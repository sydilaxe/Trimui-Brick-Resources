/* The Brick's LEDs by named zone: see brick_zones.h and docs/leds.md. */
#include "brick_zones.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const zone_name[BZ_COUNT] = {"f1", "f2", "m", "lr"};
static const char *const scale_name[] = {"max_scale_f1f2", "max_scale", "max_scale_lr"};
#define NSCALE 3
#define SAVED_FILE "leds_saved.txt"

static char dir[256] = "/sys/class/led_anim";
static int on;
static pthread_t thread;
static int have_thread;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static volatile int running;
static uint32_t want[BZ_COUNT], written[BZ_COUNT];
static int have_written[BZ_COUNT];
static char saved_col[BZ_COUNT][16], saved_eff[BZ_COUNT][8], saved_scale[NSCALE][8], saved_enable[8];

static int put(const char *name, const char *val)
{
    char p[320];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (!f)
        return 0;
    int ok = fputs(val, f) >= 0;
    return fclose(f) == 0 && ok;
}

static void get(const char *name, char *out, int n)
{
    char p[320];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    out[0] = 0;
    FILE *f = fopen(p, "r");
    if (f) {
        if (!fgets(out, n, f))
            out[0] = 0;
        fclose(f);
    }
    out[strcspn(out, " \r\n")] = 0;
    if (!out[0])
        snprintf(out, (size_t)n, "-");
}

/* the colour, then the effect: a new colour shows once the effect is written again (4: static) */
static void write_zone(int z, uint32_t c)
{
    char name[40], v[16];
    snprintf(name, sizeof name, "effect_rgb_hex_%s", zone_name[z]);
    snprintf(v, sizeof v, "%06X \n", (unsigned)(c & 0xffffff));
    put(name, v);
    snprintf(name, sizeof name, "effect_%s", zone_name[z]);
    put(name, "4\n");
}

static void flush(void)
{
    uint32_t w[BZ_COUNT];
    pthread_mutex_lock(&mx);
    memcpy(w, want, sizeof w);
    pthread_mutex_unlock(&mx);
    for (int z = 0; z < BZ_COUNT; z++)
        if (!have_written[z] || w[z] != written[z]) {
            write_zone(z, w[z]);
            written[z] = w[z];
            have_written[z] = 1;
        }
}

static void *led_thread(void *arg)
{
    (void)arg;
    while (running) {
        flush();
        usleep(25000);
    }
    return NULL;
}

int bz_open(const char *prefix)
{
    char name[64];
    snprintf(name, sizeof name, "%s_LEDS", prefix);
    const char *e = getenv(name);
    if (e && *e == '0')
        return 0;
    snprintf(name, sizeof name, "%s_LED_DIR", prefix);
    e = getenv(name);
    if (e && *e)
        snprintf(dir, sizeof dir, "%s", e);
    char p[320];
    snprintf(p, sizeof p, "%s/effect_rgb_hex_f1", dir);
    if (access(p, W_OK) != 0)
        return 0; /* no such driver here, or not ours to write */

    /* what to put back: from the file a crashed run left, else as found */
    FILE *sf = fopen(SAVED_FILE, "r");
    int n = 0;
    if (sf) {
        for (int z = 0; z < BZ_COUNT; z++)
            n += fscanf(sf, "%15s %7s", saved_col[z], saved_eff[z]);
        for (int i = 0; i < NSCALE; i++)
            n += fscanf(sf, "%7s", saved_scale[i]);
        n += fscanf(sf, "%7s", saved_enable);
        fclose(sf);
    }
    if (n != 2 * BZ_COUNT + NSCALE + 1) {
        for (int z = 0; z < BZ_COUNT; z++) {
            snprintf(name, sizeof name, "effect_rgb_hex_%s", zone_name[z]);
            get(name, saved_col[z], sizeof saved_col[z]);
            snprintf(name, sizeof name, "effect_%s", zone_name[z]);
            get(name, saved_eff[z], sizeof saved_eff[z]);
        }
        for (int i = 0; i < NSCALE; i++)
            get(scale_name[i], saved_scale[i], sizeof saved_scale[i]);
        get("effect_enable", saved_enable, sizeof saved_enable);
        if ((sf = fopen(SAVED_FILE, "w"))) {
            for (int z = 0; z < BZ_COUNT; z++)
                fprintf(sf, "%s %s\n", saved_col[z], saved_eff[z]);
            for (int i = 0; i < NSCALE; i++)
                fprintf(sf, "%s\n", saved_scale[i]);
            fprintf(sf, "%s\n", saved_enable);
            fclose(sf);
        }
    }

    /* MustardOS starts the LEDs at brightness 0: set it */
    snprintf(name, sizeof name, "%s_LED_BRIGHTNESS", prefix);
    e = getenv(name);
    int b = e && *e ? atoi(e) : 60;
    b = b < 1 ? 1 : b > 60 ? 60 : b;
    char v[16];
    snprintf(v, sizeof v, "%d\n", b);
    for (int i = 0; i < NSCALE; i++)
        put(scale_name[i], v);
    put("effect_enable", "1\n");
    for (int z = 0; z < BZ_COUNT; z++) {
        snprintf(name, sizeof name, "effect_cycles_%s", zone_name[z]);
        put(name, "-1\n");
    }
    memset(want, 0, sizeof want);
    memset(have_written, 0, sizeof have_written);
    on = 1;
    running = 1;
    have_thread = pthread_create(&thread, NULL, led_thread, NULL) == 0;
    if (!have_thread) {
        running = 0;
        flush(); /* at least dark to start with */
    }
    return 1;
}

void bz_set(BzZone z, uint32_t rgb)
{
    if (z < 0 || z >= BZ_COUNT)
        return;
    pthread_mutex_lock(&mx);
    want[z] = rgb & 0xffffff;
    pthread_mutex_unlock(&mx);
}

void bz_close(void)
{
    if (!on)
        return;
    if (have_thread) {
        running = 0;
        pthread_join(thread, NULL);
        have_thread = 0;
    }
    /* back as they were: colour, then effect (writing it starts it), brightness, effect_enable */
    for (int z = 0; z < BZ_COUNT; z++) {
        char name[40], val[24];
        if (strcmp(saved_col[z], "-")) {
            snprintf(name, sizeof name, "effect_rgb_hex_%s", zone_name[z]);
            snprintf(val, sizeof val, "%s \n", saved_col[z]);
            put(name, val);
        }
        if (strcmp(saved_eff[z], "-")) {
            snprintf(name, sizeof name, "effect_%s", zone_name[z]);
            snprintf(val, sizeof val, "%s\n", saved_eff[z]);
            put(name, val);
        }
    }
    for (int i = 0; i < NSCALE; i++)
        if (strcmp(saved_scale[i], "-")) {
            char val[32];
            snprintf(val, sizeof val, "%s\n", saved_scale[i]);
            put(scale_name[i], val);
        }
    if (strcmp(saved_enable, "-")) {
        char val[32];
        snprintf(val, sizeof val, "%s\n", saved_enable);
        put("effect_enable", val);
    }
    remove(SAVED_FILE);
    on = 0;
}

uint32_t bz_scale(uint32_t c, float k)
{
    k = k < 0.f ? 0.f : k > 1.f ? 1.f : k;
    uint32_t r = (uint32_t)(((c >> 16) & 255) * k), g = (uint32_t)(((c >> 8) & 255) * k), b = (uint32_t)((c & 255) * k);
    return r << 16 | g << 8 | b;
}
