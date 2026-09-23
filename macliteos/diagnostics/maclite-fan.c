/* maclite-fan — AppleSMC fan control and thermal regulation for iMac.
 *
 * iMac Mid-2010 (iMac11,2 and iMac11,3) has 3 hardware fans:
 *   fan1: ODD (Optical Disc Drive)
 *   fan2: HDD (Hard Disk Drive)
 *   fan3: CPU (Processor)
 *
 * Known Issue: If an SSD is installed or the Apple proprietary SATA thermal
 * sensor is disconnected, Apple SMC firmware defaults to emergency 5500-6000 RPM
 * full-blast fan noise ("jet engine").
 *
 * maclite-fan discovers applesmc sysfs, reads fan telemetry with zero fake data,
 * and allows regulating fan speeds to quiet, safe baselines while dynamically
 * scaling with CPU core temperatures.
 */
#include "diag_common.h"
#include <dirent.h>

#define MAX_FANS 6

typedef struct {
    int index;
    char label[32];
    int current_rpm;
    int min_rpm;
    int max_rpm;
    int target_rpm;
    int manual;
} ml_fan_info;

typedef struct {
    bool present;
    char path[256];
    int fan_count;
    ml_fan_info fans[MAX_FANS];
    int cpu_temp_c;
} ml_smc_state;

static int read_sysfs_int(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static bool write_sysfs_int(const char *path, int val)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    int ret = fprintf(f, "%d\n", val);
    fclose(f);
    return ret > 0;
}

static bool read_sysfs_str(const char *path, char *buf, size_t sz)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(buf, sz, f)) { fclose(f); return false; }
    fclose(f);
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    return true;
}

static bool find_applesmc_path(char *out_path, size_t sz)
{
    /* Check direct platform device */
    const char *pdev = hw_sys("devices/platform/applesmc.768");
    if (access(pdev, F_OK) == 0) {
        snprintf(out_path, sz, "%s", pdev);
        return true;
    }

    /* Check hwmon devices */
    const char *hwmon_dir = hw_sys("class/hwmon");
    DIR *d = opendir(hwmon_dir);
    if (!d) return false;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char name_path[512];
        snprintf(name_path, sizeof(name_path), "%s/%s/name", hwmon_dir, de->d_name);
        char name[64];
        if (read_sysfs_str(name_path, name, sizeof(name))) {
            if (strstr(name, "applesmc") != NULL) {
                snprintf(out_path, sz, "%s/%s", hwmon_dir, de->d_name);
                closedir(d);
                return true;
            }
        }
    }
    closedir(d);
    return false;
}

static int probe_cpu_temp(void)
{
    /* Check coretemp / thermal zones */
    const char *hwmon_dir = hw_sys("class/hwmon");
    DIR *d = opendir(hwmon_dir);
    if (!d) return -1;

    struct dirent *de;
    int best_temp = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "%s/%s/temp1_input", hwmon_dir, de->d_name);
        int t = read_sysfs_int(temp_path);
        if (t > 0) {
            int tc = t / 1000;
            if (tc > best_temp) best_temp = tc;
        }
    }
    closedir(d);
    return best_temp;
}

