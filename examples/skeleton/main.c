/* A starting point for an audio app on the TrimUI Brick, Brick Hammer and
 * Brick Pro, on MustardOS. One binary for all of them. It puts
 * together what the sydilaxe apps learned the hard way:
 *
 *   screen   a 1024x768 window; the picture is drawn into a software surface
 *            and sent opaque (MustardOS shows alpha: alpha 0 is black); the
 *            Brick Pro's screen wake under MustardOS, and the renderer made
 *            again after it and whenever SDL loses it
 *   input    raw joystick numbers for each firmware and device (brick_input)
 *   sound    lib/brickio's usbaudio: USB interface first (in and out), opened
 *            direct for low latency, 32-bit float at its own resolution and
 *            channels, real-time threads, a buffer that starts small and grows
 *            only if it must, the input's drift followed, hot-plug, a
 *            watchdog; flush-to-zero, memory locked. USB input passes through.
 *   LEDs     Brick Pro: each LED (ringleds: level meter, stick rings, beat);
 *            Brick / Hammer: named zones (brick_zones); both put back at quit
 *   MIDI     USB MIDI notes play the synth (lib/brickio's midi: a reader
 *            thread that wakes the main loop at once)
 *   log      log.txt with the performance log (perflog)
 *
 * What it does: a sine voice. A / B / X / Y play four notes, the d-pad moves
 * the pitch, the Brick Pro's left stick bends it, F1 / F2 light up, MENU
 * quits. The squares on screen show what is pressed.
 *
 * Settings (env, in launch.sh): SK_BUFFER (64-2048; AUTO without it),
 * SK_AUDIO / SK_INPUT=internal|<part of a name>, SK_DIRECT=0,
 * SK_LEDS=0, SK_LED_BRIGHTNESS=1..60, SK_JOYMAP=A=1,..., SK_LOG=0|1|2,
 * SK_SCREEN_WAKE (set by mux_launch.sh). */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "brick_input.h"
#include "brick_zones.h"
#include "midi.h"
#include "perflog.h"
#include "ringleds.h"
#include "usbaudio.h"

#define W 1024
#define H 768

static int env_int(const char *n, int def)
{
    const char *v = getenv(n);
    return v && *v ? atoi(v) : def;
}

/* ================================================================ sound */
/* The devices, the threads, the buffer and the watchdog are lib/brickio's usbaudio; here only the
 * sound itself: 48 kHz stereo float in and out. */
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

/* the audio thread: stereo in (silence while there is none), stereo out */
static void process(const float *in, float *out, int frames, int have_input, void *ud)
{
    (void)ud, (void)have_input;
    static int ftz;
    static int16_t s16[4096 * 2]; /* a 16-bit copy for the LEDs' meter and the log's click check */
    if (!ftz)
        flush_denormals(), ftz = 1;
    if (frames > 4096)
        frames = 4096;
    uint64_t t0 = perf_audio_begin(frames);
    static double ph;
    static float amp, hz = 220.f;
    float want = atomic_load(&target_hz), b = atomic_load(&bend);
    for (int i = 0; i < frames; i++) {
        float a_target = want > 0.f ? 0.3f : 0.f;
        amp += (a_target - amp) * 0.002f; /* no clicks */
        if (want > 0.f)
            hz += (want - hz) * 0.01f;
        ph += hz * powf(2.f, b / 12.f) / 48000.0;
        if (ph >= 1.0)
            ph -= 1.0;
        float v = sinf((float)ph * 6.2831853f) * amp;
        for (int c = 0; c < 2; c++) {
            float o = in[i * 2 + c] + v; /* the input passes through */
            out[i * 2 + c] = o;
            s16[i * 2 + c] = (int16_t)((o > 1.f ? 1.f : o < -1.f ? -1.f : o) * 32767.f);
        }
    }
    ringleds_audio(s16, frames);
    perf_audio_output(s16, frames);
    perf_audio_end(t0);
}

static void audio_opened(int frames, const char *device) { perf_audio_open(48000, frames, device); }

/* ================================================================ MIDI */
/* the reader thread wakes the main loop (an SDL event) so a note sounds at once, not at the next frame */
static Uint32 midi_wake_type;
static atomic_int midi_wake_pending;
static void midi_wake(void)
{
    if (!midi_wake_type || atomic_exchange(&midi_wake_pending, 1))
        return;
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = midi_wake_type;
    SDL_PushEvent(&ev);
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
    perf_log("START", "%s on MustardOS, LEDs %s", pro ? "Brick Pro" : "Brick / Brick Hammer", leds ? "on" : "off");

    UaConfig ac = {.app = "SK", .process = process, .log = perf_log, .settings = "audio.txt", .opened = audio_opened};
    ua_start(&ac);
    midi_wake_type = SDL_RegisterEvents(1);
    if (midi_wake_type == (Uint32)-1)
        midi_wake_type = 0;
    midi_start(NULL, midi_wake);

    static const float notes[4] = {220.f, 277.18f, 329.63f, 440.f}; /* A B X Y */
    int held[BI_COUNT] = {0}, octave = 0, running = 1, midi_note = -1;
    float sx[2] = {0}, sy[2] = {0};
    while (running) {
        Uint32 t0 = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = 0;
            else if (e.type == SDL_AUDIODEVICEADDED || e.type == SDL_AUDIODEVICEREMOVED)
                ua_event(&e);
            else if (midi_wake_type && e.type == midi_wake_type)
                atomic_store(&midi_wake_pending, 0);
            else if (e.type == SDL_RENDER_TARGETS_RESET)
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
        /* USB MIDI: the last note held plays */
        MidiEv me[64];
        int nm = midi_poll(me, 64);
        for (int k = 0; k < nm; k++)
            if (me[k].type == MIDI_NOTE && me[k].val)
                midi_note = me[k].num;
            else if (me[k].type == MIDI_NOTE && me[k].num == midi_note)
                midi_note = -1;
        /* the sound's devices: the watchdog, the buffer, and what to tell the player */
        int ua_notes = ua_tick();
        for (int bit = 1; bit <= UA_NOTE_REOPEN; bit <<= 1)
            if (ua_notes & bit) {
                char nb[96];
                perf_log("AUDIO", "%s", ua_note_text(bit, nb, sizeof nb));
            }

        /* the sound */
        float hz = 0.f;
        for (int k = 0; k < 4; k++)
            if (held[BI_A + k])
                hz = notes[k] * powf(2.f, (float)octave);
        if (midi_note >= 0)
            hz = 440.f * powf(2.f, (midi_note - 69) / 12.f);
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

    midi_stop();
    ua_stop();
    ringleds_close();
    bz_close();
    bi_switch_close();
    perf_close();
    SDL_Quit();
    return 0;
}
