/* A starting point for an audio app on the TrimUI Brick, Brick Hammer and
 * Brick Pro, on TrimUI OS or MustardOS. One binary for all of them. It puts
 * together what the sydilaxe apps learned the hard way:
 *
 *   screen   a 1024x768 window; the picture is drawn into a software surface
 *            and sent opaque (MustardOS shows alpha: alpha 0 is black); the
 *            Brick Pro's screen wake under MustardOS, and the renderer made
 *            again after it and whenever SDL loses it
 *   input    raw joystick numbers for each firmware and device (brick_input)
 *   sound    48 kHz 16-bit stereo, 512-frame buffers, follows USB audio
 *            devices as they come and go, flush-to-zero, memory locked
 *   LEDs     Brick Pro: each LED (ringleds: level meter, stick rings, beat);
 *            Brick / Hammer: named zones (brick_zones); both put back at quit
 *   MIDI     USB MIDI notes play the synth (brick_midi)
 *   log      log.txt with the performance log (perflog)
 *
 * What it does: a sine voice. A / B / X / Y play four notes, the d-pad moves
 * the pitch, the Brick Pro's left stick bends it, F1 / F2 light up, MENU
 * quits. The squares on screen show what is pressed.
 *
 * Settings (env, in launch.sh): SK_BUFFER, SK_RATE, SK_AUDIO=internal|<name>,
 * SK_LEDS=0, SK_LED_BRIGHTNESS=1..60, SK_JOYMAP=A=1,..., SK_LOG=0|1|2,
 * SK_SCREEN_WAKE (set by mux_launch.sh). */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "brick_input.h"
#include "brick_midi.h"
#include "brick_zones.h"
#include "perflog.h"
#include "ringleds.h"

#define W 1024
#define H 768

static int env_int(const char *n, int def)
{
    const char *v = getenv(n);
    return v && *v ? atoi(v) : def;
}

/* ================================================================ sound */
static SDL_AudioDeviceID adev;
static int rate = 48000, buffer = 512;
static char adev_name[128];
static _Atomic float target_hz = 0.f; /* 0: silent */
static _Atomic float bend = 0.f;      /* semitones */

static void flush_denormals(void)
{
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= 1u << 24; /* FZ */
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    static int ftz;
    if (!ftz)
        flush_denormals(), ftz = 1;
    int16_t *s = (int16_t *)stream;
    int frames = len / 4;
    uint64_t t0 = perf_audio_begin(frames);
    static double ph;
    static float amp, hz = 220.f;
    float want = atomic_load(&target_hz), b = atomic_load(&bend);
    for (int i = 0; i < frames; i++) {
        float a_target = want > 0.f ? 0.3f : 0.f;
        amp += (a_target - amp) * 0.002f; /* no clicks */
        if (want > 0.f)
            hz += (want - hz) * 0.01f;
        ph += hz * powf(2.f, b / 12.f) / rate;
        if (ph >= 1.0)
            ph -= 1.0;
        int16_t v = (int16_t)(sinf((float)ph * 6.2831853f) * amp * 32767.f);
        s[i * 2] = s[i * 2 + 1] = v;
    }
    ringleds_audio(s, frames);
    perf_audio_output(s, frames);
    perf_audio_end(t0);
}

static int is_usb_name(const char *n) { return n && (strcasestr(n, "usb") || strcasestr(n, "uac") || strcasestr(n, "dac")); }

/* a USB sound card or DAC while one is plugged in, else the speaker / headphones (NULL) */
static const char *pick_output(void)
{
    const char *want = getenv("SK_AUDIO");
    if (want && !strcasecmp(want, "internal"))
        return NULL;
    for (int i = 0; i < SDL_GetNumAudioDevices(0); i++) {
        const char *n = SDL_GetAudioDeviceName(i, 0);
        if (n && (want && *want && strcasecmp(want, "auto") ? strcasestr(n, want) != NULL : is_usb_name(n)))
            return n;
    }
    return NULL;
}