static void probe_smc(ml_smc_state *st)
{
    memset(st, 0, sizeof(*st));
    st->present = find_applesmc_path(st->path, sizeof(st->path));
    st->cpu_temp_c = probe_cpu_temp();

    if (!st->present) return;

    /* Count fans */
    char fans_num_path[512];
    snprintf(fans_num_path, sizeof(fans_num_path), "%s/fans", st->path);
    int num_fans = read_sysfs_int(fans_num_path);
    if (num_fans <= 0) {
        /* Probe fan1..MAX_FANS manually */
        num_fans = 0;
        for (int i = 1; i <= MAX_FANS; i++) {
            char p[512];
            snprintf(p, sizeof(p), "%s/fan%d_input", st->path, i);
            if (access(p, F_OK) == 0) num_fans = i;
        }
    }
    if (num_fans > MAX_FANS) num_fans = MAX_FANS;
    st->fan_count = num_fans;

    for (int i = 1; i <= num_fans; i++) {
        ml_fan_info *fi = &st->fans[i - 1];
        fi->index = i;

        char path_buf[512];
        /* Label */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_label", st->path, i);
        if (!read_sysfs_str(path_buf, fi->label, sizeof(fi->label))) {
            snprintf(fi->label, sizeof(fi->label), "Fan %d", i);
        }

        /* Current RPM */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_input", st->path, i);
        fi->current_rpm = read_sysfs_int(path_buf);

        /* Min RPM */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_min", st->path, i);
        fi->min_rpm = read_sysfs_int(path_buf);

        /* Max RPM */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_max", st->path, i);
        fi->max_rpm = read_sysfs_int(path_buf);

        /* Target / Output RPM */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_output", st->path, i);
        int tgt = read_sysfs_int(path_buf);
        if (tgt < 0) {
            snprintf(path_buf, sizeof(path_buf), "%s/fan%d_target", st->path, i);
            tgt = read_sysfs_int(path_buf);
        }
        fi->target_rpm = tgt;

        /* Manual mode */
        snprintf(path_buf, sizeof(path_buf), "%s/fan%d_manual", st->path, i);
        fi->manual = read_sysfs_int(path_buf);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [status | quiet | auto | set <fan_idx> <rpm> | restore | --diagnostics]\n"
        "  status           Report current AppleSMC fan speeds and thresholds\n"
        "  quiet            Set ODD and HDD fans to quiet minimums (~1000 RPM) to stop SSD noise\n"
        "  auto             Apply automatic thermal regulation curve matching CPU temperature\n"
        "  set <idx> <rpm>  Set target RPM for fan 1..N and enable manual mode\n"
        "  restore          Restore all fans to SMC automatic hardware control\n"
        "  --diagnostics    Output formal hardware diagnostic results\n"
        "Exit codes: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n",
        prog);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help") || diag_flag(argc, argv, "-h")) {
        usage(argv[0]);
        return HW_EXIT_USAGE;
    }

    const char *cmd = "status";
    if (argc > 1 && argv[1][0] != '-') {
        cmd = argv[1];
    } else if (diag_flag(argc, argv, "--diagnostics")) {
        cmd = "diagnostics";
    }

    ml_smc_state smc;
    probe_smc(&smc);

    if (!strcmp(cmd, "status") || !strcmp(cmd, "diagnostics")) {
        diag_title("MacLiteOS AppleSMC Fan & Thermal Management");
        hw_report rep;
        hw_report_init(&rep);

        if (!smc.present) {
            hw_report_add(&rep, "Thermal", "AppleSMC Hardware", false, HW_UNSUPPORTED,
                          "no applesmc device found (non-Apple host or module not loaded)");
            hw_report_print(&rep);
            return HW_EXIT_UNSUPPORTED;
        }

        hw_report_add(&rep, "Thermal", "AppleSMC Hardware", true, HW_PASS,
                      "%s (%d fans enumerated)", smc.path, smc.fan_count);

        if (smc.cpu_temp_c > 0) {
            hw_report_add(&rep, "Thermal", "CPU Core Temperature", true, HW_PASS,
                          "%d C", smc.cpu_temp_c);
        } else {
            hw_report_add(&rep, "Thermal", "CPU Core Temperature", false, HW_NOT_TESTED,
                          "temperature probe unavailable");
        }

        for (int i = 0; i < smc.fan_count; i++) {
            ml_fan_info *f = &smc.fans[i];
            char key[64];
            snprintf(key, sizeof(key), "Fan %d (%s)", f->index, f->label);
            hw_report_add(&rep, "Thermal", key, true,
                          (f->current_rpm > 0) ? HW_PASS : HW_NOT_TESTED,
                          "%d RPM (min %d, max %d, target %d, mode: %s)",
                          f->current_rpm, f->min_rpm, f->max_rpm, f->target_rpm,
                          f->manual == 1 ? "manual" : "auto-smc");
        }

        hw_report_print(&rep);
        return HW_EXIT_PASS;
    }

    if (!smc.present) {
        fprintf(stderr, "maclite-fan: error: AppleSMC not detected on this system\n");
        return HW_EXIT_UNSUPPORTED;
    }

    if (!strcmp(cmd, "quiet") || !strcmp(cmd, "auto")) {
        printf("Applying MacLiteOS quiet thermal curve (CPU: %d C)...\n", smc.cpu_temp_c);
        bool all_ok = true;

        for (int i = 0; i < smc.fan_count; i++) {
            ml_fan_info *f = &smc.fans[i];
            int target_rpm = f->min_rpm > 0 ? f->min_rpm : 1000;

            if (strstr(f->label, "CPU") != NULL) {
                /* Scale CPU fan with temperature */
                int t = smc.cpu_temp_c > 0 ? smc.cpu_temp_c : 50;
                if (t <= 50) {
                    target_rpm = f->min_rpm > 0 ? f->min_rpm : 1200;
                } else if (t < 75) {
                    int base = f->min_rpm > 0 ? f->min_rpm : 1200;
                    target_rpm = base + (t - 50) * (2500 - base) / 25;
                } else {
                    target_rpm = 3000 + (t - 75) * 50;
                    if (f->max_rpm > 0 && target_rpm > f->max_rpm) target_rpm = f->max_rpm;
                }
            } else if (strstr(f->label, "HDD") != NULL) {
                /* HDD fan safe quiet speed (eliminates swapped-SSD jet engine noise) */
                target_rpm = f->min_rpm > 0 ? f->min_rpm : 1100;
            } else if (strstr(f->label, "ODD") != NULL) {
                /* ODD fan minimum baseline */
                target_rpm = f->min_rpm > 0 ? f->min_rpm : 1000;
            }

            char m_path[512], o_path[512];
            snprintf(m_path, sizeof(m_path), "%s/fan%d_manual", smc.path, f->index);
            snprintf(o_path, sizeof(o_path), "%s/fan%d_output", smc.path, f->index);

            bool ok1 = write_sysfs_int(m_path, 1);
            bool ok2 = write_sysfs_int(o_path, target_rpm);
            if (!ok2) {
                snprintf(o_path, sizeof(o_path), "%s/fan%d_target", smc.path, f->index);
                ok2 = write_sysfs_int(o_path, target_rpm);
            }

            printf("  Fan %d [%s]: target -> %d RPM (manual) [%s]\n",
                   f->index, f->label, target_rpm, (ok1 && ok2) ? "OK" : "FAILED (check permissions)");
            if (!ok1 || !ok2) all_ok = false;
        }

        return all_ok ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }

    if (!strcmp(cmd, "restore")) {
        printf("Restoring all fans to AppleSMC hardware control...\n");
        bool all_ok = true;
        for (int i = 0; i < smc.fan_count; i++) {
            ml_fan_info *f = &smc.fans[i];
            char m_path[512];
            snprintf(m_path, sizeof(m_path), "%s/fan%d_manual", smc.path, f->index);
            bool ok = write_sysfs_int(m_path, 0);
            printf("  Fan %d [%s]: mode -> auto [%s]\n", f->index, f->label, ok ? "OK" : "FAILED");
            if (!ok) all_ok = false;
        }
        return all_ok ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }

    if (!strcmp(cmd, "set")) {
        if (argc < 4) {
            usage(argv[0]);
            return HW_EXIT_USAGE;
        }
        int idx = atoi(argv[2]);
        int rpm = atoi(argv[3]);
        if (idx < 1 || idx > smc.fan_count) {
            fprintf(stderr, "maclite-fan: invalid fan index %d (valid: 1..%d)\n", idx, smc.fan_count);
            return HW_EXIT_USAGE;
        }
        ml_fan_info *f = &smc.fans[idx - 1];
        if (f->min_rpm > 0 && rpm < f->min_rpm) rpm = f->min_rpm;
        if (f->max_rpm > 0 && rpm > f->max_rpm) rpm = f->max_rpm;

        char m_path[512], o_path[512];
        snprintf(m_path, sizeof(m_path), "%s/fan%d_manual", smc.path, idx);
        snprintf(o_path, sizeof(o_path), "%s/fan%d_output", smc.path, idx);

        write_sysfs_int(m_path, 1);
        bool ok = write_sysfs_int(o_path, rpm);
        if (!ok) {
            snprintf(o_path, sizeof(o_path), "%s/fan%d_target", smc.path, idx);
            ok = write_sysfs_int(o_path, rpm);
        }
        printf("Fan %d [%s]: manual target set to %d RPM [%s]\n", idx, f->label, rpm, ok ? "OK" : "FAILED");
        return ok ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }

    usage(argv[0]);
    return HW_EXIT_USAGE;
}
