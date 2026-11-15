/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USB audio for the TrimUI Brick / Brick Pro: see usbaudio.h. */
#define _GNU_SOURCE
#include "usbaudio.h"

#include <glob.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "aring.h"

#define RATE 48000
#define MAXCH 8
#define MAXFR 4096

static UaConfig cfg;
static ARing ring;
static SDL_AudioDeviceID out_dev, in_dev;
static char out_name[128], in_name[128];      /* open ("" = the default device) */
static char out_want[128], in_want[128];      /* chosen in SETUP ("" = AUTO) */
static int in_pair_want, out_pair_want;
static int buf_user;                          /* 0 AUTO, else frames */
#define NDEVBUF 8
static struct { char id[48]; int frames; } devbuf[NDEVBUF]; /* a buffer chosen for one interface (by card id) */
static int env_buffer;                        /* <app>_BUFFER: fixed for every interface */
static int buffer = 256;                      /* frames, now */
static atomic_int in_ch = 2, out_ch = 2, in_pair, out_pair, in_block = 256, have_in, guard;
static atomic_int rt_out, rt_in;              /* 0 not yet, 1 on, -1 refused */
static atomic_uint out_calls, out_clips, dropouts, hw_period_a;
static atomic_ullong last_call_ns;
static unsigned reopens, notes;
static int started;
static Uint32 hw_at, last_out_check, last_in_check, grow_at;
static unsigned seen_out_calls, seen_w, drop_mark;
static char hw_in[64], hw_out[64];
static int matched_period; /* the buffer was matched to this interface's period (once a device) */
static int direct_in, direct_out, direct_off; /* DIRECT in use; off (by setting, or it failed on this device) */
static char direct_failed[128];

static void lg(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void lg(const char *tag, const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (cfg.log) cfg.log(tag, "%s", b);
    else fprintf(stderr, "%s %s\n", tag, b);
}

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void go_realtime(atomic_int *flag, int prio)
{
    if (atomic_load_explicit(flag, memory_order_relaxed)) return;
    struct sched_param sp = {.sched_priority = prio};
    atomic_store(flag, pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0 ? 1 : -1);
}

static void flush_denormals(void)
{
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= 1u << 24;
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__)
    __builtin_ia32_ldmxcsr(__builtin_ia32_stmxcsr() | 0x8040);
#endif
}

/* ---------------------------------------------------------------- the audio threads */
static void in_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    go_realtime(&rt_in, 78);
    int ch = atomic_load_explicit(&in_ch, memory_order_relaxed);
    ar_push(&ring, (const float *)stream, len / (int)(sizeof(float) * (unsigned)ch), ch, atomic_load_explicit(&in_pair, memory_order_relaxed), now_ns());
}

static void out_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    static int ftz;
    static float in[MAXFR * 2], out[MAXFR * 2];
    if (!ftz) flush_denormals(), ftz = 1;
    go_realtime(&rt_out, 80);
    uint64_t t = now_ns(), prev = atomic_exchange_explicit(&last_call_ns, t, memory_order_relaxed);
    /* a dropout: a gap past what the device can hold (two of its periods, or of our buffers) */
    unsigned per = atomic_load_explicit(&hw_period_a, memory_order_relaxed);
    int ch = atomic_load_explicit(&out_ch, memory_order_relaxed), pair = atomic_load_explicit(&out_pair, memory_order_relaxed);
    int frames = len / (int)(sizeof(float) * (unsigned)ch);
    if (frames > MAXFR) frames = MAXFR;
    double hold_ns = 2.0 * (double)((int)per > frames ? (int)per : frames) / RATE * 1e9;
    if (prev && (double)(t - prev) > hold_ns * 1.25) atomic_fetch_add_explicit(&dropouts, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&out_calls, 1, memory_order_relaxed);

    int live = atomic_load_explicit(&have_in, memory_order_acquire);
    int on = live ? ar_pull(&ring, in, frames, atomic_load_explicit(&in_block, memory_order_relaxed), t) : 0;
    if (!live) memset(in, 0, sizeof(float) * 2 * (size_t)frames);
    cfg.process(in, out, frames, on, cfg.ud);

    float *s = (float *)stream;
    int g = atomic_load_explicit(&guard, memory_order_relaxed), clips = 0, a = pair * 2;
    if (a + 1 >= ch) a = 0;
    memset(stream, 0, (size_t)len);
    for (int i = 0; i < frames; i++) {
        float l = out[2 * i], r = out[2 * i + 1];
        if (l > 1.f || l < -1.f) clips++, l = l > 1.f ? 1.f : -1.f;
        if (r > 1.f || r < -1.f) clips++, r = r > 1.f ? 1.f : -1.f;
        if (g) l = r = 0.f;
        if (ch == 1) s[i] = 0.5f * (l + r);
        else s[i * ch + a] = l, s[i * ch + a + 1] = r;
    }
    if (clips) atomic_fetch_add_explicit(&out_clips, (unsigned)clips, memory_order_relaxed);
}

