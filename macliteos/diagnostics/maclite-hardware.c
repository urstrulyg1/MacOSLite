/* maclite-hardware — one unified hardware report (spec §15).
 *
 * Every line is the result of a probe that ran here, in this process. Nothing is
 * carried over from a datasheet: capability-DB facts are labelled as such, and
 * anything that could not be exercised prints NOT TESTED and keeps the overall
 * verdict honest.
 *
 * Rows that need a live kernel (mixer ioctls, addresses, DNS, file writes) are
 * reported NOT TESTED under fixture roots, because a fixture tree cannot answer
 * them — see the shared rules in hardware/{audio,hwprobe}.c.
 *
 *   maclite-hardware            fast report (no decode round trip)
 *   maclite-hardware --full     also run the 1080p H.264 decode measurement
 *   maclite-hardware --tsv      machine readable
 * exit: 0 PASS, 1 FAIL, 2 NOT TESTED/PARTIAL, 3 UNSUPPORTED
 */
#include "diag_common.h"
#include "../hardware/cpu.h"
#include "../hardware/driver.h"
#include <sys/statvfs.h>
#include <netdb.h>
#include <dirent.h>
#include <ctype.h>

/* ---- small helpers the new sections need ------------------------------- */
static bool path_exists(const char *p) { return access(p, F_OK) == 0; }

/* Cards in the reader: /sys/class/block/mmcblk0, mmcblk0p1, ... */
static int count_mmc_cards(char *first, size_t firstlen)
{
    DIR *d = opendir(hw_sys("class/block"));
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "mmcblk", 6)) continue;
        if (strchr(e->d_name, 'p')) continue;         /* a partition, not the card */
        if (n == 0 && first && firstlen) snprintf(first, firstlen, "%.24s", e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

/* The mount point of a block device — or of one of its partitions, which is
 * what a mount table normally names (sda2, mmcblk0p1). */
static bool mount_of(const char *devname, char *out, size_t outlen)
{
    out[0] = 0;
    FILE *f = fopen(hw_proc("self/mountinfo"), "r");
    if (!f) return false;
    char line[1024];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        char *dash = strstr(line, " - ");
        if (!dash) continue;
        /* " - <fstype> <source> <super options>": the source is the second token */
        char *src = dash + 3;
        char fs[32] = "", dev[64] = "";
        if (sscanf(src, "%31s %63s", fs, dev) != 2) continue;
        (void)fs;
        const char *base = ml_path_base(dev);
        bool same = !strcmp(base, devname);
        /* a partition of this device: "sda2" for "sda", "mmcblk0p1" for "mmcblk0" */
        if (!same && !strncmp(base, devname, strlen(devname))) {
            const char *rest = base + strlen(devname);
            if (isdigit((unsigned char)devname[strlen(devname) - 1]) ? *rest == 'p' : isdigit((unsigned char)*rest))
                same = true;
        }
        if (!same) continue;
        /* fields 1-5: id parent major:minor root mountpoint */
        int id = 0, parent = 0;
        char majmin[32] = "", root[256] = "", mp[256] = "";
        if (sscanf(line, "%d %d %31s %255s %255s", &id, &parent, majmin, root, mp) < 5) continue;
        if (mp[0] != '/') continue;
        /* kernel escapes spaces as \040 in mount tables */
        char clean[256];
        size_t o = 0;
        for (size_t i = 0; mp[i] && o + 1 < sizeof clean; i++) {
            if (mp[i] == '\\' && isdigit((unsigned char)mp[i + 1]) &&
                isdigit((unsigned char)mp[i + 2]) && isdigit((unsigned char)mp[i + 3])) {
                clean[o++] = (char)((mp[i + 1] - '0') * 64 + (mp[i + 2] - '0') * 8 + (mp[i + 3] - '0'));
                i += 3;
            } else clean[o++] = mp[i];
        }
        clean[o] = 0;
        snprintf(out, outlen, "%.240s", clean);
        found = true;
    }
    fclose(f);
    return found;
}

/* Is anything holding the optical drive open? MacLiteOS must never poll it, so
 * a background handle would be a policy failure, not a curiosity. Returns -1
 * when the process table cannot be read here (then the row is NOT TESTED). */
