/* Host test of the LED drivers against a stand-in /sys/class/led_anim folder:
 * what they write, and that they put the settings found at start back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "brick_zones.h"
#include "ringleds.h"

static const char *dir;
static void rd(const char *f, char *b, int n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, f);
    FILE *fp = fopen(p, "r");
    b[0] = 0;
    if (fp) { if (!fgets(b, n, fp)) b[0] = 0; fclose(fp); }
    b[strcspn(b, "\n")] = 0;
}
static int fails;
static void expect(const char *f, const char *want)
{
    char b[256];
    rd(f, b, sizeof b);
    int ok = !strcmp(b, want);
    printf("%s %-20s = \"%s\"%s%s%s\n", ok ? "ok  " : "FAIL", f, b, ok ? "" : " (want \"", ok ? "" : want, ok ? "" : "\")");
    fails += !ok;
}
int main(int argc, char **argv)
{
    (void)argc;
    dir = argv[1];
    setenv("T_LED_DIR", dir, 1);
    /* the Brick's named zones */
    if (!bz_open("T")) { printf("FAIL bz_open\n"); return 1; }
    expect("max_scale", "60");
    expect("effect_enable", "1");
    bz_set(BZ_F1, 0xff0000);
    bz_set(BZ_TOP, 0x00ff00);
    usleep(80000);
    expect("effect_rgb_hex_f1", "FF0000 ");
    expect("effect_f1", "4");
    expect("effect_rgb_hex_m", "00FF00 ");
    expect("effect_rgb_hex_f2", "000000 ");
    bz_close();
    expect("effect_rgb_hex_f1", "123456 ");
    expect("effect_f1", "2");
    expect("max_scale", "0");
    expect("effect_enable", "1");
    if (access("leds_saved.txt", F_OK) == 0) { printf("FAIL leds_saved.txt left behind\n"); fails++; }
    /* the Brick Pro's frame */
    if (!ringleds_open("T", 1)) { printf("FAIL ringleds_open\n"); return 1; }
    expect("effect_enable", "0");
    expect("max_scale", "60");
    ringleds_update(-1.f, 0.f, 0x0000ff, 0.f, 0.f, 0, 0, 0, 120.f, 0xff0000, 0x00ff00);
    char b[256];
    rd("frame_hex", b, sizeof b);
    printf("     frame_hex = \"%s\"\n", b);
    /* F2 is the 9th, F1 the 10th, the left ring's left corners 11 and 12 */
    const char *want = "000000 000000 000000 000000 000000 000000 000000 000000 00FF00 FF0000 0000FF 0000FF 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 ";
    if (strcmp(b, want)) { printf("FAIL frame_hex\n     want      \"%s\"\n", want); fails++; } else printf("ok   frame_hex layout\n");
    ringleds_close();
    expect("effect_enable", "1");
    expect("max_scale", "0");
    expect("effect_rgb_hex_f1", "123456 ");
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
