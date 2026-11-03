/* evdump: every key, switch and axis event from every /dev/input/event*
 * device, as the kernel sends them, with the device's name. No SDL needed.
 * Run it over SSH on the device and press each control in turn:
 *
 *   ./evdump            (Ctrl+C to stop)
 *
 * This shows the kernel's view (KEY_ / BTN_ / ABS_ / SW_ codes). Apps see the
 * SDL joystick numbers instead: use joydump for those. Under MustardOS the
 * muOS-Keys device appears here too, alongside the TrimUI gamepad it reads. */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAXDEV 32

int main(void)
{
    struct pollfd pf[MAXDEV];
    char names[MAXDEV][96], paths[MAXDEV][48];
    int n = 0;
    DIR *d = opendir("/dev/input");
    if (!d) {
        perror("/dev/input");
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d)) && n < MAXDEV) {
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(paths[n], sizeof paths[n], "/dev/input/%.30s", e->d_name);
        int fd = open(paths[n], O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        names[n][0] = 0;
        ioctl(fd, EVIOCGNAME(sizeof names[n]), names[n]);
        struct input_id id;
        memset(&id, 0, sizeof id);
        ioctl(fd, EVIOCGID, &id);
        printf("%s: \"%s\" bus %04x vendor %04x product %04x\n", paths[n], names[n], id.bustype, id.vendor,
               id.product);
        pf[n].fd = fd, pf[n].events = POLLIN;
        n++;
    }
    closedir(d);
    printf("-- press things (Ctrl+C to stop)\n");
    fflush(stdout);
    for (;;) {
        if (poll(pf, (nfds_t)n, -1) < 0)
            break;
        for (int i = 0; i < n; i++) {
            if (!(pf[i].revents & POLLIN))
                continue;
            struct input_event ev[16];
            ssize_t r = read(pf[i].fd, ev, sizeof ev);
            for (int k = 0; k < (int)(r / (ssize_t)sizeof ev[0]); k++) {
                const char *t = ev[k].type == EV_KEY ? "KEY" : ev[k].type == EV_ABS ? "ABS"
                              : ev[k].type == EV_SW  ? "SW"  : NULL;
                if (!t)
                    continue; /* EV_SYN, EV_MSC */
                printf("%-18s %-22.22s %-3s code %3d value %6d\n", paths[i], names[i], t, ev[k].code, ev[k].value);
            }
            fflush(stdout);
        }
    }
    return 0;
}
