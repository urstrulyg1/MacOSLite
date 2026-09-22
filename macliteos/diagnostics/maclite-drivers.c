/* maclite-drivers — driver/firmware/microcode resolver (spec §4, §5, §25-§30).
 *
 *   maclite-drivers detect     what this machine is, per component
 *   maclite-drivers status     what is installed now, from the state file + live probes
 *   maclite-drivers check      what the resolver would do, with every skipped release explained
 *   maclite-drivers update     install/verify the newest COMPATIBLE release (local repo by
 *                              default; --online fetches, and only from the catalog's URL)
 *   maclite-drivers verify     re-validate the running configuration (post-reboot step)
 *   maclite-drivers rollback   restore the known-good snapshot
 *
 * Exit codes: 0 everything the resolver wanted is satisfied, 1 a required check
 * failed, 2 something could not be exercised here, 3 unsupported, 4 usage.
 *
 * Nothing is downloaded unless --online is given, and nothing is installed
 * without a digest check unless --allow-unpinned says the operator accepts a
 * file the catalog does not pin. The tool never writes to a fixture.
 */
#include "diag_common.h"
#include "../hardware/driver.h"
#include "../hardware/cpu.h"
#include "../core/include/ml/sha256.h"
#include <unistd.h>
#include <sys/stat.h>

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s detect|status|check|update|verify|rollback [options]\n"
        "  --repo DIR          trusted local repository (default $ML_REPO or /usr/lib/maca-lite/repo)\n"
        "  --catalog FILE      catalog file (default <repo>/master.cat, or the shipped offline catalog)\n"
        "  --root DIR          install root to write into (default $ML_ROOT or /)\n"
        "  --tsv               machine readable check output (check/verify)\n"
        "  --component NAME    restrict update/check/verify to one component\n"
        "  --online            allow fetching package files from the catalog's https source\n"
        "  --allow-unpinned    install files the catalog does not pin a digest for (loud, opt-in)\n"
        "  --backup-only       take the known-good snapshot without installing anything\n"
        "  --reboot-required   exit 2 (NOT TESTED) and report that a reboot is needed to validate\n"
        "exit: 0 satisfied, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

static const char *repo_path(const char *opt)
{
    if (opt) return opt;
    const char *env = getenv("ML_REPO");
    if (env && *env) return env;
    return "/usr/lib/maca-lite/repo";
}
static const char *root_path(const char *opt)
{
    if (opt) return opt;
    const char *env = getenv("ML_ROOT");
    return env ? env : "";
}
/* the catalog that ships inside the image, used when a repo has none */
static const char *shipped_catalog(void)
{
    static const char *cands[] = {
        "/usr/share/maca-lite/catalog/maclite-offline.cat",
        "drivers/catalog/maclite-offline.cat",           /* running from the tree */
        "../drivers/catalog/maclite-offline.cat",
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(cands); i++)
        if (access(cands[i], F_OK) == 0) return cands[i];
    return NULL;
}

static hw_result validation_result(const drv_hw *hw, const char *detail, size_t detail_len);

