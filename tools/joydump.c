/* joydump: every SDL2 joystick event, as an app sees them, plus how
 * src/brick_input.c reads each. Run it on the device over SSH (stop the
 * firmware's frontend first only if it grabs the input):
 *
 *   ./joydump [seconds]     (default 60; Ctrl+C to stop sooner)
 *
 * It lists each controller (name, vendor:product from the GUID, buttons,
 * axes, hats), then prints each button, hat and axis event with the raw
 * number and the BiButton it maps to. Axis events are printed only when they
 * move by more than 1/8 of full scale, to keep the output readable.
 * Under MustardOS run it after sourcing the app environment so SDL finds the
 * same libraries an app gets:
 *   . /opt/muos/script/var/retro.sh; SETUP_SDL_ENVIRONMENT; ./joydump */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "brick_input.h"

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    bi_init("JD");
    bi_open_joysticks(); /* prints each controller to stderr */
    printf("brick pro: %d, MustardOS: %d, pad kind read: %d\n", bi_is_brickpro(), bi_is_mustardos(), bi_pad_kind());
    printf("side switch (GPIO 243): %d\n-- press things\n", bi_switch_read());
    fflush(stdout);
    Uint32 end = SDL_GetTicks() + (Uint32)secs * 1000;
    int last_axis[8][8] = {{0}};
    int sw = bi_switch_read();
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                printf("joy %d button %2d %s", (int)e.jbutton.which, e.jbutton.button,
                       e.type == SDL_JOYBUTTONDOWN ? "down" : "up  ");
                break;
            case SDL_JOYHATMOTION:
                printf("joy %d hat %d value %d", (int)e.jhat.which, e.jhat.hat, e.jhat.value);
                break;
            case SDL_JOYAXISMOTION: {
                int j = e.jaxis.which & 7, a = e.jaxis.axis & 7;
                if (abs(e.jaxis.value - last_axis[j][a]) < 4096)
                    goto skip_print;
                last_axis[j][a] = e.jaxis.value;
                printf("joy %d axis %d value %6d", (int)e.jaxis.which, e.jaxis.axis, e.jaxis.value);
                break;
            }
            default:
                goto skip_print;
            }
            {
                BiEvent ev[8];
                int n = bi_event(&e, ev, 8);
                if (!n)
                    printf("   -> (not read)");
                for (int i = 0; i < n; i++) {
                    if (ev[i].type == BI_EV_BUTTON)
                        printf("   -> %s %s", bi_button_names[ev[i].button], ev[i].down ? "down" : "up");
                    else
                        printf("   -> %s stick x %.2f y %.2f", ev[i].stick ? "right" : "left", ev[i].x, ev[i].y);
                }
                printf("\n");
                fflush(stdout);
            }
            continue;
        skip_print:
            bi_event(&e, NULL, 0); /* device added / removed */
        }
        int s = bi_switch_read();
        if (s != sw) {
            printf("side switch: %d\n", s);
            fflush(stdout);
            sw = s;
        }
        SDL_Delay(5);
    }
    bi_switch_close();
    SDL_Quit();
    return 0;
}
