/* The Brick's (and Brick Hammer's) LEDs by named zone, through TrimUI's
 * led_anim driver (/sys/class/led_anim), as Columbia Street drives them:
 *
 *   BZ_F1, BZ_F2   the two lit buttons under the screen
 *   BZ_TOP         the top bar's 8 LEDs, one colour ("m")
 *   BZ_SHOULDERS   the 4 shoulder LEDs L1 L2 R2 R1, one colour ("lr")
 *
 * Each zone shows a steady colour (effect 4). Colours are written from a
 * thread of its own, at most every 25 ms and only when one changed, so the
 * audio and the screen never wait for the driver. The settings found at
 * start (colours, effects, brightness, effect_enable) are put back by
 * bz_close; until then <dir-of-app>/leds_saved.txt keeps them so a crash
 * doesn't lose them (the next bz_open restores from it).
 *
 * On the Brick Pro the named zones reach only the left stick's ring: use
 * ringleds.c (frame_hex) there.
 *
 * Settings (<P> is the prefix given to bz_open): <P>_LEDS=0 leaves the LEDs
 * alone; <P>_LED_BRIGHTNESS=1..60 (60 by default); <P>_LED_DIR another
 * folder (for tests). Nothing happens where the driver isn't. */
#pragma once
#include <stdint.h>

typedef enum { BZ_F1, BZ_F2, BZ_TOP, BZ_SHOULDERS, BZ_COUNT } BzZone;

int bz_open(const char *prefix);        /* 1 if the LEDs are driven */
void bz_set(BzZone z, uint32_t rgb);    /* 0xRRGGBB; 0 is dark. Any thread. */
void bz_close(void);                    /* dark, then the settings found at start back */
uint32_t bz_scale(uint32_t rgb, float k); /* a colour at brightness 0..1 */
