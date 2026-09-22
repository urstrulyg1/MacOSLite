/* maclite-brightness — backlight control (spec §5).
 *
 * Uses the same backend as System Settings > Displays (hardware/backlight.c).
 * A set is reported OK only after sysfs has been read back; on Apple iMacs the
 * ACPI _BCL interface is a documented no-op in EFI mode, so it is flagged
 * SUSPECT and this tool refuses to write to it — it will never claim a change
 * the panel did not make.
 */
#include "diag_common.h"

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [get|set N|up|down|list|status] [--step N] [--device NAME]\n"
        "  get            print current brightness as a percentage\n"
        "  set N          set brightness to N%% (0-100)\n"
        "  up | down      step by --step (default 10%%)\n"
        "  list           enumerate every backlight device and why it was (not) chosen\n"
        "  status         report the mechanism in use, no change attempted\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    const char *cmd = argc > 1 && argv[1][0] != '-' ? argv[1] : "status";
    const char *step_s = diag_opt(argc, argv, "--step");
    const char *dev = diag_opt(argc, argv, "--device");
    int step = step_s ? atoi(step_s) : 10;

    bl_state st;
    bl_probe(&st);

    if (!strcmp(cmd, "list")) {
        printf("MacLiteOS backlight devices\n\n");
        printf("  DMI: %s %s\n", st.dmi_vendor[0] ? st.dmi_vendor : "?", st.dmi_product);
        printf("  EFI boot: %s   acpi_backlight=%s\n", diag_yn(st.efi_boot),
               st.acpi_backlight_arg[0] ? st.acpi_backlight_arg : "(unset)");
        printf("  mechanism: %s\n\n", st.mechanism);
        if (!st.n) { printf("  (none)\n"); return HW_EXIT_UNSUPPORTED; }
        for (int i = 0; i < st.n; i++) {
            const bl_device *b = &st.dev[i];
            printf("  %-16s %-34s max=%-5ld cur=%-5ld actual=%-5ld %s%s\n",
                   b->name, bl_kind_name(b->kind), b->max, b->cur, b->actual,
                   b->suspect ? "SUSPECT " : "", i == st.chosen ? "<- in use" : "");
            if (b->note[0]) printf("      %s\n", b->note);
        }
        return st.chosen >= 0 && !st.dev[st.chosen].suspect ? HW_EXIT_PASS : HW_EXIT_UNSUPPORTED;
    }

    const bl_device *b = bl_active(&st);
    if (dev) b = bl_find(&st, dev);

    if (!strcmp(cmd, "status")) {
        hw_report rep;
        hw_report_init(&rep);
        diag_title("MacLiteOS Brightness");
        printf("  %-16s %s\n", "Mechanism:", st.mechanism);
        if (b) {
            int pct = -1;
            bool ok = bl_get_percent(&st, &pct);
            printf("  %-16s %s (max %ld)\n", "Device:", b->name, b->max);
            printf("  %-16s %s\n", "Path:", b->path);
            printf("  %-16s %s\n", "Writable:", diag_yn(b->writable));
            hw_report_add(&rep, "Brightness", "Control available", true,
                          (b->writable && !b->suspect) ? HW_PASS : (b->suspect ? HW_FAIL : HW_UNSUPPORTED),
                          "%s", b->suspect ? b->note : bl_kind_name(b->kind));
            hw_report_add(&rep, "Brightness", "Current value readable", true,
                          ok ? HW_PASS : HW_FAIL, ok ? "%d%%" : "brightness/max_brightness unreadable", pct);
            if (ok) printf("  %-16s %d%%\n", "Current:", pct);
            else printf("  %-16s unknown\n", "Current:");
        } else {
            hw_report_add(&rep, "Brightness", "Control available", true, HW_UNSUPPORTED,
                          "%s", st.mechanism);
            printf("  no backlight device on this host\n");
        }
        printf("\n");
        for (int i = 0; i < rep.n; i++)
            printf("  %-26s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
        return hw_report_exit(&rep);
    }

    if (!b) {
        fprintf(stderr, "no usable backlight device (%s)\n", st.mechanism);
        return HW_EXIT_UNSUPPORTED;
    }
    if (dev && st.chosen >= 0 && strcmp(dev, st.dev[st.chosen].name)) {
        fprintf(stderr, "note: --device %s is not the preferred device (%s); using it anyway\n",
                dev, st.dev[st.chosen].name);
    }

    int applied = -1;
    bl_set_result rc;
    if (!strcmp(cmd, "get")) {
        int pct = 0;
        if (!bl_get_percent(&st, &pct)) {
            fprintf(stderr, "cannot read brightness from %s\n", b->path);
            return HW_EXIT_FAIL;
        }
        printf("%d\n", pct);
        return HW_EXIT_PASS;
    } else if (!strcmp(cmd, "set")) {
        const char *v = argc > 2 && argv[2][0] != '-' ? argv[2] : NULL;
        if (!v) { usage(argv[0]); return HW_EXIT_USAGE; }
        int pct = atoi(v);
        /* the state struct points at the preferred device; honour --device */
        if (dev) {
            for (int i = 0; i < st.n; i++)
                if (!strcmp(st.dev[i].name, dev)) { st.chosen = i; break; }
        }
        rc = bl_set_percent(&st, pct, &applied);
    } else if (!strcmp(cmd, "up") || !strcmp(cmd, "down")) {
        rc = bl_step(&st, !strcmp(cmd, "up") ? step : -step, &applied);
    } else {
        usage(argv[0]);
        return HW_EXIT_USAGE;
    }

    const bl_device *after = &st.dev[st.chosen];
    if (bl_set_ok(rc)) {
        printf("%d%% (verified by read-back from %s)\n", applied, after->name);
        if (after->note[0]) printf("note: %s\n", after->note);
        return HW_EXIT_PASS;
    }
    fprintf(stderr, "brightness change %s\n", bl_set_result_str(rc));
    if (after->note[0]) fprintf(stderr, "detail: %s\n", after->note);
    fprintf(stderr, "mechanism: %s\n", st.mechanism);
    fprintf(stderr, "see docs/gpu.md — on iMac Mid-2010 boot with acpi_backlight=native\n");
    if (rc == BL_SET_NOT_TESTED) return HW_EXIT_NOT_TESTED;
    return rc == BL_SET_UNSUPPORTED || rc == BL_SET_NO_DEVICE ? HW_EXIT_UNSUPPORTED : HW_EXIT_FAIL;
}
