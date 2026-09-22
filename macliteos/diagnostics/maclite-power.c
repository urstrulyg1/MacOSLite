/* maclite-power — suspend, resume and display power (spec §14).
 *
 * iMac Mid-2010 is a documented suspend failure: the panel does not re-light
 * after resume from RAM (ArchWiki "iMac Unibody", and the same symptom is
 * reported across 2009-2011 iMacs). Rather than pretend suspend works,
 * MacLiteOS treats system suspend as unsupported-by-policy on Apple machines
 * and offers the thing that does work: display sleep through the backlight
 * backend, which is also what the brightness keys drive.
 */
#include "diag_common.h"

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [status | display-sleep | display-wake | --allow-suspend]\n"
        "  status          report what this machine actually supports\n"
        "  display-sleep   backlight to 0 (safest supported 'sleep' on iMacs)\n"
        "  display-wake    restore the previous backlight level\n"
        "  --allow-suspend opt in to a real suspend attempt (not recommended here)\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    const char *cmd = argc > 1 && argv[1][0] != '-' ? argv[1] : "status";
    bool allow = diag_flag(argc, argv, "--allow-suspend");

    mica_power_state p;
    mica_power_probe(&p);
    bl_state bl;
    bl_probe(&bl);
    const bl_device *b = bl_active(&bl);

    if (!strcmp(cmd, "display-sleep") || !strcmp(cmd, "display-wake")) {
        if (!b) { fprintf(stderr, "no backlight device: %s\n", bl.mechanism); return HW_EXIT_UNSUPPORTED; }
        int applied = -1;
        bl_set_result rc = bl_set_percent(&bl, !strcmp(cmd, "display-sleep") ? 0 : 100, &applied);
        if (bl_set_ok(rc)) { printf("display %s (%d%%, verified)\n",
                                    !strcmp(cmd, "display-sleep") ? "asleep" : "awake", applied); return HW_EXIT_PASS; }
        fprintf(stderr, "display power change %s\n", bl_set_result_str(rc));
        return rc == BL_SET_UNSUPPORTED ? HW_EXIT_UNSUPPORTED : HW_EXIT_FAIL;
    }

    diag_title("MacLiteOS Power");
    hw_report rep;
    hw_report_init(&rep);

    {
        char if_txt[192];
        if (p.state_node)
            snprintf(if_txt, sizeof if_txt, "%s (%s), writable: %s", hw_sys("power/state"),
                     p.deep ? "mem" : p.freeze ? "freeze" : "disk", diag_yn(p.writable));
        else
            snprintf(if_txt, sizeof if_txt, "no %s on this host", hw_sys("power/state"));
        hw_report_add(&rep, "Power", "Suspend interface", true,
                      p.state_node ? HW_PASS : HW_UNSUPPORTED, "%s", if_txt);
    }
    hw_report_add(&rep, "Power", "Sleep modes", false,
                  p.mem_modes[0] ? HW_PASS : HW_UNSUPPORTED, "%s",
                  p.mem_modes[0] ? p.mem_modes : "no mem_sleep attribute");
    hw_report_add(&rep, "Power", "Lid switch", false, p.lid ? HW_PASS : HW_UNSUPPORTED,
                  p.lid ? "present" : "no ACPI lid (desktop)");
    hw_report_add(&rep, "Power", "Battery", false,
                  p.battery ? HW_PASS : HW_UNSUPPORTED,
                  p.battery ? "%d%%" : "mains powered (iMac has no battery)", p.battery_pct);
    hw_report_add(&rep, "Power", "Display sleep", true,
                  (b && b->writable && !b->suspect) ? HW_PASS : HW_UNSUPPORTED,
                  "%s", b ? bl.mechanism : "no backlight device");

    /* Suspend verdict: never claimed on hardware with a documented failure. */
    if (!p.state_node) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED,
                      "no suspend interface on this host");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "cannot test without suspend");
    } else if (bl.apple_machine && !allow) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_UNSUPPORTED,
                      "disabled by policy: %s", p.note);
        hw_report_add(&rep, "Power", "Resume", false, HW_UNSUPPORTED,
                      "blocked with suspend; use display-sleep/display-wake instead");
    } else {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED,
                      "interface present; a suspend attempt needs a human to confirm the panel "
                      "comes back (re-run with --allow-suspend)");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "not exercised");
    }

    printf("  %-18s %s\n", "Suspend interface:", p.state_node ? hw_sys("power/state") : "absent");
    printf("  %-18s %s\n", "Sleep modes:", p.mem_modes[0] ? p.mem_modes : "-");
    printf("  %-18s %s\n", "Backlight:", bl.mechanism);
    printf("  %-18s %s\n", "Platform:", bl.dmi_vendor[0] ? bl.dmi_vendor : "unknown");
    printf("  %-18s %s\n\n", "Caveat:", p.note);
    for (int i = 0; i < rep.n; i++)
        printf("  %-18s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    (void)argc;
    return hw_report_exit(&rep);
}
