/* maclite-storage — disk, filesystem and space checks (spec §12).
 *
 *   maclite-storage                 enumerate + report
 *   maclite-storage --bench         measure sequential write/read on the
 *                                   filesystem holding $TMPDIR (default /tmp)
 *   maclite-storage --unmount PATH  unmount removable media
 * No storage daemon is started by this tool or by MacLiteOS.
 */
#include "diag_common.h"
#include <sys/mount.h>
#include <errno.h>

static void bench(const char *dir)
{
    char path[300];
    snprintf(path, sizeof path, "%s/maclite-storage.bench", dir);
    size_t n = 32u * 1024 * 1024;
    char *buf = ml_alloc(n);
    memset(buf, 0xA5, n);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  cannot create %s: %s\n", path, strerror(errno)); ml_free(buf); return; }
    uint64_t t0 = ml_now_ns();
    fwrite(buf, 1, n, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    double wms = ml_elapsed_ms(t0);
    f = fopen(path, "rb");
    t0 = ml_now_ns();
    size_t got = f ? fread(buf, 1, n, f) : 0;
    if (f) fclose(f);
    double rms = ml_elapsed_ms(t0);
    printf("  %-18s %s\n", "Path:", path);
    printf("  %-18s %.0f MB in %.0f ms = %.0f MB/s\n", "Sequential write:",
           n / (1024.0 * 1024.0), wms, n / wms / 1e3);
    printf("  %-18s %zu MB in %.0f ms = %.0f MB/s\n", "Sequential read:",
           got / (1024 * 1024), rms, rms > 0 ? n / rms / 1e3 : 0.0);
    unlink(path);
    ml_free(buf);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--bench [DIR]] [--unmount PATH]\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    const char *unmount = diag_opt(argc, argv, "--unmount");
    if (unmount) {
        if (umount(unmount) == 0) { printf("unmounted %s\n", unmount); return HW_EXIT_PASS; }
        fprintf(stderr, "unmount %s failed: %s\n", unmount, strerror(errno));
        return HW_EXIT_FAIL;
    }

    mica_storage_state s;
    mica_storage_probe(&s);

    diag_title("MacLiteOS Storage");
    hw_report rep;
    hw_report_init(&rep);

    hw_report_add(&rep, "Storage", "Block devices", true,
                  s.n ? HW_PASS : HW_UNSUPPORTED, "%d device(s), %d whole disk(s), %d mounted",
                  s.n, s.nwhole, s.nmounted);
    diag_section("Devices");
    if (!s.n) printf("  (none under %s)\n", hw_sys("block"));
    for (int i = 0; i < s.n; i++) {
        const mica_disk *d = &s.v[i];
        printf("  %-10s %-9s %-6s %-22s %s\n", d->name,
               d->whole ? "disk" : "part", d->removable ? "remov" : "fixed",
               d->model[0] ? d->model : "-", d->devnode);
        if (d->mounted)
            printf("             %s on %s  %llu MB total, %llu MB free (%d%% used)\n",
                   d->fstype, d->mountpoint,
                   (unsigned long long)(d->fs_total_kb / 1024),
                   (unsigned long long)(d->fs_free_kb / 1024), d->use_pct);
        else
            printf("             not mounted%s\n", d->fstype[0] ? "" : " (no filesystem detected)");
    }
    putchar('\n');

    int root_i = -1;
    for (int i = 0; i < s.n; i++)
        if (!strcmp(s.v[i].mountpoint, "/")) { root_i = i; break; }
    hw_report_add(&rep, "Storage", "Root filesystem", true,
                  root_i >= 0 ? HW_PASS : HW_UNSUPPORTED,
                  root_i >= 0 ? "%s %s, %d%% used" : "no mount point at / in %s",
                  root_i >= 0 ? s.v[root_i].fstype : "",
                  root_i >= 0 ? s.v[root_i].devnode : "",
                  root_i >= 0 ? s.v[root_i].use_pct : 0);
    hw_report_add(&rep, "Storage", "Removable media", false,
                  s.nremovable ? HW_PASS : HW_UNSUPPORTED, "%d removable device(s)", s.nremovable);
    hw_report_add(&rep, "Storage", "Low space", false,
                  s.low_space[0] ? HW_PARTIAL : HW_PASS, "%s",
                  s.low_space[0] ? s.low_space : "no mounted filesystem above 90% full");
    hw_report_add(&rep, "Storage", "Filesystem errors", false, HW_NOT_TESTED,
                  "smartctl/fsck are not part of MacLiteOS; run them from the recovery shell");

    if (diag_flag(argc, argv, "--bench")) {
        const char *dir = argc > 2 && argv[2][0] != '-' ? argv[2] : "/tmp";
        diag_section("Throughput");
        bench(dir);
        putchar('\n');
    }

    for (int i = 0; i < rep.n; i++)
        printf("  %-18s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    (void)argc;
    return hw_report_exit(&rep);
}
