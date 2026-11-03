/* The TrimUI Brick's, Brick Hammer's and Brick Pro's buttons and sticks, on
 * TrimUI OS or MustardOS, through SDL2's raw joystick API. One binary serves
 * all four: the controller is identified at run time. Taken from the input
 * code of the sydilaxe TrimUI apps (RadioScan, Columbia Street, HI5,
 * Jaffedrone); see docs/input.md for the tables and where they come from.
 *
 * Use:
 *   SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
 *   SDL_Init(... | SDL_INIT_JOYSTICK);
 *   bi_init("MYAPP");              // reads MYAPP_JOYMAP=A=1,B=0,...
 *   bi_open_joysticks();           // and again on SDL_JOYDEVICEADDED
 *   each SDL event: BiEvent out[8]; int n = bi_event(&e, out, 8);
 *   each out[i] is a button press/release (BiButton) or a stick move
 *
 * Why raw joystick and not SDL_GameController: MustardOS swaps A/B and X/Y in
 * its GameController mapping with the user's layout setting. The raw numbers
 * stay put, so buttons stay as labelled. */
#pragma once
#include <SDL2/SDL.h>

typedef enum {
    BI_UP, BI_DOWN, BI_LEFT, BI_RIGHT,
    BI_A, BI_B, BI_X, BI_Y,
    BI_L1, BI_R1, BI_L2, BI_R2,
    BI_SELECT, BI_START, BI_MENU,
    BI_F1, BI_F2,          /* the two lit buttons under the screen */
    BI_L3, BI_R3,          /* the Brick Pro's stick clicks */
    BI_HOME,               /* the Brick Pro's HOME key (MustardOS: button 4) */
    BI_COUNT,
    BI_NONE = -1
} BiButton;

typedef enum {
    BI_PAD_TRIMUI,          /* TrimUI OS's own gamepad (either device) */
    BI_PAD_MUOS_BRICK,      /* MustardOS muOS-Keys, product 2: Brick, Brick Hammer */
    BI_PAD_MUOS_BRICKPRO,   /* MustardOS muOS-Keys, product 3: Brick Pro */
    BI_PAD_KINDS
} BiPad;

typedef enum { BI_EV_BUTTON, BI_EV_STICK } BiEventType;

typedef struct {
    BiEventType type;
    BiButton button; /* BI_EV_BUTTON */
    int down;        /* BI_EV_BUTTON: 1 pressed, 0 released */
    int stick;       /* BI_EV_STICK: 0 left, 1 right */
    float x, y;      /* BI_EV_STICK: -1..1, x right, y UP, dead zone removed */
} BiEvent;

extern const char *const bi_button_names[BI_COUNT];

/* prefix: the app's env prefix, for <prefix>_JOYMAP; may be NULL */
void bi_init(const char *prefix);
/* opens every joystick not yet open; logs each to stderr */
void bi_open_joysticks(void);
/* turns one SDL event into up to max button/stick events; returns how many.
 * Handles SDL_JOYDEVICEREMOVED itself. Ignores the TrimUI gamepad while a
 * muOS-Keys controller is open (else every press would arrive twice). */
int bi_event(const SDL_Event *e, BiEvent *out, int max);
/* 1 if this is a Brick Pro: from the controller, else MustardOS's board
 * name, else TrimUI OS's DEVICE / TRIMUI_MODEL */
int bi_is_brickpro(void);
/* 1 if running under MustardOS (/opt/muos exists) */
int bi_is_mustardos(void);
/* the kind of the first open controller that is read, or -1 */
int bi_pad_kind(void);
/* the side switch (FN switch, GPIO 243): 0 up, 1 down, -1 unreadable.
 * Exports the GPIO if nobody has; bi_switch_close unexports it again. */
int bi_switch_read(void);
void bi_switch_close(void);