/* ---------------------------------------------------------------- the kernel's view */
/* the ALSA card a device name belongs to: its id or its name in /proc/asound/cards; -1 if none.
 * usb: set if the kernel's USB audio driver runs it */
static int card_of(const char *dev, int *usb)
{
    *usb = 0;
    if (!dev || !*dev) return -1;
    FILE *f = fopen("/proc/asound/cards", "r");
    if (!f) return -1;
    char line[256];
    int found = -1;
    while (found < 0 && fgets(line, sizeof line, f)) {
        int card;
        char id[64] = "", rest[192] = "";
        if (sscanf(line, " %d [%63[^]]]: %191[^\n]", &card, id, rest) < 2) continue;
        for (int k = (int)strlen(id) - 1; k >= 0 && id[k] == ' '; k--) id[k] = 0;
        char *dash = strstr(rest, " - ");
        const char *name = dash ? dash + 3 : "";
        if ((strlen(id) >= 3 && strcasestr(dev, id)) || (strlen(name) >= 3 && strcasestr(dev, name))) {
            found = card;
            *usb = strstr(rest, "USB-Audio") != NULL;
        }
    }
    fclose(f);
    return found;
}

/* the ALSA card id of a device (for remembering its buffer), "" if none */
static void card_id(const char *dev, char *id, int n)
{
    int usb, card = card_of(dev, &usb);
    id[0] = 0;
    if (card < 0) return;
    char p[64];
    snprintf(p, sizeof p, "/proc/asound/card%d/id", card);
    FILE *f = fopen(p, "r");
    if (f) { if (fgets(id, n, f)) id[strcspn(id, "\r\n")] = 0; fclose(f); }
}

static int is_usb(const char *dev)
{
    int usb;
    if (!dev) return 0;
    if (strcasestr(dev, "usb") || strcasestr(dev, "uac")) return 1;
    card_of(dev, &usb);
    return usb;
}

/* a USB card's channels and rates for one direction, from stream0 (the most channels offered) */
static int native_channels(const char *dev, int capture, char *rates, int n)
{
    int usb, card = card_of(dev, &usb);
    if (rates && n) rates[0] = 0;
    if (card < 0) return 2;
    char p[64], line[256];
    snprintf(p, sizeof p, "/proc/asound/card%d/stream0", card);
    FILE *f = fopen(p, "r");
    if (!f) return 2;
    int in_dir = 0, best = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "Playback:", 9)) in_dir = !capture;
        else if (!strncmp(line, "Capture:", 8)) in_dir = capture;
        else if (in_dir) {
            int c;
            char *q = strstr(line, "Channels:");
            if (q && sscanf(q, "Channels: %d", &c) == 1 && c > best) best = c;
            q = strstr(line, "Rates:");
            if (q && rates && !rates[0]) { snprintf(rates, (size_t)n, "%s", q + 7); rates[strcspn(rates, "\r\n")] = 0; }
        }
    }
    fclose(f);
    return best > 0 ? (best > MAXCH ? MAXCH : best) : 2;
}

