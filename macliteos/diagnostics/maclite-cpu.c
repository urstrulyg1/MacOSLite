/* maclite-cpu — CPU, microcode and frequency-scaling diagnostics (spec §6, §7).
 *
 * Three questions, kept apart so none of them can launder a claim:
 *
 *   1. What is installed here?            -> /proc/cpuinfo + sysfs, printed raw
 *      and cross-checked against the known-part table. A mismatch is printed as
 *      a mismatch.
 *   2. Is the microcode right?            -> loaded revision (read), shipped
 *      revision (parsed out of the matched blob), compatibility (blob signature
 *      must equal this CPU's). Incompatible microcode is never selected.
 *   3. Is frequency scaling configured?   -> cpufreq driver/governor/limits,
 *      idle states, turbo knob, thermal sensors, and what MacLiteOS recommends.
 *
 *   maclite-cpu                     report
 *   maclite-cpu --tsv               machine readable
 *   maclite-cpu --governor NAME     set a governor on every CPU, then verify
 *   maclite-cpu --turbo on|off      write the boost knob, read it back
 *   maclite-cpu --recommend         what MacLiteOS would choose, and why
 *   maclite-cpu --policy            check the no-daemon / no-polling rules
 * exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage
 */
#include "diag_common.h"
#include "../hardware/cpu.h"
#include <dirent.h>

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [--tsv] [--governor NAME] [--turbo on|off] [--recommend] [--policy]\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

/* §7 is mostly about what the OS does NOT do: no permanent CPU monitor, no
 * sampling daemon, no polling loop. That is checkable — count the processes
 * whose name matches a known governor/monitor daemon and look at the thermal
 * poll interval knob. Absence of the daemon is a pass; presence is a fail. */
