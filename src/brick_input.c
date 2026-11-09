/* The TrimUI's buttons and sticks: see brick_input.h and docs/input.md. */
#define _GNU_SOURCE
#include "brick_input.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

const char *const bi_button_names[BI_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L1", "R1",
                                               "L2", "R2", "SELECT", "START", "MENU", "F1", "F2", "L3", "R3",
                                               "HOME"};
static const char *const pad_names[BI_PAD_KINDS] = {"MustardOS (Brick)", "MustardOS (Brick Pro)"};

/* Each pad's SDL button number for each BiButton (-1: not a button there).
 * The d-pad is hat 0; L2 / R2 are buttons. */
static const signed char map_muos_brick[BI_COUNT] = {
    /* UP DN LT RT  A  B  X  Y  L1 R1 L2  R2 SEL STA MENU F1  F2  L3  R3  HOME
     * F1 / F2 are what MustardOS calls L3 / R3 (the Brick has no sticks to click) */
    -1, -1, -1, -1, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1, -1, -1};
static const signed char map_muos_brickpro[BI_COUNT] = {
    /* F1 / F2 come first as 0 / 1; 2 / 3 are volume down / up (left to MustardOS);
     * 4 is HOME; 16 / 17 the stick clicks */
    -1, -1, -1, -1, 6, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 16, 17, 4};

/* what each pad's axes are: the Brick Pro's sticks */
enum { AX_NONE, AX_LX, AX_LY, AX_RX, AX_RY };
#define NAXES 4
static const unsigned char pad_axes[BI_PAD_KINDS][NAXES] = {
    [BI_PAD_MUOS_BRICK] = {AX_NONE},
    [BI_PAD_MUOS_BRICKPRO] = {AX_LX, AX_LY, AX_RX, AX_RY},
};

#define NBTN 32
static int joy_map[BI_PAD_KINDS][NBTN]; /* SDL button -> BiButton */

#define MAX_PADS 8
static struct {
    SDL_JoystickID id;
    int kind;
} pads[MAX_PADS];
static int npads;

static int hat_state = -1;
static float stick_v[2][2];

static int read_line(const char *path, char *buf, int n)
{
    FILE *f = fopen(path, "r");
    buf[0] = 0;
    if (!f)
        return 0;
    if (!fgets(buf, n, f))
        buf[0] = 0;
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf[0] != 0;
}

/* the device as MustardOS names its board, without a controller: 1 Brick Pro, 0 not, -1 unknown */
static int board_says_brickpro(void)
{
    char b[64];
    if (read_line("/opt/muos/device/config/board/name", b, sizeof b))
        return strstr(b, "pro") != NULL;
    return -1;
}

void bi_init(const char *prefix)
{
    const signed char *src[BI_PAD_KINDS] = {map_muos_brick, map_muos_brickpro};
    for (int p = 0; p < BI_PAD_KINDS; p++) {
        for (int i = 0; i < NBTN; i++)
            joy_map[p][i] = BI_NONE;
        for (int b = 0; b < BI_COUNT; b++)
            if (src[p][b] >= 0)
                joy_map[p][(int)src[p][b]] = b;
    }
    /* <prefix>_JOYMAP=A=1,B=0,...: other button numbers, for every pad */
    char name[64];
    snprintf(name, sizeof name, "%s_JOYMAP", prefix ? prefix : "APP");
    const char *m = getenv(name);
    while (m && *m) {
        char key[16];
        int num;
        if (sscanf(m, "%15[^=]=%d", key, &num) == 2 && num >= 0 && num < NBTN)
            for (int b = 0; b < BI_COUNT; b++)
                if (!strcasecmp(key, bi_button_names[b]))
                    for (int p = 0; p < BI_PAD_KINDS; p++) {
                        for (int i = 0; i < NBTN; i++)
                            if (joy_map[p][i] == b)
                                joy_map[p][i] = BI_NONE;
                        joy_map[p][num] = b;
                    }
        m = strchr(m, ',');
        if (m)
            m++;
    }
}

/* vendor and product from the GUID (Linux: bytes 4-5 and 8-9, little-endian):
 * every SDL2 has this, unlike SDL_JoystickGetVendor */
static int guid_u16(SDL_Joystick *j, int at)
{
    SDL_JoystickGUID g = SDL_JoystickGetGUID(j);
    return g.data[at] | g.data[at + 1] << 8;
}

/* muOS-Keys (vendor 0x756d, "mu"; product 2 Brick, 3 Brick Pro), or -1 for any other controller */
static int kind_of(SDL_Joystick *j)
{
    const char *name = SDL_JoystickName(j);
    if (!(name && strcasestr(name, "muos")) && guid_u16(j, 4) != 0x756d)
        return -1;
    switch (guid_u16(j, 8)) {
    case 2: return BI_PAD_MUOS_BRICK;
    case 3: return BI_PAD_MUOS_BRICKPRO;
    }
    int pro = board_says_brickpro();
    if (pro >= 0)
        return pro ? BI_PAD_MUOS_BRICKPRO : BI_PAD_MUOS_BRICK;
    return SDL_JoystickNumButtons(j) > 17 ? BI_PAD_MUOS_BRICKPRO : BI_PAD_MUOS_BRICK;
}

