/* USB MIDI through ALSA raw MIDI: see brick_midi.h. */
#define _GNU_SOURCE
#include "brick_midi.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint64_t bm_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
}

/* ---------------------------------------------------------------- notices */
static pthread_mutex_t nmx = PTHREAD_MUTEX_INITIALIZER;
static char notices[8][64];
static int n_head, n_tail;

static void notice(const char *fmt, const char *arg)
{
    pthread_mutex_lock(&nmx);
    if (n_head - n_tail < 8) {
        snprintf(notices[n_head % 8], sizeof notices[0], fmt, arg);
        n_head++;
    }
    pthread_mutex_unlock(&nmx);
}

int bm_take_notice(char *buf, int n)
{
    int got = 0;
    pthread_mutex_lock(&nmx);
    if (n_tail < n_head) {
        snprintf(buf, (size_t)n, "%s", notices[n_tail % 8]);
        n_tail++;
        got = 1;
    }
    pthread_mutex_unlock(&nmx);
    return got;
}

/* ---------------------------------------------------------------- ports */
#define MAXPORT 8
typedef struct {
    int fd;
    char path[40], name[40];
    /* the parser's state for this port */
    uint8_t status, msg[3];
    int have, need, in_sysex;
} Port;
static Port ports[MAXPORT];
static volatile int nports;
static pthread_mutex_t pmx = PTHREAD_MUTEX_INITIALIZER; /* the port list, for bm_send */

int bm_port_count(void) { return nports; }

static void port_name(int card, char *b, int n)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/asound/card%d/id", card);
    FILE *f = fopen(p, "r");
    b[0] = 0;
    if (f) {
        if (fgets(b, n, f))
            b[strcspn(b, "\n")] = 0;
        fclose(f);
    }
    if (!b[0])
        snprintf(b, (size_t)n, "card %d", card);
}

static void scan(void)
{
    DIR *d = opendir("/dev/snd");
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) && nports < MAXPORT) {
        int card, devn;
        if (sscanf(e->d_name, "midiC%dD%d", &card, &devn) != 2)
            continue;
        char path[40];
        snprintf(path, sizeof path, "/dev/snd/%.30s", e->d_name);
        int known = 0;
        for (int i = 0; i < nports; i++)
            known |= !strcmp(ports[i].path, path);
        if (known)
            continue;
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        pthread_mutex_lock(&pmx);
        Port *p = &ports[nports];
        memset(p, 0, sizeof *p);
        p->fd = fd;
        snprintf(p->path, sizeof p->path, "%s", path);
        port_name(card, p->name, sizeof p->name);
        nports++;
        pthread_mutex_unlock(&pmx);
        notice("MIDI: %.40s", p->name);
    }
    closedir(d);
}

static void drop(int i)
{
    notice("MIDI: %.40s GONE", ports[i].name);
    pthread_mutex_lock(&pmx);
    close(ports[i].fd);
    ports[i] = ports[--nports];
    pthread_mutex_unlock(&pmx);
}

void bm_send(const uint8_t *b, int n)
{
    pthread_mutex_lock(&pmx);
    for (int i = 0; i < nports; i++)
        if (write(ports[i].fd, b, (size_t)n) < 0) {
            /* a full port drops the bytes; a vanished one is dropped when read */
        }
    pthread_mutex_unlock(&pmx);
}

/* ---------------------------------------------------------------- the parser */
static BmCallback cb_;
static void *user_;

static int data_len(uint8_t status)
{
    switch (status & 0xf0) {
    case 0xc0: /* program change */
    case 0xd0: /* channel pressure */
        return 1;
    case 0xf0:
        switch (status) {
        case 0xf1: /* MTC quarter frame */
        case 0xf3: /* song select */
            return 1;
        case 0xf2: /* song position */
            return 2;
        default:
            return 0;
        }
    default:
        return 2;
    }
}

static void feed(Port *p, const uint8_t *b, int n, uint64_t t)
{
    for (int i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c >= 0xf8) { /* realtime: on its own, even mid-message */
            cb_(&c, 1, t, user_);
            continue;
        }
        if (c == 0xf0) {
            p->in_sysex = 1;
            continue;
        }
        if (c == 0xf7) {
            p->in_sysex = 0;
            continue;
        }
        if (c & 0x80) { /* a status byte */
            p->in_sysex = 0;
            p->status = c;
            p->have = 0;
            p->need = data_len(c);
            if (p->need == 0) { /* tune request and the like */
                cb_(&c, 1, t, user_);
                if (c >= 0xf0)
                    p->status = 0; /* system common cancels running status */
            }
            continue;
        }
        if (p->in_sysex || !p->status)
            continue;
        p->msg[1 + p->have++] = c;
        if (p->have == p->need) {
            p->msg[0] = p->status;
            cb_(p->msg, 1 + p->need, t, user_);
            p->have = 0; /* running status: the next data bytes reuse it */
            if (p->status >= 0xf0)
                p->status = 0;
        }
    }
}

/* ---------------------------------------------------------------- the thread */
static pthread_t th;
static volatile int running;
static int core_;

static void *loop(void *arg)
{
    (void)arg;
    if (core_ >= 0) {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(core_, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof cs, &cs);
    }
    uint64_t next_scan = 0;
    while (running) {
        uint64_t t = bm_now_us();
        if (t >= next_scan) {
            scan();
            next_scan = t + 1000000;
        }
        struct pollfd pf[MAXPORT];
        int np = nports;
        for (int i = 0; i < np; i++)
            pf[i].fd = ports[i].fd, pf[i].events = POLLIN, pf[i].revents = 0;
        poll(pf, (nfds_t)np, 2); /* a short wait: the rescan and bm_stop stay prompt */
        t = bm_now_us();
        for (int i = np - 1; i >= 0; i--) {
            if (!(pf[i].revents & (POLLIN | POLLERR | POLLHUP)))
                continue;
            uint8_t buf[256];
            ssize_t r = read(ports[i].fd, buf, sizeof buf);
            if (r > 0)
                feed(&ports[i], buf, (int)r, t);
            else if (r == 0 || (errno != EAGAIN && errno != EINTR))
                drop(i);
        }
    }
    pthread_mutex_lock(&pmx);
    for (int i = 0; i < nports; i++)
        close(ports[i].fd);
    nports = 0;
    pthread_mutex_unlock(&pmx);
    return NULL;
}

int bm_start(BmCallback cb, void *user, int core)
{
    if (running || !cb)
        return 0;
    cb_ = cb, user_ = user, core_ = core;
    running = 1;
    pthread_attr_t a;
    pthread_attr_init(&a);
    struct sched_param sp = {.sched_priority = 40};
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    pthread_attr_setschedparam(&a, &sp);
    /* real-time where allowed (root on the device), else an ordinary thread */
    if (pthread_create(&th, &a, loop, NULL) != 0 && pthread_create(&th, NULL, loop, NULL) != 0)
        running = 0;
    pthread_attr_destroy(&a);
    return running;
}

void bm_stop(void)
{
    if (!running)
        return;
    running = 0;
    pthread_join(th, NULL);
}