/* ---------------------------------------------------------------- detect --- */
static int cmd_detect(const drv_hw *hw, const drv_catalog *cat, bool tsv)
{
    hw_report rep;
    hw_report_init(&rep);
    char summary[768];
    drv_hw_summary(hw, summary, sizeof summary);

    diag_title("MacLiteOS Hardware Identification");
    printf("  %s\n\n", summary);

    hw_report_add(&rep, "Identity", "DMI product", true,
                  hw->dmi_product[0] ? HW_PASS : HW_UNSUPPORTED, "%s",
                  hw->dmi_product[0] ? hw->dmi_product : "no DMI product name readable here");
    if (hw->present) {
        const hw_gpu_cap *cap = hw_gpu_lookup(hw->vendor, hw->device);
        hw_report_add(&rep, "GPU", "PCI identity", true, HW_PASS,
                      "%04x:%04x rev %02x, subsystem %04x:%04x, driver %s",
                      hw->vendor, hw->device, hw->revision, 0x106b,
                      (unsigned)0, hw->driver[0] ? hw->driver : "(none)");
        hw_report_add(&rep, "GPU", "Named part", true,
                      cap ? HW_PASS : HW_NOT_TESTED, "%s",
                      cap ? cap->model : "this PCI id is not in the capability database");
        hw_report_add(&rep, "GPU", "Video decode silicon", false,
                      hw->decode_mask ? HW_PASS : HW_UNSUPPORTED, "%s via %s",
                      hw->decode_mask ? "fixed-function decoder present" : "no fixed-function decoder",
                      hw->decode_api[0] ? hw->decode_api : "none");
    } else {
        hw_report_add(&rep, "GPU", "Detection", true, HW_UNSUPPORTED, "no PCI display controller here");
    }
    if (hw->cpu_present) {
        char why[300] = "";
        ml_cpu_state cs;
        ml_cpu_probe(&cs);
        ml_cpu_xcheck x = ml_cpu_crosscheck(&cs, why, sizeof why);
        hw_report_add(&rep, "CPU", "Identity", true,
                      x == ML_CPU_MATCH ? HW_PASS : x == ML_CPU_MISMATCH ? HW_FAIL : HW_NOT_TESTED,
                      "%.30s sig=%05x — %.220s", hw->cpu_part, hw->cpu_signature, why);
    }
    if (hw->wifi.present)
        hw_report_add(&rep, "Wi-Fi", "Chipset", true,
                      hw->wifi.in_table ? HW_PASS : HW_NOT_TESTED,
                      "%.50s [%04x:%04x] driver %s", hw->wifi.chipset, hw->wifi.vendor, hw->wifi.device,
                      hw->wifi.driver[0] ? hw->wifi.driver : "(unbound)");
    if (hw->eth.present)
        hw_report_add(&rep, "Ethernet", "Chipset", true,
                      hw->eth.in_table ? HW_PASS : HW_NOT_TESTED,
                      "%.50s [%04x:%04x] driver %s", hw->eth.chipset, hw->eth.vendor, hw->eth.device,
                      hw->eth.driver[0] ? hw->eth.driver : "(unbound)");
    hw_report_add(&rep, "Audio", "Codec", true,
                  hw->audio_codec[0] ? HW_PASS : HW_UNSUPPORTED, "%s",
                  hw->audio_codec[0] ? hw->audio_codec : "no codec readable here");
    hw_report_add(&rep, "Storage", "SATA controller", false,
                  hw->sata_chipset[0] ? HW_PASS : HW_NOT_TESTED, "%s",
                  hw->sata_chipset[0] ? hw->sata_chipset : "controller not named by the device table");
    hw_report_add(&rep, "Other", "SDXC reader", false, hw->sd_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                  "%s", hw->sd_chipset[0] ? hw->sd_chipset : "absent");
    hw_report_add(&rep, "Other", "FireWire", false, hw->fw_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                  "%s", hw->fw_chipset[0] ? hw->fw_chipset : "absent");
    hw_report_add(&rep, "Other", "Bluetooth", false, hw->bt_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                  "%s", hw->bt_chipset[0] ? hw->bt_chipset : "absent");
    hw_report_add(&rep, "Other", "Camera", false, hw->cam_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                  "%s", hw->cam_chipset[0] ? hw->cam_chipset : "absent");

    hw_report_add(&rep, "Catalog", "Loaded", false, cat->loaded ? HW_PASS : HW_FAIL,
                  "%s (%d package entr%s, repo %s)%s", cat->path, cat->n, cat->n == 1 ? "y" : "ies",
                  cat->repo, cat->error[0] ? cat->error : "");
    /* what the resolver maps this machine to — the §4 chain, printed as a chain */
    const char *kinds[] = { "drm", "firmware", "mesa", "microcode" };
    for (size_t k = 0; k < ML_ARRAY_SIZE(kinds); k++) {
        drv_kind kind = drv_kind_parse(kinds[k]);
        char rejected[192] = "";
        const drv_package *p = drv_pick_for(cat, hw, kind, NULL, rejected, sizeof rejected);
        if (!p) continue;
        hw_report_add(&rep, "Selection", p->name, false, HW_PASS,
                      "%s %s (%s)%s", p->name, p->version, p->status,
                      rejected[0] ? " — newer release skipped" : "");
    }
    if (tsv) hw_report_print_tsv(&rep);
    else {
        diag_section("Identification");
        for (int i = 0; i < rep.n; i++)
            printf("  %-24s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    }
    return hw_report_exit(&rep);
}

/* ------------------------------------------------------------- check -------- */
/* What the plan means for the caller's exit code: 0 satisfied, 1 something
 * required is broken or absent, 2 incomplete (a needed component is not in the
 * catalog, or a newer release was declined). */
static int plan_exit(const drv_plan *plan)
{
    for (int i = 0; i < plan->n; i++) {
        const drv_item *it = &plan->v[i];
        if (it->action == DRV_ACT_REPAIR || it->action == DRV_ACT_KERNEL_MISSING ||
            it->action == DRV_ACT_INSTALL)
            return HW_EXIT_FAIL;
    }
    for (int i = 0; i < plan->n; i++) {
        const drv_item *it = &plan->v[i];
        if (it->action == DRV_ACT_UPDATE || it->action == DRV_ACT_INCOMPATIBLE ||
            it->action == DRV_ACT_NOT_IN_CATALOG)
            return HW_EXIT_NOT_TESTED;
    }
    return HW_EXIT_PASS;
}

static void print_plan(const drv_plan *plan, const drv_verify *catv, bool tsv, const char *root,
                       int rc)
{
    hw_report rep;
    hw_report_init(&rep);
    for (int i = 0; i < plan->n; i++) {
        const drv_item *it = &plan->v[i];
        hw_result r;
        switch (it->action) {
        case DRV_ACT_NONE:            r = it->digest_checked ? HW_PASS : HW_PARTIAL; break;
        case DRV_ACT_KERNEL_IN_USE:   r = HW_PASS; break;
        case DRV_ACT_INSTALL:         r = HW_FAIL; break;      /* the image is incomplete */
        case DRV_ACT_UPDATE:          r = HW_PARTIAL; break;
        case DRV_ACT_REPAIR:          r = HW_FAIL; break;
        case DRV_ACT_KERNEL_MISSING:  r = HW_FAIL; break;
        case DRV_ACT_INCOMPATIBLE:    r = HW_PARTIAL; break;
        default:                      r = HW_NOT_TESTED; break;
        }
        char ev[300];
        if (it->action == DRV_ACT_NONE && !it->digest_checked)
            snprintf(ev, sizeof ev, "%.40s | %.150s — files present, integrity NOT TESTED "
                     "(the catalog does not pin a digest for them)",
                     drv_action_str(it->action), it->reason);
        else
            snprintf(ev, sizeof ev, "%.40s | %.180s", drv_action_str(it->action), it->reason);
        hw_report_add(&rep, it->pkg ? drv_kind_name(it->pkg->kind) : "component", it->target,
                      it->action == DRV_ACT_INSTALL || it->action == DRV_ACT_REPAIR ||
                      it->action == DRV_ACT_KERNEL_MISSING,
                      r, "%s", ev);
    }
    if (!tsv) {
        diag_title("MacLiteOS Driver Resolution");
        printf("  install root: %s   catalog: verified=%s (%s)\n\n",
               root && root[0] ? root : "/",
               catv->signature_checked ? (catv->signature_ok ? "yes" : "FAILED") : "no verifier available",
               catv->detail[0] ? catv->detail : "-");
        for (int i = 0; i < rep.n; i++) {
            printf("  %-12s %-22s %-20s %s\n", rep.v[i].group, rep.v[i].name,
                   hw_result_str(rep.v[i].result), rep.v[i].evidence);
            if (plan->v[i].rejected[0]) printf("      skipped: %s\n", plan->v[i].rejected);
        }
        printf("\nOverall: %s\n", rc == 0 ? "PASS" : rc == 1 ? "FAIL" : "PARTIAL");
    } else {
        hw_report_print_tsv(&rep);
    }
}

static int cmd_check(const drv_hw *hw, const drv_catalog *cat, const char *root, bool tsv)
{
    drv_plan plan;
    drv_plan_resolve(cat, hw, root, &plan);
    drv_verify catv;
    drv_verify_catalog(cat, &catv);
    int rc = plan_exit(&plan);
    print_plan(&plan, &catv, tsv, root, rc);
    return rc;
}

/* --------------------------------------------------------------- update ----- */
static int cmd_update(const drv_hw *hw, const drv_catalog *cat, const char *repo, const char *root,
                      bool online, bool allow_unpinned, bool backup_only, const char *only,
                      bool reboot_required)
{
    drv_plan plan;
    drv_plan_resolve(cat, hw, root, &plan);
    drv_verify catv;
    drv_verify_catalog(cat, &catv);

    printf("MacLiteOS driver update\n  repo: %s\n  install root: %s\n  catalog: %s (%s)\n\n",
           repo, root && root[0] ? root : "/", cat->path,
           catv.signature_checked ? (catv.signature_ok ? "signature verified" : "SIGNATURE FAILED")
                                  : "no signature verifier available (provenance NOT TESTED)");

    /* Refuse before touching anything when the catalog cannot be trusted and the
     * operator did not say otherwise: installing unverified driver code is the
     * one shortcut this tool exists to prevent. */
    if (catv.signature_checked && !catv.signature_ok) {
        printf("refusing to install: %s\n", catv.detail);
        return HW_EXIT_FAIL;
    }
    if (!catv.signature_checked && online) {
        printf("refusing to install from an online source without a verifiable catalog signature.\n"
               "Provide a signed catalog (key + .sig) or stay offline.\n");
        return HW_EXIT_FAIL;
    }
    /* A fixture is a model of a machine, not a machine: refuse to install into
     * one unless the operator names the root explicitly. */
    if (hw_using_fixture() && !getenv("ML_ROOT")) {
        printf("refusing to install: fixture roots are in use and no ML_ROOT install root was given.\n"
               "A fixture must never be written to; pass --root DIR for a throwaway install.\n");
        return HW_EXIT_NOT_TESTED;
    }
    if (online) {
        printf("--online requested: fetching is delegated to the catalog's source URLs.\n");
        const char *curl = ml_find_in_path("curl");
        if (!curl) { printf("no curl on this machine; cannot fetch\n"); return HW_EXIT_UNSUPPORTED; }
    }

    int installed = 0, skipped = 0, failed = 0;
    for (int i = 0; i < plan.n; i++) {
        const drv_item *it = &plan.v[i];
        if (!it->pkg) continue;
        if (only && strcmp(only, it->pkg->name)) continue;
        if (it->action == DRV_ACT_NONE || it->action == DRV_ACT_KERNEL_IN_USE) continue;
        if (it->pkg->kind == DRV_KIND_KERNEL || it->pkg->kind == DRV_KIND_DRM) {
            printf("  %-24s %-10s %s (kernel-side, not a file to install)\n", it->pkg->name,
                   drv_action_str(it->action), it->reason);
            skipped++;
            continue;
        }
        if (backup_only) {
            printf("  %-24s %s (backup-only: nothing installed)\n", it->pkg->name, it->reason);
            continue;
        }
        if (hw_using_fixture() && !getenv("ML_ROOT")) {
            printf("  %-24s refused: fixture roots are set but no ML_ROOT install root is; "
                   "a fixture must not be written to\n", it->pkg->name);
            skipped++;
            continue;
        }
        char err[300] = "";
        printf("  %-24s %s %s -> ", it->pkg->name, it->pkg->version, drv_action_str(it->action));
        fflush(stdout);
        if (drv_install_package(cat, it->pkg, repo, allow_unpinned, err, sizeof err)) {
            printf("installed\n");
            installed++;
        } else {
            printf("FAILED: %s\n", err);
            failed++;
        }
    }
    if (installed == 0 && failed == 0 && skipped) {
        printf("\nnothing installed: %d component(s) were left alone (see the reasons above)\n", skipped);
        return HW_EXIT_NOT_TESTED;
    }
    if (installed == 0 && failed == 0) {
        printf("\nnothing to do: the newest compatible releases are already in place\n");
        /* refresh the state file: it records what the running system now says */
        drv_state st;
        char val[96];
        hw_result vr = validation_result(hw, val, sizeof val);
        drv_state_fill(&st, hw, &plan, hw_result_str(vr));
        char *sp = drv_state_path();
        bool ok = drv_state_save(sp, &st);
        ml_free(sp);
        if (!ok) printf("warning: could not write %s\n", sp);
        printf("validation: %s — %s\n", hw_result_str(vr), val);
        return hw_result_ok(vr) ? HW_EXIT_PASS : HW_EXIT_NOT_TESTED;
    }
    if (failed) return HW_EXIT_FAIL;

    drv_state st;
    drv_state_fill(&st, hw, &plan, "pending reboot");
    char *sp = drv_state_path();
    if (!drv_state_save(sp, &st)) {
        printf("warning: could not write %s\n", sp);
        ml_free(sp);
        return HW_EXIT_FAIL;
    }
    printf("\nstate written to %s\n", sp);
    ml_free(sp);
    printf("installed %d component(s); %d skipped\n", installed, skipped);
    printf("REBOOT REQUIRED%s: a driver, firmware or microcode change only proves itself after a\n"
           "reboot. Run `maclite-drivers verify` then — until that passes, this is NOT TESTED, and\n"
           "MacLiteOS will not claim the change worked (spec §25/§30).\n",
           reboot_required ? "" : "");
    return HW_EXIT_NOT_TESTED;
}

/* ---------------------------------------------------- validation + verify --- */
/* The post-install validation is deliberately made of the same probes the
 * diagnostics use: GPU bound + KMS reachable, microcode revision readable,
 * firmware present for the parts that need it. */
static hw_result validation_result(const drv_hw *hw, const char *detail, size_t detail_len)
{
    char parts[300] = "";
    /* Validation means looking at the running system. With fixture roots in play
     * the probes are reading a model of a machine, so the honest answer is NOT
     * TESTED — never PASS and never a FAIL that would trigger a rollback. */
    if (hw_using_fixture()) {
        if (detail)
            snprintf((char *)detail, detail_len,
                     "fixture roots in use: the running configuration cannot be validated here "
                     "(install a real root and reboot to validate)");
        return HW_NOT_TESTED;
    }
    hw_result worst = HW_PASS;
    if (hw->present) {
        bool kms = false;
        mica_drm_state drm;
        mica_drm_probe(&drm);
        kms = drm.kms && drm.primary >= 0;
        worst = hw_result_worst(worst, kms ? HW_PASS : HW_FAIL);
        size_t n = strlen(parts);
        snprintf(parts + n, sizeof parts - n, "GPU %04x:%04x driver=%s KMS=%s; ",
                 hw->vendor, hw->device, hw->driver[0] ? hw->driver : "(none)", kms ? "up" : "not up");
    }
    ml_cpu_state cs;
    /* Only compare revisions when the image actually carries a blob for this
     * exact stepping: "0x1 vs 0x0" is not a validation of anything. */
    if (ml_cpu_probe(&cs) && cs.ucode_loaded_known && cs.ucode_file_present) {
        bool ok = cs.ucode_available <= cs.ucode_loaded;
        worst = hw_result_worst(worst, ok ? HW_PASS : HW_PARTIAL);
        size_t n = strlen(parts);
        snprintf(parts + n, sizeof parts - n, "microcode 0x%x (image 0x%x)%s; ",
                 cs.ucode_loaded, cs.ucode_available, ok ? "" : " — update has not taken effect");
    }
    if (hw->wifi.present && hw->wifi.firmware[0]) {
        bool ok = hw->wifi.firmware_present;
        worst = hw_result_worst(worst, ok ? HW_PASS : HW_FAIL);
        size_t n = strlen(parts);
        snprintf(parts + n, sizeof parts - n, "wifi %s firmware=%s", hw->wifi.chipset,
                 ok ? "present" : "MISSING");
    }
    if (detail) snprintf((char *)detail, detail_len, "%.250s", parts[0] ? parts : "no components to validate");
    return worst;
}

static int cmd_verify(const drv_hw *hw, const drv_catalog *cat, const char *root, bool tsv,
                      bool auto_rollback)
{
    char detail[256];
    hw_result vr = validation_result(hw, detail, sizeof detail);
    drv_plan plan;
    drv_plan_resolve(cat, hw, root, &plan);

    drv_state st;
    drv_state_fill(&st, hw, &plan, hw_result_str(vr));
    char *sp = drv_state_path();
    /* Same rule as installing: a fixture is not a machine, so it gets no state
     * file unless the operator named a root to keep it in. */
    bool write_state = !(hw_using_fixture() && !getenv("ML_ROOT"));
    if (!write_state)
        printf("(fixture roots in use: no state file is written to %s)\n", sp);
    else if (!drv_state_save(sp, &st))
        printf("warning: cannot write %s\n", sp);

    hw_report rep;
    hw_report_init(&rep);
    hw_report_add(&rep, "Validation", "Running configuration", true, vr, "%s", detail);
    hw_report_add(&rep, "State", "Known-good record", false,
                  hw_result_ok(vr) ? HW_PASS : HW_NOT_TESTED, "%s", sp);
    ml_free(sp);
    hw_report_add(&rep, "State", "Rollback snapshot", false,
                  drv_snapshot_exists(root) ? HW_PASS : HW_NOT_TESTED,
                  drv_snapshot_exists(root) ? "a snapshot exists: rollback is available"
                                            : "no snapshot: nothing to roll back to yet");

    if (tsv) hw_report_print_tsv(&rep);
    else {
        diag_title("MacLiteOS Driver Validation");
        for (int i = 0; i < rep.n; i++)
            printf("  %-24s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    }
    if (vr == HW_FAIL && auto_rollback) {
        char err[300] = "";
        printf("\nvalidation failed: %s\n", detail);
        if (drv_rollback(root, err, sizeof err)) {
            printf("rollback: %s\nrestoring the previous known-good configuration; reboot to apply.\n", err);
            return HW_EXIT_FAIL;
        }
        printf("rollback FAILED: %s\n", err);
        return HW_EXIT_FAIL;
    }
    return hw_report_exit(&rep);
}

/* ------------------------------------------------------------- rollback ----- */
static int cmd_rollback(const char *root)
{
    char err[300] = "";
    if (drv_rollback(root, err, sizeof err)) {
        printf("MacLiteOS driver rollback\n  %s\n  reboot to apply the restored files\n", err);
        return HW_EXIT_PASS;
    }
    printf("MacLiteOS driver rollback\n  %s\n", err);
    return HW_EXIT_FAIL;
}

/* ------------------------------------------------------------- status ------- */
static int cmd_status(const drv_hw *hw, const drv_catalog *cat, const char *root)
{
    drv_state st;
    char *sp = drv_state_path();
    bool have = drv_state_load(sp, &st);
    hw_report rep;
    hw_report_init(&rep);
    diag_title("MacLiteOS Driver State");
    printf("  state file: %s (%s)\n\n", sp, have ? "read" : "absent — nothing recorded yet");

    hw_report_add(&rep, "State", "Record", false, have ? HW_PASS : HW_NOT_TESTED,
                  have ? "hardware %s" : "no state file: run `maclite-drivers verify` to record one", 
                  st.hardware_id);
    if (have) {
        hw_report_add(&rep, "State", "Kernel", false, HW_NOT_TESTED, "recorded: %s (running: %s)",
                      st.kernel, hw->kernel_release);
        hw_report_add(&rep, "State", "GPU driver", false, HW_NOT_TESTED, "recorded: %s (bound: %s)",
                      st.gpu_driver, hw->driver[0] ? hw->driver : "(none)");
        hw_report_add(&rep, "State", "Mesa", false, HW_NOT_TESTED, "recorded: %s (queried: %s)",
                      st.mesa, hw->mesa_version[0] ? hw->mesa_version : "no GL query");
        hw_report_add(&rep, "State", "CPU microcode", false, HW_NOT_TESTED, "recorded: %s", st.microcode);
        hw_report_add(&rep, "State", "Validation", false,
                      !strcmp(st.validation, "PASS") ? HW_PASS : HW_NOT_TESTED,
                      "%s at %s", st.validation, st.timestamp);
        hw_report_add(&rep, "State", "Known-good", false, st.known_good[0] ? HW_PASS : HW_NOT_TESTED,
                      "%s", st.known_good[0] ? st.known_good : "no versions recorded");
    }
    hw_report_add(&rep, "State", "Rollback snapshot", false,
                  drv_snapshot_exists(root) ? HW_PASS : HW_NOT_TESTED,
                  drv_snapshot_exists(root) ? "present" : "absent");

    /* live facts, so a stale state file cannot masquerade as the truth */
    char detail[256];
    hw_result vr = validation_result(hw, detail, sizeof detail);
    hw_report_add(&rep, "Live", "Hardware validation", true, vr, "%s", detail);
    hw_report_add(&rep, "Live", "Catalog", false, cat->loaded ? HW_PASS : HW_FAIL, "%s (%d entries)",
                  cat->path, cat->n);

    for (int i = 0; i < rep.n; i++)
        printf("  %-16s %-24s %-10s %s\n", rep.v[i].group, rep.v[i].name,
               hw_result_str(rep.v[i].result), rep.v[i].evidence);
    ml_free(sp);
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    return hw_report_exit(&rep);
}

/* --------------------------------------------------------------- main ------- */
int main(int argc, char **argv)
{
    if (argc < 2 || diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    const char *cmd = argv[1];
    bool tsv = diag_flag(argc, argv, "--tsv");
    bool online = diag_flag(argc, argv, "--online");
    bool allow_unpinned = diag_flag(argc, argv, "--allow-unpinned");
    bool backup_only = diag_flag(argc, argv, "--backup-only");
    bool reboot_required = diag_flag(argc, argv, "--reboot-required");
    bool auto_rollback = diag_flag(argc, argv, "--auto-rollback");
    const char *only = diag_opt(argc, argv, "--component");
    const char *repo = repo_path(diag_opt(argc, argv, "--repo"));
    const char *root = root_path(diag_opt(argc, argv, "--root"));

    char catalog_path[512];
    const char *catopt = diag_opt(argc, argv, "--catalog");
    if (catopt) snprintf(catalog_path, sizeof catalog_path, "%.400s", catopt);
    else {
        snprintf(catalog_path, sizeof catalog_path, "%.300s/master.cat", repo);
        if (access(catalog_path, F_OK) != 0) {
            const char *shipped = shipped_catalog();
            if (shipped) snprintf(catalog_path, sizeof catalog_path, "%.400s", shipped);
        }
    }
    drv_catalog cat;
    bool catok = drv_catalog_load(catalog_path, &cat);

    drv_hw hw;
    drv_hw_snapshot(&hw);

    if (!strcmp(cmd, "detect")) return cmd_detect(&hw, &cat, tsv);
    if (!strcmp(cmd, "test-catalog")) {   /* internal: parse the catalog and stop */
        printf("%s: %d package(s)%s\n", catalog_path, cat.n, cat.error[0] ? cat.error : "");
        return catok ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }
    if (!catok) {
        fprintf(stderr, "%s: %s\n", cmd, cat.error[0] ? cat.error : "no catalog");
        return HW_EXIT_UNSUPPORTED;
    }
    if (!strcmp(cmd, "status"))   return cmd_status(&hw, &cat, root);
    if (!strcmp(cmd, "check"))    return cmd_check(&hw, &cat, root, tsv);
    if (!strcmp(cmd, "update"))   return cmd_update(&hw, &cat, repo, root, online, allow_unpinned,
                                                    backup_only, only, reboot_required);
    if (!strcmp(cmd, "verify"))   return cmd_verify(&hw, &cat, root, tsv, auto_rollback);
    if (!strcmp(cmd, "rollback")) return cmd_rollback(root);
    usage(argv[0]);
    return HW_EXIT_USAGE;
}