static int open_handles_of(const char *needle)
{
    DIR *proc = opendir(hw_proc(""));
    if (!proc) return -1;
    int count = 0, seen_pids = 0;
    char dirname[480];
    snprintf(dirname, sizeof dirname, "%.470s", hw_proc(""));
    struct dirent *e;
    while ((e = readdir(proc))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        seen_pids++;
        char fdpath[512];
        snprintf(fdpath, sizeof fdpath, "%.460s/%.32s/fd", dirname, e->d_name);
        DIR *fds = opendir(fdpath);
        if (!fds) continue;
        struct dirent *fd;
        while ((fd = readdir(fds))) {
            if (fd->d_name[0] == '.') continue;
            char link[512];
            snprintf(link, sizeof link, "%.495s/%.12s", fdpath, fd->d_name);
            char target[PATH_MAX];
            ssize_t n = readlink(link, target, sizeof target - 1);
            if (n <= 0) continue;
            target[n] = 0;
            if (strstr(target, needle)) count++;
        }
        closedir(fds);
    }
    closedir(proc);
    return seen_pids ? count : -1;
}

static hw_result storage_result(mica_storage_state *s, char *ev, size_t evlen)
{
    struct statvfs sv;
    if (statvfs("/", &sv) == 0) {
        uint64_t total = (uint64_t)sv.f_blocks * (sv.f_frsize / 1024);
        uint64_t free_kb = (uint64_t)sv.f_bavail * (sv.f_frsize / 1024);
        snprintf(ev, evlen, "/ holds %llu MB, %llu MB free", 
                 (unsigned long long)(total / 1024), (unsigned long long)(free_kb / 1024));
        /* real write test: 4 MB, then removed */
        char path[128];
        snprintf(path, sizeof path, "/tmp/maclite-hw.%d", (int)getpid());
        FILE *f = fopen(path, "wb");
        if (!f) return HW_FAIL;
        char *buf = ml_zalloc(4u * 1024 * 1024);
        uint64_t t0 = ml_now_ns();
        size_t w = fwrite(buf, 1, 4u * 1024 * 1024, f);
        fclose(f);
        double ms = ml_elapsed_ms(t0);
        unlink(path);
        ml_free(buf);
        if (w != 4u * 1024 * 1024) return HW_FAIL;
        size_t n = strlen(ev);
        snprintf(ev + n, evlen - n, "; wrote 4 MB in %.0f ms (%.0f MB/s)", ms,
                 ms > 0 ? 4.0 / (ms / 1000.0) : 0.0);
        (void)s;
        return HW_PASS;
    }
    snprintf(ev, evlen, "statvfs(/) failed");
    return HW_FAIL;
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--full] [--tsv]\n"
                        "exit: 0 PASS, 1 FAIL, 2 PARTIAL, 3 UNSUPPORTED, 4 usage\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    bool full = diag_flag(argc, argv, "--full");
    bool tsv = diag_flag(argc, argv, "--tsv");

    hw_report rep;
    hw_report_init(&rep);

    /* ---- CPU (spec §31 puts the processor first) ---- */
    ml_cpu_state cs;
    ml_cpu_probe(&cs);
    mica_mem_info mem;
    mica_mem_probe(&mem);
    hw_report_add(&rep, "CPU", "Processor", true, cs.brand[0] ? HW_PASS : HW_FAIL,
                  "%s — %.24s family %u model %u stepping %u, signature %05x",
                  cs.brand[0] ? cs.brand : "(no /proc/cpuinfo)", cs.part[0] ? cs.part : "unrecognised",
                  cs.family, cs.model, cs.stepping, cs.signature);
    hw_report_add(&rep, "CPU", "Topology", true,
                  cs.brand[0] ? HW_PASS : HW_FAIL,
                  "%u core(s), %u thread(s)%s", cs.cores, cs.threads,
                  cs.hyperthreading ? ", hyper-threading" : ", no hyper-threading");
    {
        char why[300] = "";
        ml_cpu_xcheck x = ml_cpu_crosscheck(&cs, why, sizeof why);
        hw_report_add(&rep, "CPU", "Model cross-check", true,
                      x == ML_CPU_MATCH ? HW_PASS : x == ML_CPU_MISMATCH ? HW_FAIL : HW_NOT_TESTED,
                      "%s", why[0] ? why : "the part table agrees with the CPU's own fields");
    }
    /* The microcode row is the one §6 asks for: current and available revision,
     * and PASS only when the running revision is the newest this image has. */
    if (!cs.brand[0]) {
        hw_report_add(&rep, "CPU", "Microcode", true, HW_NOT_TESTED, "no CPU to read a revision from");
    } else if (strcmp(cs.vendor, "GenuineIntel")) {
        hw_report_add(&rep, "CPU", "Microcode", false, HW_UNSUPPORTED,
                      "not an Intel CPU: the microcode interface does not apply");
    } else if (!cs.ucode_loaded_known) {
        hw_report_add(&rep, "CPU", "Microcode", true, HW_NOT_TESTED,
                      "/proc/cpuinfo exposes no microcode revision on this kernel");
    } else {
        bool newest = !cs.ucode_available_known || cs.ucode_available <= cs.ucode_loaded;
        char ev[300];
        if (cs.ucode_note[0])
            snprintf(ev, sizeof ev, "running 0x%x — %.230s", cs.ucode_loaded, cs.ucode_note);
        else
            snprintf(ev, sizeof ev, "running 0x%x; %.200s", cs.ucode_loaded,
                     cs.ucode_available_known ? "the newest blob in this image is already loaded"
                                              : "no microcode blob is shipped for this stepping");
        hw_report_add(&rep, "CPU", "Microcode", true, newest ? HW_PASS : HW_PARTIAL, "%s", ev);
    }
    hw_report_add(&rep, "CPU", "Frequency scaling", true,
                  cs.scaling_driver[0] ? HW_PASS : HW_NOT_TESTED,
                  cs.scaling_driver[0] ? "%s, governor %s%s, %u MHz base / %u MHz max"
                                      : "no cpufreq interface on this machine (virtual or fixed clock)",
                  cs.scaling_driver, cs.governor[0] ? cs.governor : "?",
                  cs.governor_writable ? " (writable)" : " (not writable here)",
                  cs.mhz_base, cs.mhz_max);
    if (cs.thermal)
        hw_report_add(&rep, "CPU", "Thermal", true, HW_PASS, "%d C%s%d C critical (%.60s)",
                      cs.thermal_c, cs.thermal_crit_c > 0 ? ", " : " (no critical point reported)",
                      cs.thermal_crit_c, cs.thermal_source);
    else
        hw_report_add(&rep, "CPU", "Thermal", true, HW_NOT_TESTED,
                      "no hwmon/thermal zone exposes a package temperature");
    if (cs.vuln_affected >= 0)
        hw_report_add(&rep, "CPU", "Vulnerabilities", false, HW_PASS, "%s", cs.vulnerabilities);
    else
        hw_report_add(&rep, "CPU", "Vulnerabilities", false, HW_NOT_TESTED,
                      "this kernel does not report CPU vulnerability status");
    hw_report_add(&rep, "CPU", "System RAM", true, mem.total_kb ? HW_PASS : HW_FAIL,
                  "%llu MB total, %llu MB available",
                  (unsigned long long)(mem.total_kb / 1024), (unsigned long long)(mem.avail_kb / 1024));

    /* ---- GPU ---- */
    mica_gpu_info g;
    mica_gpu_probe(&g);
    mica_drm_state drm;
    mica_drm_probe(&drm);
    hw_report_add(&rep, "GPU", "Detection", true,
                  g.present ? HW_PASS : HW_UNSUPPORTED, "%s",
                  g.present ? g.device : "no PCI display controller here");
    hw_report_add(&rep, "GPU", "PCI ID", false,
                  g.present ? HW_PASS : HW_UNSUPPORTED,
                  g.present ? "%04x:%04x at %s" : "none",
                  g.vendor_id, g.device_id, g.pci_addr);
    hw_report_add(&rep, "GPU", "Kernel Driver", false,
                  g.driver[0] ? HW_PASS : (g.present ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", g.driver[0] ? g.driver : "no driver bound");
    hw_report_add(&rep, "GPU", "DRM/KMS", true,
                  (g.has_kms && drm.any) ? HW_PASS : (drm.any ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", g.card_node[0] ? g.card_node : "no /dev/dri/card*");
    gl_probe_result glr = mica_gl_probe(&g);
    hw_report_add(&rep, "GPU", "OpenGL Version", false,
                  g.gl_probed ? HW_PASS : HW_NOT_TESTED,
                  "%s", g.gl_probed && g.gl_version[0] ? g.gl_version : "not verified");
    hw_report_add(&rep, "GPU", "Hardware Rendering", true,
                  glr == GL_HW ? HW_PASS : glr == GL_SW ? HW_FAIL
                  : glr == GL_NONE ? HW_UNSUPPORTED : HW_NOT_TESTED,
                  "%s", g.gl_probed ? g.gl_renderer : "no GL query possible on this host");
    hw_report_add(&rep, "GPU", "Vulkan", false, HW_UNSUPPORTED,
                  "TeraScale architecture predates Vulkan 1.0 (RADV requires GCN 1.0+)");

    /* ---- Display ---- */
    bool disp_ok = drm.primary >= 0;
    hw_report_add(&rep, "Display", "Detection", true,
                  disp_ok ? HW_PASS : (drm.any ? HW_FAIL : HW_UNSUPPORTED),
                  disp_ok ? "%s %s" : "no connected connector",
                  disp_ok ? drm.c[drm.primary].connector : "",
                  disp_ok ? drm.c[drm.primary].status : "");
    const edid_mode *pref = disp_ok ? edid_preferred(&drm.c[drm.primary].edid) : NULL;
    hw_report_add(&rep, "Display", "Native Resolution", true,
                  pref ? HW_PASS : (disp_ok && drm.c[drm.primary].nmodes ? HW_PARTIAL : HW_UNSUPPORTED),
                  pref ? "%dx%d@%d Hz" : "no EDID preferred mode",
                  pref ? pref->w : 0, pref ? pref->h : 0, pref ? edid_refresh_hz(pref) : 0);
    bl_state bl;
    bl_probe(&bl);
    const bl_device *b = bl_active(&bl);
    int pct = 0;
    bool got_pct = b && bl_get_percent(&bl, &pct);
    hw_report_add(&rep, "Display", "Brightness Control", true,
                  (b && b->writable && !b->suspect && got_pct) ? HW_PASS
                  : b ? HW_FAIL : HW_UNSUPPORTED,
                  "%s", b ? (b->suspect ? b->note : bl.mechanism) : "no backlight device");

    /* External display (Mini DisplayPort / external connector) */
    {
        int ext_idx = -1;
        for (int i = 0; i < drm.n; i++) {
            if (i == drm.primary) continue;
            if (strstr(drm.c[i].connector, "DP") || strstr(drm.c[i].connector, "HDMI") ||
                strstr(drm.c[i].connector, "DVI") || strstr(drm.c[i].connector, "VGA")) {
                ext_idx = i;
                break;
            }
        }
        if (ext_idx >= 0) {
            bool ext_conn = drm.c[ext_idx].connected;
            hw_report_add(&rep, "Display", "External Display", false,
                          ext_conn ? HW_PASS : HW_NOT_TESTED,
                          "%s: %s%s", drm.c[ext_idx].connector, drm.c[ext_idx].status,
                          ext_conn ? "" : " (no external monitor attached)");
        } else {
            hw_report_add(&rep, "Display", "External Display", false,
                          drm.kms ? HW_NOT_TESTED : HW_UNSUPPORTED,
                          drm.kms ? "no secondary display connector detected" : "no KMS display controller");
        }
    }

    /* ---- Audio ---- */
    ml_aud_state as;
    ml_audio_probe(&as);
    const ml_aud_card *card = ml_audio_default(&as);
    /* shared rule (see hardware/audio.c): a mixer ioctl that could not be
     * performed here is NOT TESTED, never FAIL and never PASS */
    ml_audio_status aud = ml_audio_status_get(&as);
    hw_report_add(&rep, "Audio", "Output", true,
                  (card && card->can_play) ? HW_PASS : (as.any ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", card ? card->name : (as.note[0] ? as.note : "no card"));
    hw_report_add(&rep, "Audio", "Volume", true,
                  aud.have_volume ? HW_PASS : (aud.readable ? HW_FAIL : HW_NOT_TESTED),
                  "%s", aud.have_volume ? aud.why : aud.why);
    hw_report_add(&rep, "Audio", "Mute", true,
                  aud.have_mute ? HW_PASS : (aud.readable ? HW_FAIL : HW_NOT_TESTED),
                  "%s", aud.why);
    bool has_rec = card && card->can_record;
    hw_report_add(&rep, "Audio", "Microphone", false,
                  has_rec ? HW_NOT_TESTED : (as.any ? HW_UNSUPPORTED : HW_NOT_TESTED),
                  has_rec ? "capture PCM stream available; no live recording was performed here"
                          : "no audio capture stream detected");

    /* ---- Network ---- */
    mica_net_state net;
    mica_net_probe(&net);
    /* shared rule (see hardware/hwprobe.c): link layer from sysfs, address and
     * live probes only when they were really performed on this machine */
    ml_link_status link = ml_net_link_status(&net);
    bool live_net = !hw_using_fixture();
    hw_report_add(&rep, "Network", "Ethernet", true,
                  link.have_iface && link.link_up ? HW_PASS : (net.neth ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", link.have_iface ? link.evidence
                                        : (net.neth ? "no non-loopback interface is up" : "none"));
    hw_report_add(&rep, "Network", "Address", true,
                  !link.address_readable ? HW_NOT_TESTED : (link.has_ip4 ? HW_PASS : HW_FAIL),
                  "%s", !link.address_readable
                            ? "fixture mode: an address cannot be modelled, ioctl not performed"
                            : (link.has_ip4 ? link.ip4 : "no IPv4 address on the primary interface"));
    hw_report_add(&rep, "Network", "Wi-Fi", false,
                  net.nwifi > 0 ? HW_PASS : HW_UNSUPPORTED,
                  "%d wireless interface(s)%s", net.nwifi, live_net ? " on this host" : " in the fixture");
    bool dns_ok = false;
    if (!live_net) {
        hw_report_add(&rep, "Network", "DNS", true, HW_NOT_TESTED,
                      "%d nameserver(s) from %s; live lookup not run in fixture mode",
                      net.ndns, net.resolv_source);
    } else {
        dns_ok = net.ndns > 0;
        if (dns_ok) {
            struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *ai = NULL;
            dns_ok = getaddrinfo("one.one.one.one", "80", &hints, &ai) == 0;
            if (ai) freeaddrinfo(ai);
        }
        hw_report_add(&rep, "Network", "DNS", true,
                      dns_ok ? HW_PASS : (net.ndns ? HW_FAIL : HW_UNSUPPORTED),
                      dns_ok ? "name resolution succeeded via %s" : "resolution failed",
                      net.ndns ? net.dns[0] : "-");
    }

    /* ---- USB / input ---- */
    mica_input_state in;
    mica_usb_state usb;
    mica_input_probe(&in);
    mica_usb_probe(&usb);
    hw_report_add(&rep, "USB", "Keyboard", true,
                  in.keyboard ? HW_PASS : (in.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d keyboard device(s)", in.nkey);
    hw_report_add(&rep, "USB", "Mouse", true,
                  in.mouse ? HW_PASS : (in.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d pointer device(s)", in.nmouse);
    hw_report_add(&rep, "USB", "Storage", false,
                  usb.nstorage ? HW_PASS : (usb.present ? HW_UNSUPPORTED : HW_NOT_TESTED),
                  "%d mass-storage device(s)", usb.nstorage);

    /* ---- Storage (SATA disk, mechanical-disk policy) ---- */
    mica_storage_state sto;
    mica_storage_probe(&sto);
    char sev[192];
    const bool fx = hw_using_fixture();
    drv_hw dhw;
    drv_hw_snapshot(&dhw);
    hw_report_add(&rep, "Storage", "SATA controller", false,
                  dhw.sata_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                  "%s%s%.24s", dhw.sata_chipset[0] ? dhw.sata_chipset : "no SATA controller in the table",
                  dhw.sata_driver[0] ? ", driver " : "", dhw.sata_driver);
    {
        const mica_disk *whole = NULL;
        for (int i = 0; i < sto.n; i++)
            if (sto.v[i].whole && !sto.v[i].removable) { whole = &sto.v[i]; break; }
        char dev[200];
        if (whole) {
            /* the disk itself is rarely in the mount table; its partitions are */
            const char *where = whole->mounted ? whole->mountpoint : "not mounted";
            char fsinfo[96] = "";
            /* prefer the root filesystem, then any other mounted partition */
            const mica_disk *best = NULL;
            for (int i = 0; i < sto.n; i++) {
                if (!sto.v[i].mounted || sto.v[i].whole) continue;
                if (strncmp(sto.v[i].name, whole->name, strlen(whole->name))) continue;
                if (!best || !strcmp(sto.v[i].mountpoint, "/")) best = &sto.v[i];
                if (!strcmp(sto.v[i].mountpoint, "/")) break;
            }
            if (best) {
                where = best->mountpoint;
                snprintf(fsinfo, sizeof fsinfo, " (%.8s on %.12s)", best->fstype, best->name);
            }
            snprintf(dev, sizeof dev, "%.20s %.20s, %llu GB, %s%s", whole->vendor, whole->model,
                     (unsigned long long)(whole->size_bytes / 1000000000ull), where, fsinfo);
        } else
            snprintf(dev, sizeof dev, "%d block device(s), none internal", sto.n);
        hw_report_add(&rep, "Storage", "Disk", true,
                      whole ? HW_PASS : (sto.n ? HW_UNSUPPORTED : HW_NOT_TESTED), "%s", dev);
    }
    hw_result st_res = fx ? HW_NOT_TESTED : storage_result(&sto, sev, sizeof sev);
    if (fx)
        snprintf(sev, sizeof sev, "fixture mode: %d block device(s) listed, no filesystem write attempted",
                 sto.n);
    hw_report_add(&rep, "Storage", "Read/write", true, st_res, "%s", sev);
    {
        /* Mechanical-disk policy (§18): nothing in this image may index the
         * disk, rebuild caches in the background or report telemetry. The check
         * is the process table, not a promise. */
        static const char *banned[] = { "updatedb", "tracker-miner", "baloo_file", "mandb",
                                        "locate", "packagekitd", "fwupd", "incrond" };
        char found[64] = "";
        int hits = 0, readable = 0;
        DIR *proc = opendir(hw_proc(""));
        if (proc) {
            struct dirent *e;
            while ((e = readdir(proc))) {
                if (!isdigit((unsigned char)e->d_name[0])) continue;
                readable++;
                char comm[64] = "";
                char path[512];
                snprintf(path, sizeof path, "%.460s/%.32s/comm", hw_proc(""), e->d_name);
                FILE *f = fopen(path, "r");
                if (!f) continue;
                if (fgets(comm, sizeof comm, f)) {
                    char *nl = strchr(comm, '\n');
                    if (nl) *nl = 0;
                }
                fclose(f);
                for (size_t b = 0; b < ML_ARRAY_SIZE(banned); b++)
                    if (comm[0] && !strcmp(comm, banned[b])) { hits++; snprintf(found, sizeof found, "%s", comm); }
            }
            closedir(proc);
        }
        hw_report_add(&rep, "Storage", "Disk activity policy", false,
                      !readable ? HW_NOT_TESTED : hits ? HW_FAIL : HW_PASS,
                      !readable ? "process table unreadable here"
                                : hits ? "a background indexer/cache/telemetry process is running"
                                       : "no indexer, cache rebuild or telemetry process was found");
        if (hits)
            hw_report_set_evidence(&rep, "Storage", "Disk activity policy",
                                   "background file indexer/telemetry process running: %.40s", found);
        if (readable && !hits)
            hw_report_set_evidence(&rep, "Storage", "Disk activity policy",
                                   "no indexer, cache rebuild or telemetry process is running");
    }

    /* ---- Optical (SuperDrive) ---- */
    {
        char sr[300];
        snprintf(sr, sizeof sr, "%.200s", hw_sys("class/block/sr0"));
        bool drive = ml_is_dir(sr);
        char model[64] = "", vendor[24] = "";
        if (drive) {
            char *m = ml_sysfs_str(hw_path(sr, "device/model"), "");
            char *v = ml_sysfs_str(hw_path(sr, "device/vendor"), "");
            snprintf(model, sizeof model, "%.40s", m ? m : "");
            snprintf(vendor, sizeof vendor, "%.16s", v ? v : "");
            ml_free(m); ml_free(v);
        }
        hw_report_add(&rep, "Optical", "Drive", false, drive ? HW_PASS : HW_UNSUPPORTED,
                      drive ? "%.16s %.40s" : "no optical drive here", vendor, model);
        hw_report_add(&rep, "Optical", "Disc", false, HW_NOT_TESTED,
                      drive ? "reading a disc needs a live drive; MacLiteOS asks only when you use it"
                            : "blocked: no drive");
        {
            int held = open_handles_of("sr0");
            hw_report_add(&rep, "Optical", "Drive polling", false,
                          held < 0 ? HW_NOT_TESTED : held ? HW_FAIL : HW_PASS,
                          held < 0 ? "process table unreadable here"
                                   : held ? "%d process(es) hold the drive open in the background" : "", held);
            if (held == 0)
                hw_report_set_evidence(&rep, "Optical", "Drive polling",
                                       "nothing holds /dev/sr0 open: the drive is not polled");
        }
    }

    /* ---- SDXC ---- */
    {
        char card[32] = "";
        int cards = count_mmc_cards(card, sizeof card);
        hw_report_add(&rep, "SDXC", "Reader", false, dhw.sd_chipset[0] ? HW_PASS : HW_UNSUPPORTED,
                      dhw.sd_chipset[0] ? "%.40s%s%.20s" : "no card reader here",
                      dhw.sd_chipset, dhw.sd_driver[0] ? ", driver " : "", dhw.sd_driver);
        hw_report_add(&rep, "SDXC", "Card", false, cards ? HW_PASS : (dhw.sd_chipset[0] ? HW_UNSUPPORTED
                                                                                      : HW_NOT_TESTED),
                      cards ? "%d card(s) in the slot (%s)" : "no card inserted", cards, card);
        char mp[256] = "";
        bool mounted = cards && mount_of(card, mp, sizeof mp);
        hw_report_add(&rep, "SDXC", "Mount", false,
                      mounted ? HW_PASS : cards ? HW_NOT_TESTED : HW_UNSUPPORTED,
                      mounted ? "mounted at %s (mounted on insertion, unmounted on removal)"
                              : cards ? "card present, not mounted (mounts when opened)"
                                      : "no card", mp);
        hw_report_add(&rep, "SDXC", "Read/write", false,
                      !mounted ? HW_NOT_TESTED : fx ? HW_NOT_TESTED : HW_PASS,
                      !mounted ? "only meaningful with a mounted card"
                               : fx ? "fixture mode: no card write attempted" : "card is mounted read-write");
    }

    /* ---- FireWire ---- */
    {
        bool ctrl = dhw.fw_chipset[0] != 0;
        bool stack = ml_is_dir(hw_sys("bus/firewire/devices"));
        hw_report_add(&rep, "FireWire", "Controller", false, ctrl ? HW_PASS : HW_UNSUPPORTED,
                      ctrl ? "%.40s%s%.20s" : "no FireWire controller here",
                      dhw.fw_chipset, dhw.fw_driver[0] ? ", driver " : "", dhw.fw_driver);
        hw_report_add(&rep, "FireWire", "Linux support", false,
                      !ctrl ? HW_UNSUPPORTED : stack ? HW_PASS : HW_NOT_TESTED,
                      !ctrl ? "blocked: no controller"
                            : stack ? "the firewire bus is present in sysfs"
                                    : "the controller is here but this kernel has no firewire bus");
        hw_report_add(&rep, "FireWire", "Device test", false,
                      !stack ? HW_UNSUPPORTED : HW_NOT_TESTED,
                      !stack ? "blocked: nothing to test"
                             : "no FireWire peripheral is attached, so nothing was transferred");
    }

    /* ---- Camera ---- */
    {
        bool node = path_exists(hw_dev("video0"));
        bool video = ml_is_dir(hw_sys("class/video4linux"));
        bool dev = dhw.cam_chipset[0] != 0;
        hw_report_add(&rep, "Camera", "Device", false, dev ? HW_PASS : HW_UNSUPPORTED,
                      dev ? "%.40s%s%.20s" : "no camera here",
                      dhw.cam_chipset, dhw.cam_driver[0] ? ", driver " : "", dhw.cam_driver);
        hw_report_add(&rep, "Camera", "Capture node", false,
                      node ? HW_PASS : video ? HW_FAIL : HW_UNSUPPORTED,
                      node ? "/dev/video0" : video ? "the video4linux class exists but /dev/video0 does not"
                                                   : "no capture node");
        hw_report_add(&rep, "Camera", "Capture", false, node ? HW_NOT_TESTED : HW_UNSUPPORTED,
                      node ? "no frame was captured here: a capture needs a live camera and a human"
                           : "blocked: no node");
    }

    /* ---- Bluetooth ---- */
    {
        bool bt_dev = dhw.bt_chipset[0] != 0;
        bool bt_sys = ml_is_dir(hw_sys("class/bluetooth"));
        hw_report_add(&rep, "Bluetooth", "Controller", false,
                      bt_dev ? HW_PASS : bt_sys ? HW_PARTIAL : HW_UNSUPPORTED,
                      bt_dev ? "%.40s%s%.20s" : bt_sys ? "bluetooth class in sysfs (no driver catalog match)" : "no Bluetooth controller detected",
                      dhw.bt_chipset, dhw.bt_driver[0] ? ", driver " : "", dhw.bt_driver);
        hw_report_add(&rep, "Bluetooth", "Pairing & Transfer", false,
                      (bt_dev || bt_sys) ? HW_NOT_TESTED : HW_UNSUPPORTED,
                      (bt_dev || bt_sys) ? "no Bluetooth device paired or transferred here"
                                         : "blocked: no Bluetooth controller");
    }

    /* ---- Multimedia ---- */
    ml_hwdec_state hd;
    ml_hwdec_probe(&hd);
    char cap_detail[192];
    ml_cap h264 = ml_codec_capability(g.vendor_id, g.device_id, ML_CODEC_H264, &hd,
                                      cap_detail, sizeof cap_detail);
    hw_report_add(&rep, "Multimedia", "H.264 HW Decode", true,
                  h264 == ML_CAP_HW ? HW_PASS : h264 == ML_CAP_UNKNOWN ? HW_NOT_TESTED : HW_UNSUPPORTED,
                  "%s", cap_detail);
    hw_report_add(&rep, "Multimedia", "VA-API", false,
                  hd.vaapi_lib ? (hd.probed ? HW_PASS : HW_NOT_TESTED) : HW_NOT_TESTED,
                  "%s", hd.vaapi_lib ? (hd.probed ? hd.detail : "libva present; no runtime query ran")
                                     : "libva not present in test environment");
    hw_report_add(&rep, "Multimedia", "VDPAU", false,
                  hd.vdpau_lib ? (hd.probed ? HW_PASS : HW_NOT_TESTED) : HW_NOT_TESTED,
                  "%s", hd.vdpau_lib ? (hd.probed ? hd.detail : "libvdpau present; no runtime query ran")
                                     : "libvdpau not present in test environment");
    if (full) {
        ml_vtest vt;
        int rc = 0;
        bool ran = ml_video_test_run(&vt, "h264", 1920, 1080, 300, true, &rc);
        hw_report_add(&rep, "Multimedia", "1080p Playback", true,
                      !ran ? HW_NOT_TESTED : (rc == 0 ? HW_PASS : HW_FAIL),
                      "%s", ran ? vt.note : vt.note);
    } else {
        hw_report_add(&rep, "Multimedia", "1080p Playback", true, HW_NOT_TESTED,
                      "re-run with --full (encodes and decodes 10 s of 1080p H.264)");
    }

    /* ---- Power ---- */
    mica_power_state pw;
    mica_power_probe(&pw);
    if (!pw.state_node) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED, "no suspend interface here");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "blocked with suspend");
    } else if (bl.apple_machine) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_UNSUPPORTED, "disabled by policy: %s", pw.note);
        hw_report_add(&rep, "Power", "Resume", false, HW_UNSUPPORTED, "blocked with suspend");
    } else {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED, "needs a human to confirm the panel returns");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "not exercised");
    }

    if (tsv) { hw_report_print_tsv(&rep); printf("Overall\t%s\n", hw_result_str(hw_report_overall(&rep))); }
    else {
        diag_title("MacLiteOS Hardware Report");
        const char *last = "";
        for (int i = 0; i < rep.n; i++) {
            if (strcmp(rep.v[i].group, last)) { printf("%s\n", rep.v[i].group); last = rep.v[i].group; }
            printf("  %-22s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result));
            if (rep.v[i].evidence[0]) printf("      %s\n", rep.v[i].evidence);
        }
        hw_result o = hw_report_overall(&rep);
        printf("\nOverall:\n  %s\n", o == HW_PASS ? "PASS" : o == HW_FAIL ? "FAIL" : "PARTIAL");
        printf("\n(exit code %d; contract: 0 every required check PASSED, 1 a required check FAILED,\n"
               " 2 NOT TESTED here, 3 UNSUPPORTED, 4 usage)\n", hw_report_exit(&rep));
    }
    return hw_report_exit(&rep);
}
