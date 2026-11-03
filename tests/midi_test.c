/* Host test of brick_midi's parser: bytes in as a port would deliver them, messages out. */
#include "../src/brick_midi.c"

static char got[512];
static void cb(const uint8_t *m, int len, uint64_t t, void *u)
{
    (void)t, (void)u;
    char b[32] = "";
    for (int i = 0; i < len; i++) snprintf(b + strlen(b), sizeof b - strlen(b), "%02X", m[i]);
    strcat(got, b);
    strcat(got, " ");
}

int main(void)
{
    Port p;
    memset(&p, 0, sizeof p);
    cb_ = cb;
    /* note on, running status with a clock in the middle, sysex skipped, program change, song position, stop */
    const uint8_t in[] = {0x90, 0x3C, 0x64, 0x3E, 0xF8, 0x50, 0xF0, 0x7E, 0x01, 0xF7, 0xC1, 0x05, 0xF2, 0x10, 0x00, 0xFC};
    feed(&p, in, sizeof in, 0);
    const char *want = "903C64 F8 903E50 C105 F21000 FC ";
    printf("got  %s\nwant %s\n%s\n", got, want, strcmp(got, want) ? "FAILED" : "all passed");
    return strcmp(got, want) != 0;
}