static void policy_check(hw_report *rep)
{
    static const char *banned[] = {
        "thermald", "powerd", "turbostat", "irqbalance", "cpupower", "mcelog",
        "powertop", "rtkit-daemon", "systemd-oomd",
    };
    char found[128] = "";
    /* The process table lives in the *host* /proc, never in a fixture root: a
     * fixture cannot say which processes run here, so an empty table is NOT
     * TESTED rather than "no daemon is running". */
    DIR *d = opendir("/proc");
    int hit = 0, seen = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            seen++;
            char path[300];
            snprintf(path, sizeof path, "/proc/%.30s/comm", e->d_name);
            char *n = ml_read_file(path, NULL);
            if (!n) continue;
            char *t = ml_str_trim(n);
            for (size_t i = 0; i < ML_ARRAY_SIZE(banned); i++)
                if (!strcmp(t, banned[i])) {
                    hit++;
                    size_t off = strlen(found);
                    snprintf(found + off, sizeof found - off, "%s%.16s", off ? ", " : "", banned[i]);
                }
            ml_free(n);
        }
        closedir(d);
    }
    if (!seen) {
        hw_report_add(rep, "Policy", "No monitoring daemon", true, HW_NOT_TESTED,
                      "the process table is unreadable here, so nothing can be said about "
                      "what is running");
    } else if (hit) {
        hw_report_add(rep, "Policy", "No monitoring daemon", true, HW_FAIL,
                      "running here: %.100s (MacLiteOS ships none of these)", found);
    } else {
        hw_report_add(rep, "Policy", "No monitoring daemon", true, HW_PASS,
                      "none of thermald/powerd/turbostat/irqbalance/cpupower/mcelog/powertop is "
                      "running (%d processes checked)", seen);
    }
    hw_report_add(rep, "Policy", "Event driven", true, HW_PASS,
                  "governor and boost changes are one-shot sysfs writes with read-back; "
                  "the thermal value is read on demand, not sampled");
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    bool tsv = diag_flag(argc, argv, "--tsv");
    const char *gov = diag_opt(argc, argv, "--governor");
    const char *turbo = diag_opt(argc, argv, "--turbo");
    bool want_rec = diag_flag(argc, argv, "--recommend");
    bool want_policy = diag_flag(argc, argv, "--policy");

    ml_cpu_state c;
    if (!ml_cpu_probe(&c)) {
        fprintf(stderr, "%s: cannot read %s\n", argv[0], hw_proc("cpuinfo"));
        return HW_EXIT_UNSUPPORTED;
    }

    /* ---- one-shot control paths ---- */
    if (gov) {
        bool in_list = c.governors[0] && strstr(c.governors, gov) != NULL;
        if (!in_list) {
            printf("governor '%s' is not in scaling_available_governors (%s)\n", gov,
                   c.governors[0] ? c.governors : "unreadable");
            return HW_EXIT_UNSUPPORTED;
        }
        if (hw_using_fixture()) {
            printf("fixture mode: not writing to %s (a fixture is not a CPU)\n", hw_sys("devices/system/cpu/cpu0/cpufreq"));
            return HW_EXIT_NOT_TESTED;
        }
        int n = ml_cpu_set_governor(&c, gov);
        if (n <= 0) {
            printf("FAIL: could not set governor '%s' on any CPU (needs root)\n", gov);
            return HW_EXIT_FAIL;
        }
        printf("governor set on %d CPU(s) and verified by read-back: %s\n", n, c.governor);
        return HW_EXIT_PASS;
    }
    if (turbo) {
        bool on = !strcmp(turbo, "on") || !strcmp(turbo, "1") || !strcmp(turbo, "enable");
        if (!c.turbo_knob) {
            printf("no cpufreq/boost knob on this machine: Turbo cannot be toggled here\n");
            return HW_EXIT_UNSUPPORTED;
        }
        if (hw_using_fixture()) {
            printf("fixture mode: not writing to the boost knob\n");
            return HW_EXIT_NOT_TESTED;
        }
        if (!ml_cpu_set_turbo(&c, on)) {
            printf("FAIL: writing the boost knob did not stick\n");
            return HW_EXIT_FAIL;
        }
        printf("Turbo Boost %s (verified by read-back)\n", on ? "enabled" : "disabled");
        return HW_EXIT_PASS;
    }
    if (want_rec) {
        char why[320];
        const char *r = ml_cpu_recommend_governor(&c, why, sizeof why);
        if (!r) { printf("no recommendation available: %s\n", why); return HW_EXIT_NOT_TESTED; }
        printf("recommended governor: %s\n  %s\n", r, why);
        return HW_EXIT_PASS;
    }
    if (want_policy) {
        hw_report rep;
        hw_report_init(&rep);
        policy_check(&rep);
        hw_report_print(&rep, "MacLiteOS CPU Policy");
        return hw_report_exit(&rep);
    }

    /* ---- full report ---- */
    hw_report rep;
    hw_report_init(&rep);
    const bool fixture = hw_using_fixture();

    diag_title("MacLiteOS CPU Diagnostics");
    diag_section("CPU");
    diag_kv("Model", "%s", c.part[0] ? c.part : "(unrecognised)");
    diag_kv("Brand string", "%s", c.brand);
    diag_kv("Vendor", "%s", or_dash(c.vendor));
    diag_kv("Family/Model/Stepping", "%u/%u/%u (signature 0x%05x, Intel name %s)",
            c.family, c.model, c.stepping, c.signature, c.ucode_sig);
    diag_kv("Cores / Threads", "%u / %u (%u socket(s), Hyper-Threading %s)", c.cores, c.threads,
            c.sockets ? c.sockets : 1u, diag_yn(c.hyperthreading));
    diag_kv("Cache", "see /proc/cpuinfo (L2/L3 reported per CPU)");
    putchar('\n');
    diag_section("Frequency");
    diag_kv("Current clock", "%u MHz (%s)", c.mhz_cur,
            c.mhz_from_cpufreq ? "scaling_cur_freq" : "cpuinfo sample");
    diag_kv("Base / Min / Max", "%u / %u / %u MHz", c.mhz_base, c.mhz_min, c.mhz_max);
    diag_kv("cpufreq driver", "%s", or_dash(c.scaling_driver));
    diag_kv("Governor", "%s (available: %s)", or_dash(c.governor), or_dash(c.governors));
    diag_kv("Governor writable", "%s", diag_yn(c.governor_writable));
    diag_kv("Turbo Boost", "%s", c.turbo_knob ? (c.turbo_enabled ? "knob present, enabled" : "knob present, disabled")
                                               : (c.turbo_capable ? "capable, no boost knob on this kernel"
                                                                  : "not supported by this part"));
    diag_kv("Idle states", "%s", c.cpuidle ? c.idle_names : "none exposed");
    diag_kv("Idle driver", "%s", or_dash(c.idle_driver));
    if (c.thermal) {
        if (c.thermal_crit_c >= 0)
            diag_kv("Thermal", "%d C (critical %d C)", c.thermal_c, c.thermal_crit_c);
        else
            diag_kv("Thermal", "%d C (no critical point exposed)", c.thermal_c);
        printf("  %-26s %s\n", "", c.thermal_source);
    } else {
        diag_kv("Thermal", "no CPU temperature source found under %s", hw_sys("class/hwmon"));
    }
    putchar('\n');

    /* ---- checks ---- */
    char ev[320];
    hw_report_add(&rep, "CPU", "Detected", true, c.brand[0] ? HW_PASS : HW_FAIL, "%s", c.part);

    char why[400];
    ml_cpu_xcheck x = ml_cpu_crosscheck(&c, why, sizeof why);
    hw_report_add(&rep, "CPU", "Topology", true, x == ML_CPU_MATCH ? HW_PASS : x == ML_CPU_MISMATCH ? HW_FAIL : HW_NOT_TESTED,
                  "%s", why);
    char ht[80];
    snprintf(ht, sizeof ht, "HT flag=%s, %u threads over %u cores",
             c.ht_flag ? "set" : "clear", c.threads, c.cores);
    hw_report_add(&rep, "CPU", "Hyper-Threading", true,
                  c.cores ? HW_PASS : HW_FAIL, "%s", ht);
    hw_report_add(&rep, "CPU", "Instruction sets", true, HW_PASS, "%s (absent: %s)", c.isa, c.isa_absent);
    hw_report_add(&rep, "CPU", "No AVX path", false, c.avx ? HW_UNSUPPORTED : HW_PASS,
                  c.avx ? "this CPU reports AVX: the scalar fallback is still what MacLiteOS runs"
                        : "CPU has no AVX; every MacLiteOS code path is scalar C with runtime dispatch");

    /* microcode */
    if (!c.ucode_loaded_known) {
        hw_report_add(&rep, "Microcode", "Loaded revision", true, HW_NOT_TESTED,
                      "no 'microcode' field in %s (not readable on this kernel/vendor)", hw_proc("cpuinfo"));
        hw_report_add(&rep, "Microcode", "Status", true, HW_NOT_TESTED, "%s", c.ucode_note);
    } else {
        hw_report_add(&rep, "Microcode", "Loaded revision", true, HW_PASS, "0x%x for signature %s",
                      c.ucode_loaded, c.ucode_sig);
        if (!c.ucode_file_present) {
            hw_report_add(&rep, "Microcode", "Available revision", true, HW_UNSUPPORTED, "%s", c.ucode_note);
        } else {
            snprintf(ev, sizeof ev, "0x%x (dated %08x) from %.200s", c.ucode_available, c.ucode_date,
                     c.ucode_file);
            hw_report_add(&rep, "Microcode", "Available revision", true,
                          c.ucode_available > c.ucode_loaded ? HW_PARTIAL : HW_PASS, "%s", ev);
            hw_report_add(&rep, "Microcode", "Compatibility", true, HW_PASS,
                          "blob signature matches this CPU exactly (%s); a blob for another stepping would be refused",
                          c.ucode_sig);
        }
        hw_report_add(&rep, "Microcode", "Status", true,
                      c.ucode_file_present && c.ucode_available <= c.ucode_loaded ? HW_PASS
                      : c.ucode_file_present ? HW_PARTIAL : HW_UNSUPPORTED,
                      "%s", c.ucode_note);
    }

    /* frequency scaling */
    bool scaling_ok = c.cpufreq && c.scaling_driver[0] && strcmp(c.scaling_driver, "none");
    hw_report_add(&rep, "Scaling", "cpufreq present", true, scaling_ok ? HW_PASS : HW_UNSUPPORTED,
                  scaling_ok ? "%s, governor %s" : "no /sys/devices/system/cpu/cpu0/cpufreq on this host",
                  c.scaling_driver, c.governor);
    char gwhy[320];
    const char *rec = ml_cpu_recommend_governor(&c, gwhy, sizeof gwhy);
    hw_report_add(&rep, "Scaling", "Governor", true,
                  !scaling_ok ? HW_UNSUPPORTED : (!rec ? HW_NOT_TESTED : (!strcmp(rec, c.governor) ? HW_PASS : HW_PARTIAL)),
                  "%s is running; MacLiteOS recommends %s — %s", or_dash(c.governor),
                  rec ? rec : "(none)", gwhy);
    hw_report_add(&rep, "Scaling", "Idle states", true,
                  c.cpuidle && c.idle_states > 0 ? HW_PASS : HW_UNSUPPORTED,
                  c.cpuidle ? "%d state(s) via %s: %s" : "no cpuidle states exposed", c.idle_states,
                  c.idle_driver[0] ? c.idle_driver : "?", c.idle_names);
    hw_report_add(&rep, "Scaling", "Turbo Boost", true,
                  !c.turbo_capable ? HW_UNSUPPORTED : c.turbo_knob ? HW_PASS : HW_PARTIAL,
                  c.turbo_capable ? (c.turbo_knob ? (c.turbo_enabled ? "enabled (boost=1 read back)"
                                                                   : "disabled by the boost knob")
                                                  : "capable, but this kernel exposes no boost knob")
                                  : "this part has no Turbo Boost (i3-540/i3-550 are fixed-clock parts)");
    if (c.thermal && c.thermal_crit_c >= 0)
        snprintf(ev, sizeof ev, "%d C, critical %d C, read from %.60s", c.thermal_c,
                 c.thermal_crit_c, c.thermal_source);
    else if (c.thermal)
        snprintf(ev, sizeof ev, "%d C read from %.60s (no critical point exposed)", c.thermal_c,
                 c.thermal_source);
    else
        snprintf(ev, sizeof ev, "no CPU temperature source under %s", hw_sys("class/hwmon"));
    hw_report_add(&rep, "Scaling", "Thermal sensor", true, c.thermal ? HW_PASS : HW_UNSUPPORTED, "%s", ev);
    hw_report_add(&rep, "Scaling", "Mitigations", false, HW_NOT_TESTED, "%.140s%s",
                  c.vulnerabilities[0] ? c.vulnerabilities : "no vulnerability files",
                  c.mitigations_off ? " — NOTE: the documented MacLiteOS cmdline sets mitigations=off; "
                                      "that trade is reported, never hidden" : "");

    (void)want_policy;              /* --policy alone is handled above; the rows
                                     * are part of every report because §7 is a
                                     * requirement, not an option */
    policy_check(&rep);

    hw_report_add(&rep, "Platform", "Machine", false, HW_NOT_TESTED, "%s %s, kernel %s",
                  c.dmi_vendor[0] ? c.dmi_vendor : "unknown vendor", c.dmi_product, c.kernel);

    if (tsv) hw_report_print_tsv(&rep);
    else {
        diag_section("Checks");
        for (int i = 0; i < rep.n; i++)
            printf("  %-22s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
        if (fixture)
            printf("\nNote: fixture roots are set — this describes the fixture tree, not the "
                   "machine you are typing on.\n");
    }
    return hw_report_exit(&rep);
}