/* what an open stream really runs at: "S24_3LE 48000 Hz 2 ch, period 1024" */
static int read_hw(int card, int capture, char *out, int n, int *period)
{
    char pat[64];
    glob_t g;
    snprintf(pat, sizeof pat, "/proc/asound/card%d/pcm*%c/sub*/hw_params", card, capture ? 'c' : 'p');
    out[0] = 0;
    if (glob(pat, 0, NULL, &g) != 0) return 0;
    int ok = 0;
    for (size_t i = 0; i < g.gl_pathc && !ok; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        if (!f) continue;
        char line[128], fmt[32] = "", rate[32] = "", ch[16] = "", per[32] = "";
        while (fgets(line, sizeof line, f)) {
            sscanf(line, "format: %31s", fmt);
            sscanf(line, "rate: %31s", rate);
            sscanf(line, "channels: %15s", ch);
            sscanf(line, "period_size: %31s", per);
        }
        fclose(f);
        if (!*fmt) continue;
        snprintf(out, (size_t)n, "%s %s Hz %s ch, period %s", fmt, rate, ch, per);
        if (period) *period = atoi(per);
        ok = 1;
    }
    globfree(&g);
    return ok;
}

static int valid_buffer(int b) { return b == 64 || b == 128 || b == 256 || b == 512 || b == 1024 || b == 2048; }

/* ---------------------------------------------------------------- choosing and opening */
static const char *pick(int capture)
{
    const char *want = capture ? in_want : out_want;
    int n = SDL_GetNumAudioDevices(capture);
    if (*want) {
        if (!strcasecmp(want, "internal")) return NULL;
        for (int i = 0; i < n; i++) {
            const char *d = SDL_GetAudioDeviceName(i, capture);
            if (d && !strcmp(d, want)) return d;
        }
    }
    char envn[64];
    snprintf(envn, sizeof envn, "%s_%s", cfg.app, capture ? "INPUT" : "AUDIO");
    const char *env = getenv(envn);
    if (env && !strcasecmp(env, "internal")) return NULL;
    for (int i = 0; i < n; i++) {
        const char *d = SDL_GetAudioDeviceName(i, capture);
        if (!d) continue;
        if (env && *env && strcasecmp(env, "auto") ? strcasestr(d, env) != NULL : is_usb(d)) return d;
    }
    return NULL;
}

static int asked_out(void)
{
    char envn[64];
    snprintf(envn, sizeof envn, "%s_AUDIO", cfg.app);
    const char *env = getenv(envn);
    return *out_want || (env && *env && strcasecmp(env, "auto"));
}

/* DIRECT: the device through plughw:CARD=<id>. SDL's ALSA backend opens $AUDIODEV when given no
 * name, so that is set around the call. Only on ALSA, only for a USB card, and checked afterwards
 * (ua_tick) that this card's stream is the one that opened. */
static SDL_AudioDeviceID open_direct(const char *name, int capture, SDL_AudioSpec *want, SDL_AudioSpec *have)
{
    int usb, card = card_of(name, &usb);
    const char *drv = SDL_GetCurrentAudioDriver();
    if (direct_off || card < 0 || !usb || !drv || strcmp(drv, "alsa") || (name && !strcmp(name, direct_failed))) return 0;
    char p[64], id[48] = "";
    snprintf(p, sizeof p, "/proc/asound/card%d/id", card);
    FILE *f = fopen(p, "r");
    if (f) { if (fgets(id, sizeof id, f)) id[strcspn(id, "\r\n")] = 0; fclose(f); }
    if (!*id) return 0;
    char devs[96], keep[256] = "";
    const char *was = getenv("AUDIODEV");
    int had = was != NULL;
    if (had) snprintf(keep, sizeof keep, "%s", was);
    snprintf(devs, sizeof devs, "plughw:CARD=%s,DEV=0", id);
    setenv("AUDIODEV", devs, 1);
    SDL_AudioDeviceID d = SDL_OpenAudioDevice(NULL, capture, want, have, 0);
    if (had) setenv("AUDIODEV", keep, 1);
    else unsetenv("AUDIODEV");
    if (d) lg("AUDIO", "%s: DIRECT through %s", capture ? "in" : "out", devs);
    return d;
}

