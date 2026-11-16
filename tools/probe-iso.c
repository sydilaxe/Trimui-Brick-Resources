/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Can the Brick's bottom port, through FunctionFS, be a standard USB audio device? That needs
 * isochronous endpoints (audio on a fixed timetable), which a kernel and a USB controller may or may
 * not give FunctionFS. This sets up a test gadget offering one isochronous endpoint each way, binds it
 * to the port for a moment (as the apps' clock link does at every start; with nothing plugged into the
 * bottom port, nothing else happens), reports each step, and takes everything down again.
 *
 * Run by tools/probe-gadget.sh (as root, no app running). Built for the Brick with zig:
 *   zig cc -target aarch64-linux-gnu.2.28 -O2 -o probe-iso probe-iso.c */
#define _GNU_SOURCE
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define G "/sys/kernel/config/usb_gadget/brickioiso"
#define FFS_DIR "/tmp/brickioiso-ffs"

static int put(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return 0;
    int ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    close(fd);
    return ok;
}

/* in the clock link's order (link.c's teardown, proven on the Bricks): let go of the port, unmount,
 * then unlink and remove (ep0 is closed before this) */
static void teardown(void)
{
    put(G "/UDC", "\n");
    umount(FFS_DIR);
    rmdir(FFS_DIR);
    unlink(G "/configs/c.1/ffs.brickioiso");
    rmdir(G "/configs/c.1/strings/0x409");
    rmdir(G "/configs/c.1");
    rmdir(G "/functions/ffs.brickioiso");
    rmdir(G "/strings/0x409");
    rmdir(G);
}

/* a variant to try: which endpoints, what kind, how big */
typedef struct {
    const char *what;
    int out, in;  /* an endpoint that way (1) or not (0) */
    int iso;      /* 1 isochronous, 0 bulk (the control: the clock link's kind) */
    int size;     /* bytes a packet (isochronous: a millisecond's) */
} Variant;

static const Variant VARIANTS[] = {
    {"control: bulk, one each way (the clock link's kind)", 1, 1, 0, 512},
    {"isochronous out only, 196 bytes (48 kHz stereo 16-bit)", 1, 0, 1, 196},
    {"isochronous in only, 196 bytes", 0, 1, 1, 196},
    {"isochronous out only, 294 bytes (48 kHz stereo 24-bit)", 1, 0, 1, 294},
    {"isochronous both ways, 196 bytes each", 1, 1, 1, 196},
    {"isochronous out only, 64 bytes (a small one)", 1, 0, 1, 64},
};

/* one interface with the variant's endpoints; full speed (1 ms) and high speed (bInterval 4: 1 ms) */
static int descriptors(int ep0, const Variant *v)
{
    uint8_t buf[256];
    int n = 0, neps = v->out + v->in;
    struct usb_functionfs_descs_head_v2 head;
    n = sizeof head + 8; /* the head, then fs_count and hs_count */
    for (int k = 0; k < 2; k++) { /* full speed, high speed */
        struct usb_interface_descriptor intf;
        memset(&intf, 0, sizeof intf);
        intf.bLength = USB_DT_INTERFACE_SIZE;
        intf.bDescriptorType = USB_DT_INTERFACE;
        intf.bNumEndpoints = (uint8_t)neps;
        intf.bInterfaceClass = USB_CLASS_VENDOR_SPEC;
        intf.iInterface = 1;
        memcpy(buf + n, &intf, USB_DT_INTERFACE_SIZE), n += USB_DT_INTERFACE_SIZE;
        for (int dir = 0; dir < 2; dir++) {
            if (dir == 0 ? !v->out : !v->in) continue;
            struct usb_endpoint_descriptor_no_audio ep;
            memset(&ep, 0, sizeof ep);
            ep.bLength = USB_DT_ENDPOINT_SIZE;
            ep.bDescriptorType = USB_DT_ENDPOINT;
            ep.bEndpointAddress = (uint8_t)(dir == 0 ? 1 | USB_DIR_OUT : 2 | USB_DIR_IN);
            ep.bmAttributes = v->iso ? USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC : USB_ENDPOINT_XFER_BULK;
            ep.wMaxPacketSize = htole16((uint16_t)(v->iso ? v->size : k ? 512 : 64));
            ep.bInterval = (uint8_t)(v->iso ? (k ? 4 : 1) : 0);
            memcpy(buf + n, &ep, USB_DT_ENDPOINT_SIZE), n += USB_DT_ENDPOINT_SIZE;
        }
    }
    memset(&head, 0, sizeof head);
    head.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    head.flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC);
    head.length = htole32((uint32_t)n);
    memcpy(buf, &head, sizeof head);
    __le32 count = htole32((uint32_t)(1 + neps));
    memcpy(buf + sizeof head, &count, 4), memcpy(buf + sizeof head + 4, &count, 4);
    if (write(ep0, buf, (size_t)n) != (ssize_t)n) return -errno;
    struct {
        struct usb_functionfs_strings_head head;
        struct {
            __le16 code;
            char str[17]; /* "brickio iso test" and its NUL */
        } __attribute__((packed)) lang;
    } __attribute__((packed)) str;
    memset(&str, 0, sizeof str);
    str.head.magic = htole32(FUNCTIONFS_STRINGS_MAGIC);
    str.head.length = htole32(sizeof str);
    str.head.str_count = htole32(1);
    str.head.lang_count = htole32(1);
    str.lang.code = htole16(0x0409);
    memcpy(str.lang.str, "brickio iso test", 17);
    if (write(ep0, &str, sizeof str) != (ssize_t)sizeof str) return -errno;
    return 0;
}

