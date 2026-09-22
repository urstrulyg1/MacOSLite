#ifndef MICA_HWPROBE_H
#define MICA_HWPROBE_H
#include "ml/common.h"

/* Hardware probing used by the installer, the compositor's auto mode picker,
 * System Information and System Diagnostics. Everything is read from /sys and
 * /proc: no daemons, no polling, no udev dependency required. */

typedef struct {
    char vendor[32], device[64], driver[32];
    uint32_t vendor_id, device_id;
    uint64_t vram_bytes;            /* 0 = unknown */
    bool present;
    bool has_kms;                   /* a /dev/dri/cardN bound to this device */
    char accel[64];                 /* "vdpa_u", "vaapi", "none" ... */
    char gl_renderer[64];
} mica_gpu_info;

typedef struct {
    char model[64];
    uint32_t mhz_max, mhz_cur;
    uint32_t cores, threads;
    bool sse42, avx, avx2, ssse3;
} mica_cpu_info;

typedef struct {
    uint64_t total_kb, avail_kb;
} mica_mem_info;

bool mica_gpu_probe(mica_gpu_info *out);
bool mica_cpu_probe(mica_cpu_info *out);
bool mica_mem_probe(mica_mem_info *out);
/* Suggests a performance mode from GPU/CPU capability (spec §9).
 * Returns MODE_* from proto.h (0/1/2). */
uint32_t mica_pick_mode(const mica_gpu_info *g, const mica_cpu_info *c);
const char *mica_gpu_accel_name(const mica_gpu_info *g);
/* quick benchmark: software blend throughput in MB/s of a 1920x1080 RGBA
 * buffer; used as a tie-breaker when the GPU is unknown. */
double mica_cpu_blend_benchmark_mb_s(void);
#endif