static void open_out(const char *name)
{
    char keep[128];
    snprintf(keep, sizeof keep, "%s", name ? name : "");
    name = *keep ? keep : NULL;
    if (out_dev) SDL_CloseAudioDevice(out_dev), out_dev = 0;
    atomic_store(&rt_out, 0);
    char rates[96];
    int ch = native_channels(name, 0, rates, sizeof rates);
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE, want.format = AUDIO_F32SYS, want.channels = (Uint8)ch, want.samples = (Uint16)buffer, want.callback = out_cb;
    out_dev = open_direct(name, 0, &want, &have);
    direct_out = out_dev != 0;
    if (!out_dev) out_dev = SDL_OpenAudioDevice(name, 0, &want, &have, 0);
    if (!out_dev && ch != 2) want.channels = 2, ch = 2, out_dev = SDL_OpenAudioDevice(name, 0, &want, &have, 0);
    if (!out_dev && name) name = NULL, want.channels = 2, ch = 2, out_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!out_dev) { out_name[0] = 0; lg("AUDIO", "no output: %s", SDL_GetError()); return; }
    snprintf(out_name, sizeof out_name, "%s", name ? name : "");
    atomic_store(&out_ch, ch);
    atomic_store(&out_pair, out_pair_want * 2 + 1 < ch ? out_pair_want : 0);
    atomic_store(&hw_period_a, 0);
    atomic_store(&last_call_ns, 0);
    int g = cfg.speaker_guard && !asked_out() && !is_usb(name);
    if (g != atomic_load(&guard)) notes |= g ? UA_NOTE_GUARD : UA_NOTE_UNGUARD;
    atomic_store(&guard, g);
    lg("AUDIO", "out: %s, %d ch (pair %d), %d Hz, %d frames (%.1f ms), SDL %s%s%s%s", name ? name : "default", ch, atomic_load(&out_pair) + 1,
       have.freq, have.samples, have.samples * 1000.0 / RATE, SDL_GetCurrentAudioDriver(), *rates ? ", rates " : "", rates,
       g ? " | no USB output: the speaker stays silent" : "");
    hw_at = SDL_GetTicks() + 1200;
    if (cfg.opened) cfg.opened(have.samples, name ? name : "default");
    SDL_PauseAudioDevice(out_dev, 0);
}

static void open_in(const char *name)
{
    char keep[128];
    snprintf(keep, sizeof keep, "%s", name ? name : "");
    name = *keep ? keep : NULL;
    if (in_dev) SDL_CloseAudioDevice(in_dev), in_dev = 0;
    atomic_store_explicit(&have_in, 0, memory_order_release);
    atomic_store(&rt_in, 0);
    ar_restart(&ring);
    char rates[96];
    int ch = native_channels(name, 1, rates, sizeof rates);
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE, want.format = AUDIO_F32SYS, want.channels = (Uint8)ch, want.samples = (Uint16)buffer, want.callback = in_cb;
    in_dev = open_direct(name, 1, &want, &have);
    direct_in = in_dev != 0;
    if (!in_dev) in_dev = SDL_OpenAudioDevice(name, 1, &want, &have, 0);
    if (!in_dev && ch != 2) want.channels = 2, ch = 2, in_dev = SDL_OpenAudioDevice(name, 1, &want, &have, 0);
    if (!in_dev && name) name = NULL, want.channels = 2, ch = 2, in_dev = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
    if (!in_dev) { in_name[0] = 0; lg("AUDIO", "no input: %s", SDL_GetError()); return; }
    snprintf(in_name, sizeof in_name, "%s", name ? name : "");
    atomic_store(&in_ch, ch);
    atomic_store(&in_pair, in_pair_want * 2 + 1 < ch ? in_pair_want : 0);
    atomic_store(&in_block, have.samples > 0 ? have.samples : buffer);
    lg("AUDIO", "in: %s, %d ch (pair %d), %d Hz, %d frames%s%s", name ? name : "default", ch, atomic_load(&in_pair) + 1, have.freq, have.samples,
       *rates ? ", rates " : "", rates);
    seen_w = atomic_load(&ring.w);
    last_in_check = SDL_GetTicks();
    atomic_store_explicit(&have_in, 1, memory_order_release);
    hw_at = SDL_GetTicks() + 1200;
    SDL_PauseAudioDevice(in_dev, 0);
}

static int buf_global; /* the buffer for an interface with none of its own (audio.txt "buffer=") */

/* the buffer for this output device: <app>_BUFFER, else the one chosen for this interface, else the
 * general one (0 = AUTO) */
