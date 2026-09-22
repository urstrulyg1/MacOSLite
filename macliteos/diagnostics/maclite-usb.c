/* maclite-usb — USB enumeration and hotplug verification (spec §11).
 *
 * Reads the kernel's own device tree; no udev daemon, no polling. Hotplug is
 * event driven through a NETLINK_KOBJECT_UEVENT socket in
 * scripts/maclite-automount (started by init only when a session is live).
 */
#include "diag_common.h"
#include <sys/inotify.h>
#include <errno.h>

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--watch SECONDS]\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    const char *watch = diag_opt(argc, argv, "--watch");

    mica_usb_state u;
    mica_usb_probe(&u);
    mica_input_state in;
    mica_input_probe(&in);

    diag_title("MacLiteOS USB");
    hw_report rep;
    hw_report_init(&rep);

    {
        char bus_txt[192];
        if (u.present) snprintf(bus_txt, sizeof bus_txt, "%d device(s) under %s", u.n, hw_sys("bus/usb/devices"));
        else snprintf(bus_txt, sizeof bus_txt, "%s", u.note);
        hw_report_add(&rep, "USB", "Bus present", true, u.present ? HW_PASS : HW_UNSUPPORTED, "%s", bus_txt);
    }
    diag_section("Devices");
    if (!u.n) printf("  (none)\n");
    for (int i = 0; i < u.n; i++) {
        const mica_usb_dev *d = &u.v[i];
        printf("  %-10s %04x:%04x %-10s %-24s %s\n", d->port, d->vid, d->pid, d->kind,
               d->product[0] ? d->product : "-", d->driver[0] ? d->driver : "-");
        if (d->blockdev[0]) printf("             block device: /dev/%s%s\n", d->blockdev,
                                   d->mountpoint[0] ? "" : " (not mounted)");
    }
    putchar('\n');
    diag_section("HID");
    for (int i = 0; i < in.n; i++) printf("  %s\n", in.names[i]);
    if (!in.n) printf("  (no input devices readable — %s/bus/input/devices %s)\n",
                      hw_proc_root(), in.present ? "has no entries" : "is absent");
    putchar('\n');

    hw_report_add(&rep, "USB", "Keyboard", true,
                  in.keyboard ? HW_PASS : (u.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d keyboard device(s) bound to kbd handlers", in.nkey);
    hw_report_add(&rep, "USB", "Mouse", true,
                  in.mouse ? HW_PASS : (u.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d pointer device(s) bound to mouse handlers", in.nmouse);
    hw_report_add(&rep, "USB", "Media keys", false,
                  in.media_keys ? HW_PASS : HW_NOT_TESTED,
                  "%s", in.media_keys ? "a device advertises extended key bits (volume/brightness)"
                                       : "no extended key capability seen");
    hw_report_add(&rep, "USB", "Storage", false,
                  u.nstorage ? HW_PASS : HW_UNSUPPORTED, "%d mass-storage device(s)", u.nstorage);
    hw_report_add(&rep, "USB", "Hubs", false,
                  u.nhub ? HW_PASS : HW_UNSUPPORTED, "%d hub(s)", u.nhub);
    hw_report_add(&rep, "USB", "Hotplug", false, HW_NOT_TESTED,
                  "plug a device and watch: maclite-usb --watch 10 (uevent driven, no polling)");

    for (int i = 0; i < rep.n; i++)
        printf("  %-14s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));

    if (watch) {
        int secs = atoi(watch);
        printf("\nwatching %s for %d s (events arrive on the kernel uevent socket)...\n",
               hw_sys("bus/usb/devices"), secs);
        /* inotify on the sysfs directory: kernel-driven, no polling loop */
        int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd < 0) { printf("  inotify unavailable: %s\n", strerror(errno)); return hw_report_exit(&rep); }
        int wd = inotify_add_watch(fd, hw_sys("bus/usb/devices"), IN_CREATE | IN_DELETE);
        if (wd < 0) { printf("  cannot watch %s: %s\n", hw_sys("bus/usb/devices"), strerror(errno)); close(fd); return hw_report_exit(&rep); }
        uint64_t end = ml_wall_ms() + (uint64_t)secs * 1000;
        int events = 0;
        while (ml_wall_ms() < end) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(fd, &rf);
            struct timeval tv = { 1, 0 };
            if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
            char buf[4096];
            ssize_t n = read(fd, buf, sizeof buf);
            for (char *p = buf; p < buf + n;) {
                struct inotify_event *e = (struct inotify_event *)p;
                if (e->len) printf("  %s %s\n", (e->mask & IN_CREATE) ? "added  " : "removed", e->name);
                events++;
                p += sizeof(struct inotify_event) + e->len;
            }
        }
        printf("  %d hotplug event(s) in %d s\n", events, secs);
        close(fd);
    }
    (void)argc;
    return hw_report_exit(&rep);
}