void bi_open_joysticks(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j)
            continue;
        SDL_JoystickID id = SDL_JoystickInstanceID(j);
        int known = 0;
        for (int k = 0; k < npads; k++)
            known |= pads[k].id == id;
        if (known) { /* already open: drop the extra reference */
            SDL_JoystickClose(j);
            continue;
        }
        int kind = kind_of(j);
        if (npads < MAX_PADS)
            pads[npads].id = id, pads[npads].kind = kind, npads++;
        fprintf(stderr, "controller %d: %s (%04x:%04x), %d buttons, %d axes, %d hats: %s\n", i,
                SDL_JoystickName(j), guid_u16(j, 4), guid_u16(j, 8), SDL_JoystickNumButtons(j),
                SDL_JoystickNumAxes(j), SDL_JoystickNumHats(j), kind >= 0 ? pad_names[kind] : "not read");
    }
}

/* the map an event uses, or -1 to ignore it (not a muOS-Keys controller) */
static int event_pad(SDL_JoystickID id)
{
    for (int i = 0; i < npads; i++)
        if (pads[i].id == id)
            return pads[i].kind;
    return -1;
}

int bi_pad_kind(void)
{
    for (int i = 0; i < npads; i++)
        if (pads[i].kind >= 0)
            return pads[i].kind;
    return -1;
}

int bi_is_brickpro(void)
{
    for (int i = 0; i < npads; i++)
        if (pads[i].kind == BI_PAD_MUOS_BRICKPRO)
            return 1;
    return board_says_brickpro() == 1;
}

/* -1..1 with the centre's dead zone removed */
static float stick_value(int v)
{
    float x = v / 32767.0f, a = fabsf(x);
    const float dz = 0.15f;
    if (a < dz)
        return 0.0f;
    a = (a - dz) / (1.0f - dz);
    return x < 0 ? -fminf(a, 1.0f) : fminf(a, 1.0f);
}

static int push(BiEvent *out, int n, int max, BiEvent e)
{
    if (n < max)
        out[n++] = e;
    return n;
}

int bi_event(const SDL_Event *e, BiEvent *out, int max)
{
    int n = 0;
    switch (e->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int pad = event_pad(e->jbutton.which);
        int b = e->jbutton.button;
        if (pad < 0 || b >= NBTN || joy_map[pad][b] == BI_NONE)
            break;
        n = push(out, n, max, (BiEvent){.type = BI_EV_BUTTON, .button = joy_map[pad][b],
                                         .down = e->type == SDL_JOYBUTTONDOWN});
        break;
    }
    case SDL_JOYHATMOTION: {
        if (e->jhat.hat != 0 || event_pad(e->jhat.which) < 0)
            break;
        static const int bit[4] = {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT};
        int was = hat_state < 0 ? 0 : hat_state, now = e->jhat.value;
        for (int i = 0; i < 4; i++)
            if ((was & bit[i]) != (now & bit[i]))
                n = push(out, n, max, (BiEvent){.type = BI_EV_BUTTON, .button = BI_UP + i,
                                                 .down = (now & bit[i]) != 0});
        hat_state = now;
        break;
    }
    case SDL_JOYAXISMOTION: {
        int pad = event_pad(e->jaxis.which);
        if (pad < 0 || e->jaxis.axis >= NAXES)
            break;
        int role = pad_axes[pad][e->jaxis.axis], v = e->jaxis.value;
        if (role >= AX_LX) {
            int s = role >= AX_RX, yax = role == AX_LY || role == AX_RY;
            float val = stick_value(v);
            if (yax)
                val = -val; /* the axis is negative up: make up positive */
            if (val != stick_v[s][yax]) {
                stick_v[s][yax] = val;
                n = push(out, n, max, (BiEvent){.type = BI_EV_STICK, .stick = s, .x = stick_v[s][0], .y = stick_v[s][1]});
            }
        }
        break;
    }
    case SDL_JOYDEVICEADDED:
        bi_open_joysticks();
        break;
    case SDL_JOYDEVICEREMOVED:
        for (int i = 0; i < npads; i++)
            if (pads[i].id == e->jdevice.which) {
                pads[i] = pads[--npads];
                break;
            }
        break;
    }
    return n;
}

/* ---------------------------------------------------------------- the side switch */
#define SW_GPIO "243"
#define SW_DIR "/sys/class/gpio/gpio" SW_GPIO
static int sw_exported_here;

static int write_sys(const char *path, const char *v)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return 0;
    int ok = write(fd, v, strlen(v)) == (ssize_t)strlen(v);
    close(fd);
    return ok;
}

int bi_switch_read(void)
{
    char b[8];
    if (!read_line(SW_DIR "/value", b, sizeof b)) {
        if (sw_exported_here || !write_sys("/sys/class/gpio/export", SW_GPIO))
            return -1;
        sw_exported_here = 1;
        write_sys(SW_DIR "/direction", "in");
        if (!read_line(SW_DIR "/value", b, sizeof b))
            return -1;
    }
    return b[0] == '1';
}

void bi_switch_close(void)
{
    if (sw_exported_here)
        write_sys("/sys/class/gpio/unexport", SW_GPIO);
    sw_exported_here = 0;
}