static void buffer_for(const char *dev)
{
    if (env_buffer) { buf_user = env_buffer; buffer = buf_user; return; }
    char id[48];
    card_id(dev, id, sizeof id);
    buf_user = buf_global;
    for (int i = 0; i < NDEVBUF && *id; i++)
        if (!strcmp(devbuf[i].id, id)) buf_user = valid_buffer(devbuf[i].frames) ? devbuf[i].frames : 0;
    buffer = buf_user ? buf_user : 256;
}

static void follow(int force)
{
    const char *o = pick(0);
    int out_changes = force || !out_dev || strcmp(o ? o : "", out_name);
    if (out_changes && !force) buffer_for(o), matched_period = 0;
    if (out_changes) open_out(o);
    const char *i = pick(1);
    if (force || out_changes || !in_dev || strcmp(i ? i : "", in_name)) open_in(i);
    /* one interface both ways, DIRECT: one clock, one period: a smaller cushion */
    char ido[48], idi[48];
    card_id(out_name, ido, sizeof ido);
    card_id(in_name, idi, sizeof idi);
    atomic_store(&ring.locked, direct_in && direct_out && *ido && !strcmp(ido, idi));
    last_out_check = SDL_GetTicks();
    seen_out_calls = atomic_load(&out_calls);
}

/* ---------------------------------------------------------------- settings (audio.txt) */
static void save(void)
{
    if (!cfg.settings) return;
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", cfg.settings);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "in=%s\nout=%s\nin_pair=%d\nout_pair=%d\nbuffer=%d\n", in_want, out_want, in_pair_want, out_pair_want, buf_global);
    for (int i = 0; i < NDEVBUF; i++)
        if (devbuf[i].id[0]) fprintf(f, "buffer@%s=%d\n", devbuf[i].id, devbuf[i].frames);
    fclose(f);
    rename(tmp, cfg.settings);
}

static void load(void)
{
    if (!cfg.settings) return;
    FILE *f = fopen(cfg.settings, "r");
    if (!f) return;
    char line[300];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "in")) snprintf(in_want, sizeof in_want, "%s", v);
        else if (!strcmp(line, "out")) snprintf(out_want, sizeof out_want, "%s", v);
        else if (!strcmp(line, "in_pair")) in_pair_want = atoi(v) & 3;
        else if (!strcmp(line, "out_pair")) out_pair_want = atoi(v) & 3;
        else if (!strcmp(line, "buffer")) buf_user = atoi(v);
        else if (!strncmp(line, "buffer@", 7))
            for (int i = 0; i < NDEVBUF; i++)
                if (!devbuf[i].id[0]) { snprintf(devbuf[i].id, sizeof devbuf[i].id, "%s", line + 7), devbuf[i].frames = atoi(v); break; }
    }
    fclose(f);
}

/* ---------------------------------------------------------------- the app's side */
void ua_start(const UaConfig *c)
{
    cfg = *c;
    ar_init(&ring);
    load();
    char envn[64];
    snprintf(envn, sizeof envn, "%s_BUFFER", cfg.app);
    const char *e = getenv(envn);
    if (!valid_buffer(buf_user)) buf_user = 0;
    buf_global = buf_user;
    env_buffer = e && *e && valid_buffer(atoi(e)) ? atoi(e) : 0;
    snprintf(envn, sizeof envn, "%s_DIRECT", cfg.app);
    e = getenv(envn);
    direct_off = e && *e == '0';
    buffer_for(pick(0)); /* AUTO starts low (256): it matches the interface's period, and grows if it must */
    started = 1;
    follow(1);
}

void ua_event(const SDL_Event *e)
{
    if (!started) return;
    if (e->type == SDL_AUDIODEVICEADDED || e->type == SDL_AUDIODEVICEREMOVED) follow(0);
}

static void reopen(const char *why)
{
    lg("AUDIO", "reopening the devices: %s (buffer %d)", why, buffer);
    reopens++;
    follow(1);
}

