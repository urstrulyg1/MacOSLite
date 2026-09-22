#include "hwprobe.h"
#include "ml/util.h"
#include "ml/log.h"
#include "../compositor/proto.h"
#include <dirent.h>

/* small helper: path join without leaking (used only at probe time) */
static char *ml_path_join_tmp(const char *a, const char *b);
static char *joined_keep;
static char *ml_path_join_tmp(const char *a, const char *b)
{
    free(joined_keep);
    joined_keep = ml_path_join(a, b);
    return joined_keep;
}

bool mica_gpu_probe(mica_gpu_info *out)
{
    memset(out, 0, sizeof *out);
    DIR *d = opendir("/sys/bus/pci/devices");
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char base[256];
        snprintf(base, sizeof base, "/sys/bus/pci/devices/%s", e->d_name);
        char *cls = ml_sysfs_str(ml_path_join_tmp(base, "class"), "");
        /* 0x03xxxx = display controller */
        if (strncmp(cls, "0x03", 4) != 0) { ml_free(cls); continue; }
        ml_free(cls);
        out->present = true;
        found = true;
        out->vendor_id = (uint32_t)ml_sysfs_long(ml_path_join_tmp(base, "vendor"), 0);
        out->device_id = (uint32_t)ml_sysfs_long(ml_path_join_tmp(base, "device"), 0);
        char *drv = ml_sysfs_str(ml_path_join_tmp(base, "driver/module"), "");
        snprintf(out->driver, sizeof out->driver, "%s", ml_path_base(drv));
        ml_free(drv);
        /* human readable names for the GPUs this project targets */
        struct { uint32_t vid, did; const char *name; const char *accel; } known[] = {
            { 0x1002, 0x9490, "ATI Radeon HD 4670 (RV730)", "vdpau/uvd2 h.264+mpeg2" },   /* iMac11,2 */
            { 0x1002, 0x68c1, "ATI Radeon HD 5670 (Redwood)", "vdpau/uvd2 h.264+mpeg2" }, /* iMac11,3 */
            { 0x1002, 0x689e, "ATI Radeon HD 5750 (Juniper)", "vdpau/uvd2 h.264+mpeg2" },
            { 0x1002, 0x6741, "AMD Radeon HD 6750M/6770M", "vdpau/uvd3 h.264+mpeg2+vc1" },
            { 0x10de, 0x0a34, "NVIDIA ION (GT218)", "vdpau h.264+mpeg2+vc1" },
            { 0x8086, 0x0046, "Intel HD Graphics (Ironlake)", "vaapi h.264+mpeg2" },
        };
        for (size_t i = 0; i < ML_ARRAY_SIZE(known); i++)
            if (known[i].vid == out->vendor_id && known[i].did == out->device_id) {
                snprintf(out->device, sizeof out->device, "%s", known[i].name);
                snprintf(out->accel, sizeof out->accel, "%s", known[i].accel);
            }
        if (!out->device[0]) {
            char *ven = ml_sysfs_str(ml_path_join_tmp(base, "vendor"), "?");
            char *dev = ml_sysfs_str(ml_path_join_tmp(base, "device"), "?");
            snprintf(out->device, sizeof out->device, "PCI %s:%s", ven, dev);
            ml_free(ven); ml_free(dev);
            snprintf(out->accel, sizeof out->accel, "unknown");
        }
        /* VRAM: sysfs resource sizes are unreliable pre-bind; report 0 unknown */
        out->vram_bytes = 0;
        /* KMS node? */
        char card[64];
        for (int i = 0; i < 8; i++) {
            snprintf(card, sizeof card, "/sys/class/drm/card%d/device", i);
            char *real = realpath(card, NULL);
            if (real) {
                if (strstr(real, e->d_name)) out->has_kms = true;
                free(real);
            }
        }
        break;   /* first display device wins; multi-GPU iMacs use one at a time */
    }
    closedir(d);
    return found;
}