/* one variant: set up, bind for a moment, take down (in the clock link's order). 1 bound, 0 not, -1 a
 * setup problem */
static int try_variant(const char *udc, const Variant *v)
{
    printf("iso: %s\n", v->what);
    teardown();
    int ok = mkdir(G, 0755) == 0 && put(G "/idVendor", "0x1d6b") && put(G "/idProduct", "0x0105") && mkdir(G "/strings/0x409", 0755) == 0 &&
             put(G "/strings/0x409/product", "brickio iso test") && mkdir(G "/configs/c.1", 0755) == 0 &&
             mkdir(G "/functions/ffs.brickioiso", 0755) == 0 && mkdir(FFS_DIR, 0755) == 0 &&
             mount("brickioiso", FFS_DIR, "functionfs", 0, NULL) == 0;
    if (!ok) { printf("     couldn't set up the test gadget (%s)\n", strerror(errno)); teardown(); return -1; }
    int ep0 = open(FFS_DIR "/ep0", O_RDWR);
    int r = ep0 < 0 ? -errno : descriptors(ep0, v);
    if (r < 0) {
        printf("     FunctionFS refused it (%s)\n", strerror(-r));
        if (ep0 >= 0) close(ep0);
        teardown();
        return 0;
    }
    int bound = 0;
    if (symlink(G "/functions/ffs.brickioiso", G "/configs/c.1/ffs.brickioiso") != 0)
        printf("     couldn't link the function (%s)\n", strerror(errno));
    else {
        char line[310];
        snprintf(line, sizeof line, "%s\n", udc);
        bound = put(G "/UDC", line);
        if (bound) printf("     BOUND\n"), usleep(200000);
        else printf("     not bound (%s)\n", strerror(errno));
    }
    put(G "/UDC", "\n");
    close(ep0);
    teardown();
    return bound;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* each line out at once: a step that hangs still shows */
    char udc[300] = "";
    DIR *d = opendir("/sys/class/udc");
    struct dirent *e;
    while (d && (e = readdir(d)))
        if (e->d_name[0] != '.') snprintf(udc, sizeof udc, "%s", e->d_name);
    if (d) closedir(d);
    if (!*udc) { printf("iso: no USB device controller\n"); return 0; }
    DIR *g = opendir("/sys/kernel/config/usb_gadget");
    while (g && (e = readdir(g)))
        if (e->d_name[0] != '.') {
            char p[300], v[64] = "";
            snprintf(p, sizeof p, "/sys/kernel/config/usb_gadget/%s/UDC", e->d_name);
            int fd = open(p, O_RDONLY);
            if (fd >= 0) { ssize_t r = read(fd, v, sizeof v - 1); close(fd); if (r > 1) { printf("iso: the port is in use (gadget %s): run with no app open\n", e->d_name); closedir(g); return 0; } }
        }
    if (g) closedir(g);
    int control = 0, any = 0;
    for (unsigned i = 0; i < sizeof VARIANTS / sizeof VARIANTS[0]; i++) {
        int r = try_variant(udc, &VARIANTS[i]);
        if (i == 0) control = r == 1;
        else if (r == 1) any = 1;
    }
    if (!control) printf("iso: the control didn't bind either: the test itself can't bind here, so this says nothing about isochronous\n");
    else if (any) printf("iso: AVAILABLE in some form (see BOUND above): a receiving Brick can be a standard USB audio device that way\n");
    else printf("iso: NOT AVAILABLE: bulk binds, isochronous doesn't: brickio's own channel (bulk, as the clock link)\n");
    printf("iso: test gadgets removed\n");
    return 0;
}