int ua_tick(void)
{
    if (!started) return 0;
    Uint32 now = SDL_GetTicks();
    /* what the hardware runs, a moment after opening; the buffer matched to the period */
    if (hw_at && (Sint32)(now - hw_at) >= 0) {
        hw_at = 0;
        int usb, pc = 0, pp = 0;
        hw_in[0] = hw_out[0] = 0;
        int ci = card_of(in_name, &usb), co = card_of(out_name, &usb);
        if (ci >= 0 && read_hw(ci, 1, hw_in, sizeof hw_in, &pc)) lg("HW", "in:  card %d: %s", ci, hw_in);
        if (co >= 0 && read_hw(co, 0, hw_out, sizeof hw_out, &pp)) lg("HW", "out: card %d: %s", co, hw_out);
        /* DIRECT must have opened this card's stream; if it didn't, the shared path for this device */
        if ((direct_out && co >= 0 && !*hw_out) || (direct_in && ci >= 0 && !*hw_in)) {
            snprintf(direct_failed, sizeof direct_failed, "%s", direct_out && !*hw_out ? out_name : in_name);
            lg("AUDIO", "DIRECT didn't open %s's stream: the shared path for it", direct_failed);
            reopen("DIRECT failed");
            return (int)notes;
        }
        if (pp > 0) atomic_store(&hw_period_a, (unsigned)pp);
        if (pp > 0 && strstr(hw_out, " Hz") && !strstr(hw_out, " 48000 Hz"))
            lg("HINT", "the interface plays at %s: the sound is converted. Set it to 48 kHz if it can", hw_out);
        if (!buf_user && !matched_period && pp > buffer && pp <= 2048) {
            matched_period = 1;
            buffer = pp;
            notes |= UA_NOTE_BUFFER;
            reopen("matching the interface's period");
        }
        int rto = atomic_load(&rt_out), rti = atomic_load(&rt_in);
        lg("AUDIO", "real-time priority: output %s, input %s", rto > 0 ? "on" : rto < 0 ? "REFUSED" : "-", rti > 0 ? "on" : rti < 0 ? "REFUSED" : "-");
    }
    /* the watchdog: a device that stopped calling back is opened again */
    if (out_dev && now - last_out_check > 1500) {
        unsigned c = atomic_load(&out_calls);
        if (c == seen_out_calls && SDL_GetAudioDeviceStatus(out_dev) == SDL_AUDIO_PLAYING) { notes |= UA_NOTE_REOPEN; reopen("the output stopped"); }
        seen_out_calls = atomic_load(&out_calls), last_out_check = now;
    }
    if (in_dev && now - last_in_check > 1500) {
        unsigned w = atomic_load(&ring.w);
        if (w == seen_w && SDL_GetAudioDeviceStatus(in_dev) == SDL_AUDIO_PLAYING) { notes |= UA_NOTE_REOPEN; reopen("the input stopped"); }
        seen_w = atomic_load(&ring.w), last_in_check = now;
    }
    /* AUTO: three dropouts within 20 s and the buffer grows a step (to 2048) */
    if (!grow_at) grow_at = now + 20000, drop_mark = atomic_load(&dropouts);
    if ((Sint32)(now - grow_at) >= 0) {
        unsigned d = atomic_load(&dropouts);
        if (!buf_user && d - drop_mark >= 3 && buffer < 2048) {
            buffer *= 2;
            notes |= UA_NOTE_BUFFER;
            char why[64];
            snprintf(why, sizeof why, "%u output dropouts in 20 s", d - drop_mark);
            reopen(why);
        }
        drop_mark = atomic_load(&dropouts), grow_at = now + 20000;
    }
    int n = (int)notes;
    notes = 0;
    return n;
}

const char *ua_note_text(int note, char *b, int n)
{
    switch (note) {
    case UA_NOTE_GUARD: snprintf(b, (size_t)n, "NO USB OUTPUT: SPEAKER MUTED"); break;
    case UA_NOTE_UNGUARD: snprintf(b, (size_t)n, "OUTPUT: %s", *out_name ? out_name : "DEFAULT"); break;
    case UA_NOTE_BUFFER: snprintf(b, (size_t)n, "AUDIO BUFFER %d", buffer); break;
    case UA_NOTE_REOPEN: snprintf(b, (size_t)n, "AUDIO REOPENED"); break;
    default: b[0] = 0;
    }
    return b;
}

void ua_stop(void)
{
    if (out_dev) SDL_CloseAudioDevice(out_dev), out_dev = 0;
    if (in_dev) SDL_CloseAudioDevice(in_dev), in_dev = 0;
    started = 0;
}