static void audio_open(const char *name)
{
    char keep[128];
    snprintf(keep, sizeof keep, "%s", name ? name : ""); /* SDL's list changes when reopening */
    name = *keep ? keep : NULL;
    if (adev)
        SDL_CloseAudioDevice(adev), adev = 0;
    SDL_AudioSpec want = {0}, have;
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)buffer;
    want.callback = audio_cb;
    adev = SDL_OpenAudioDevice(name, 0, &want, &have, 0); /* 0: SDL converts to what the device takes */
    if (!adev && name)
        name = NULL, adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return;
    }
    snprintf(adev_name, sizeof adev_name, "%s", name ? name : "");
    perf_audio_open(rate, have.samples, name);
    perf_log("AUDIO", "SDL %s driver: %d Hz, %d ch, %d frames a buffer, %s", SDL_GetCurrentAudioDriver(), have.freq,
             have.channels, have.samples, name ? name : "speaker / headphones");
    SDL_PauseAudioDevice(adev, 0);
}

static void audio_hotplug(void)
{
    const char *n = pick_output();
    if (adev && !strcmp(n ? n : "", adev_name))
        return;
    audio_open(n);
}

/* ================================================================ MIDI */
static _Atomic int midi_note = -1;
static void on_midi(const uint8_t *m, int len, uint64_t t, void *user)
{
    (void)t, (void)user;
    if (len == 3 && (m[0] & 0xf0) == 0x90 && m[2])
        atomic_store(&midi_note, m[1]);
    else if (len == 3 && ((m[0] & 0xf0) == 0x80 || ((m[0] & 0xf0) == 0x90 && !m[2])) && atomic_load(&midi_note) == m[1])
        atomic_store(&midi_note, -1);
}

/* ================================================================ screen */
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_Surface *surf;
static int screen_reset, draw_fails;
static Uint32 last_rebuild;

static void make_screen(int renderer)
{
    if (tex)
        SDL_DestroyTexture(tex), tex = NULL;
    if (renderer) {
        if (ren)
            SDL_DestroyRenderer(ren);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        if (!ren)
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H) : NULL;
    if (tex)
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    last_rebuild = SDL_GetTicks();
}