bool mica_cpu_probe(mica_cpu_info *out)
{
    memset(out, 0, sizeof *out);
    char *info = ml_read_file("/proc/cpuinfo", NULL);
    if (!info) return false;
    char *line = strtok(info, "\n");
    uint32_t phys = 0;
    while (line) {
        if (strncmp(line, "model name", 10) == 0) {
            char *v = strchr(line, ':');
            if (v) snprintf(out->model, sizeof out->model, "%s", ml_str_trim(v + 1));
        } else if (strncmp(line, "cpu MHz", 7) == 0) {
            char *v = strchr(line, ':');
            if (v) out->mhz_cur = (uint32_t)atof(v + 1);
        } else if (strncmp(line, "flags", 5) == 0) {
            out->sse42 = strstr(line, "sse4_2") != NULL;
            out->ssse3 = strstr(line, "ssse3") != NULL;
            out->avx = strstr(line, " avx") != NULL;
            out->avx2 = strstr(line, "avx2") != NULL;
        } else if (strncmp(line, "siblings", 8) == 0) {
            char *v = strchr(line, ':');
            if (v) out->threads = (uint32_t)atoi(v + 1);
        } else if (strncmp(line, "cpu cores", 9) == 0) {
            char *v = strchr(line, ':');
            if (v) phys = (uint32_t)atoi(v + 1);
        }
        line = strtok(NULL, "\n");
    }
    out->cores = phys ? phys : ML_MAX(1u, out->threads);
    if (!out->threads) out->threads = ML_MAX(1u, out->cores);
    char *mhz = ml_sysfs_str("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "0");
    out->mhz_max = (uint32_t)(atol(mhz) / 1000);
    ml_free(mhz);
    ml_free(info);
    return true;
}

bool mica_mem_probe(mica_mem_info *out)
{
    memset(out, 0, sizeof *out);
    char *m = ml_read_file("/proc/meminfo", NULL);
    if (!m) return false;
    char *t = strstr(m, "MemTotal:");
    if (t) out->total_kb = strtoull(t + 9, NULL, 10);
    t = strstr(m, "MemAvailable:");
    if (t) out->avail_kb = strtoull(t + 13, NULL, 10);
    ml_free(m);
    return true;
}

uint32_t mica_pick_mode(const mica_gpu_info *g, const mica_cpu_info *c)
{
    /* Rule of thumb calibrated for the 2010 iMac generation (see docs/HARDWARE):
     *   - any r600/radeonsi/ION/Ironlake class GPU with KMS  -> beautiful
     *   - KMS present but unknown accel, or weak CPU        -> balanced
     *   - no KMS / pure software                            -> performance   */
    if (!g->present || !g->has_kms) return MODE_PERFORMANCE;
    if (c && c->cores <= 1) return MODE_BALANCED;
    if (strstr(g->accel, "unknown")) return MODE_BALANCED;
    return MODE_BEAUTIFUL;
}

const char *mica_gpu_accel_name(const mica_gpu_info *g) { return g->accel[0] ? g->accel : "none"; }

double mica_cpu_blend_benchmark_mb_s(void)
{
    /* one 1920x1080 alpha-over pass, timed; honest, small, bounded */
    size_t n = (size_t)1920 * 1080;
    uint32_t *a = ml_alloc(n * 4), *b = ml_alloc(n * 4);
    for (size_t i = 0; i < n; i++) { a[i] = 0x80404040u; b[i] = 0xff102030u; }
    uint64_t t0 = ml_now_ns();
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t s = a[i], d = b[i], al = (s >> 24) + 1;
        uint32_t ia = 256 - al;
        uint32_t rb = (((s & 0x00FF00FFu) * al + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
        uint32_t g = (((s & 0x0000FF00u) * al + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
        acc += rb | g | 0xff000000u;
    }
    double ms = ml_elapsed_ms(t0);
    (void)acc;
    ml_free(a); ml_free(b);
    if (ms <= 0) ms = 0.001;
    return ((double)n * 8.0 / (1024.0 * 1024.0)) / (ms / 1000.0);
}