void ua_cycle_device(int capture, int dir)
{
    int n = SDL_GetNumAudioDevices(capture), idx = -1;
    char *want = capture ? in_want : out_want;
    for (int i = 0; i < n; i++)
        if (*want && !strcmp(want, SDL_GetAudioDeviceName(i, capture))) idx = i;
    idx += dir;
    if (idx < -1) idx = n - 1;
    if (idx >= n) idx = -1;
    if (idx < 0) want[0] = 0;
    else snprintf(want, 128, "%s", SDL_GetAudioDeviceName(idx, capture));
    if (!capture) buffer_for(pick(0)), matched_period = 0; /* this interface's own buffer, if it has one */
    save();
    follow(1); /* both directions again: the buffer, and whether they are one interface (the cushion) */
}

void ua_cycle_pair(int capture, int dir)
{
    int ch = atomic_load(capture ? &in_ch : &out_ch), np = ch / 2 > 0 ? ch / 2 : 1;
    int *w = capture ? &in_pair_want : &out_pair_want;
    *w = ((atomic_load(capture ? &in_pair : &out_pair) + dir) % np + np) % np;
    atomic_store(capture ? &in_pair : &out_pair, *w); /* takes effect at once: no reopening */
    save();
}

void ua_cycle_buffer(int dir)
{
    static const int B[7] = {0, 64, 128, 256, 512, 1024, 2048};
    int k = 0;
    for (int i = 0; i < 7; i++) if (B[i] == buf_user) k = i;
    k += dir;
    if (k < 0 || k > 6) return; /* the ends: AUTO and 2048 (no wrapping round from AUTO to 2048) */
    buf_user = B[k];
    buffer = buf_user ? buf_user : 256;
    matched_period = 0;
    /* kept for this interface (by its card id), so each interface comes back with its own */
    char id[48];
    card_id(out_name, id, sizeof id);
    if (*id) {
        int slot = -1;
        for (int i = 0; i < NDEVBUF; i++) if (!strcmp(devbuf[i].id, id)) slot = i;
        for (int i = 0; i < NDEVBUF && slot < 0; i++) if (!devbuf[i].id[0]) slot = i;
        if (slot < 0) slot = 0;
        snprintf(devbuf[slot].id, sizeof devbuf[slot].id, "%s", id);
        devbuf[slot].frames = buf_user;
    } else
        buf_global = buf_user;
    save();
    reopen(buf_user ? "the buffer was set" : "the buffer is AUTO");
}

void ua_status(UaStatus *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->in_name, sizeof s->in_name, "%s", in_dev ? (*in_name ? in_name : "DEFAULT") : "");
    snprintf(s->out_name, sizeof s->out_name, "%s", out_dev ? (*out_name ? out_name : "DEFAULT") : "");
    s->in_auto = !*in_want, s->out_auto = !*out_want;
    s->in_ch = atomic_load(&in_ch), s->out_ch = atomic_load(&out_ch);
    s->in_pair = atomic_load(&in_pair), s->out_pair = atomic_load(&out_pair);
    s->buffer = buffer, s->buffer_auto = !buf_user;
    s->guard = atomic_load(&guard);
    s->direct_in = in_dev && direct_in, s->direct_out = out_dev && direct_out;
    s->have_input = in_dev != 0;
    int ro = atomic_load(&rt_out), ri = atomic_load(&rt_in);
    s->rt = ro < 0 || ri < 0 ? -1 : ro > 0 ? 1 : 0;
    snprintf(s->hw_in, sizeof s->hw_in, "%s", hw_in);
    snprintf(s->hw_out, sizeof s->hw_out, "%s", hw_out);
    s->underruns = atomic_load(&ring.underruns), s->skips = atomic_load(&ring.skips);
    s->dropouts = atomic_load(&dropouts), s->in_clips = atomic_load(&ring.clips), s->out_clips = atomic_load(&out_clips);
    s->reopens = reopens;
    s->cushion = atomic_load(&ring.fill), s->margin = atomic_load(&ring.margin), s->drift_ppm = atomic_load(&ring.ppm);
    unsigned per = atomic_load(&hw_period_a);
    float outbuf = 2.f * (float)((int)per > buffer ? (int)per : buffer);
    s->latency_ms = (float)(s->cushion + outbuf) * 1000.f / RATE;
}