/* MustardOS on the Brick Pro: run wake_screen.sh once the window shows (double fork: never waited for) */
static void screen_wake(void)
{
    const char *cmd = getenv("SK_SCREEN_WAKE");
    if (!cmd || !*cmd)
        return;
    pid_t p = fork();
    if (p == 0) {
        if (fork() == 0) {
            /* exec the path itself, not through "sh -c": the app folder's name may have spaces */
            execl("/bin/sh", "sh", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (p > 0)
        waitpid(p, NULL, 0);
}

static void present(void)
{
    static int frames;
    /* opaque: MustardOS's display shows the alpha channel */
    for (int y = 0; y < H; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)surf->pixels + y * surf->pitch);
        for (int x = 0; x < W; x++)
            row[x] |= 0xFF000000u;
    }
    if (screen_reset)
        make_screen(screen_reset == 2), screen_reset = 0;
    int bad = !tex || SDL_UpdateTexture(tex, NULL, surf->pixels, surf->pitch) < 0;
    if (ren) {
        SDL_RenderClear(ren);
        bad |= !tex || SDL_RenderCopy(ren, tex, NULL, NULL) < 0;
        SDL_RenderPresent(ren);
    } else
        bad = 1;
    if (bad && SDL_GetTicks() - last_rebuild > 1000)
        make_screen(++draw_fails > 2 || !ren);
    if (++frames == 10)
        screen_wake();
    if (frames == 10 + 75 && getenv("SK_SCREEN_WAKE")) /* after the wake's second pass */
        make_screen(1);
}

static void box(int x, int y, int w, int h, Uint32 c)
{
    SDL_Rect r = {x, y, w, h};
    SDL_FillRect(surf, &r, c);
}

/* ================================================================ main */
int main(void)
{
    perf_init("SK", NULL);
    if (env_int("SK_MLOCK", 1) && mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        fprintf(stderr, "could not lock memory\n");
    rate = env_int("SK_RATE", 48000);
    buffer = env_int("SK_BUFFER", 512);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    win = SDL_CreateWindow("skeleton", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H,
                           SDL_WINDOW_SHOWN | (env_int("SK_WINDOWED", 0) ? 0 : SDL_WINDOW_FULLSCREEN));
    surf = SDL_CreateRGBSurface(0, W, H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    if (!win || !surf)
        return 1;
    make_screen(1);

    bi_init("SK");
    bi_open_joysticks();
    int pro = bi_is_brickpro();
    int leds = pro ? ringleds_open("SK", 1) : bz_open("SK");
    perf_log("START", "%s on %s, LEDs %s", pro ? "Brick Pro" : "Brick / Brick Hammer",
             bi_is_mustardos() ? "MustardOS" : "TrimUI OS", leds ? "on" : "off");

    audio_open(pick_output());
    bm_start(on_midi, NULL, -1);

    static const float notes[4] = {220.f, 277.18f, 329.63f, 440.f}; /* A B X Y */
    int held[BI_COUNT] = {0}, octave = 0, running = 1;
    float sx[2] = {0}, sy[2] = {0};
    while (running) {
        Uint32 t0 = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = 0;
            else if (e.type == SDL_AUDIODEVICEADDED || e.type == SDL_AUDIODEVICEREMOVED) {
                if (!e.adevice.iscapture)
                    audio_hotplug();
            } else if (e.type == SDL_RENDER_TARGETS_RESET)
                screen_reset = screen_reset > 1 ? screen_reset : 1;
            else if (e.type == SDL_RENDER_DEVICE_RESET)
                screen_reset = 2;
            BiEvent ev[8];
            int n = bi_event(&e, ev, 8);
            for (int i = 0; i < n; i++) {
                if (ev[i].type == BI_EV_STICK) {
                    sx[ev[i].stick] = ev[i].x, sy[ev[i].stick] = ev[i].y;
                    continue;
                }
                held[ev[i].button] = ev[i].down;
                if (perf_level() >= PERF_DETAIL)
                    perf_log("INPUT", "%s %s", bi_button_names[ev[i].button], ev[i].down ? "down" : "up");
                if (!ev[i].down)
                    continue;
                if (ev[i].button == BI_MENU)
                    running = 0;
                if (ev[i].button == BI_UP && octave < 2)
                    octave++;
                if (ev[i].button == BI_DOWN && octave > -2)
                    octave--;
            }
        }
        char note[80];
        while (bm_take_notice(note, sizeof note))
            perf_log("MIDI", "%s", note);

        /* the sound */
        float hz = 0.f;
        for (int k = 0; k < 4; k++)
            if (held[BI_A + k])
                hz = notes[k] * powf(2.f, (float)octave);
        int mn = atomic_load(&midi_note);
        if (mn >= 0)
            hz = 440.f * powf(2.f, (mn - 69) / 12.f);
        atomic_store(&target_hz, hz);
        atomic_store(&bend, sx[0] * 2.f);

        /* the LEDs */
        uint32_t f1 = held[BI_F1] ? 0x40c0ff : 0, f2 = held[BI_F2] ? 0xff8020 : 0;
        if (pro)
            ringleds_update(sx[0], sy[0], 0x46b4ff, sx[1], sy[1], 0xff6414, hz > 0.f, (int)(SDL_GetTicks() / 125),
                            120.f, f1, f2);
        else if (leds) {
            bz_set(BZ_F1, f1);
            bz_set(BZ_F2, f2);
            bz_set(BZ_TOP, hz > 0.f ? 0x20d040 : 0);
            bz_set(BZ_SHOULDERS, (held[BI_L1] || held[BI_R1] || held[BI_L2] || held[BI_R2]) ? 0xb4b4b4 : 0);
        }

        /* the screen: a square per button, lit while held; the sticks as dots */
        box(0, 0, W, H, 0x101418);
        for (int b = 0; b < BI_COUNT; b++)
            box(40 + (b % 10) * 96, 80 + (b / 10) * 96, 80, 80, held[b] ? 0xffb428 : 0x2a3038);
        for (int s = 0; s < 2; s++) {
            int cx = 300 + s * 424, cy = 520;
            box(cx - 100, cy - 100, 200, 200, 0x1c2228);
            box(cx + (int)(sx[s] * 90) - 8, cy - (int)(sy[s] * 90) - 8, 16, 16, 0x46b4ff);
        }
        present();
        perf_frame((uint64_t)(SDL_GetTicks() - t0) * 1000);
        perf_tick();
        Uint32 spent = SDL_GetTicks() - t0;
        if (spent < 16)
            SDL_Delay(16 - spent);
    }

    bm_stop();
    if (adev)
        SDL_CloseAudioDevice(adev);
    ringleds_close();
    bz_close();
    bi_switch_close();
    perf_close();
    SDL_Quit();
    return 0;
}
