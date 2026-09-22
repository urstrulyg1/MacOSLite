#ifndef MICA_HWCAP_H
#define MICA_HWCAP_H
#include "ml/common.h"

/* MacLiteOS hardware capability layer (v0.2).
 *
 * Two ideas live here and nowhere else:
 *
 *  1. A five-state result vocabulary. "PASS" means the thing was *exercised on
 *     this host and verified*; a check that was never run is NOT_TESTED and can
 *     never be printed as PASS. This is the rule every maclite-* diagnostic
 *     follows, and tests/test_hardware.c asserts it for the exit codes.
 *
 *  2. A capability database + backend chooser. Detection describes the machine;
 *     the chooser picks a backend; the caller falls back down the chain when a
 *     backend is missing. Nothing hardcodes "r600 + vdpau + beautiful".
 */

typedef enum {
    HW_NOT_TESTED = 0,   /* never exercised here — the honest default */
    HW_UNSUPPORTED,      /* no such hardware / no backend available on this host */
    HW_FAIL,             /* exercised, did not work */
    HW_PARTIAL,          /* works, with a documented limitation */
    HW_PASS,             /* exercised and verified */
} hw_result;

const char *hw_result_str(hw_result r);        /* "PASS" / "FAIL" / "UNSUPPORTED" / "NOT TESTED" / "PARTIAL" */
bool hw_result_ok(hw_result r);                /* PASS or PARTIAL */
hw_result hw_result_worst(hw_result a, hw_result b);

/* exit-code contract shared by every diagnostic command (docs/testing.md) */
enum {
    HW_EXIT_PASS        = 0,  /* every required check passed */
    HW_EXIT_FAIL        = 1,  /* a required check failed */
    HW_EXIT_NOT_TESTED  = 2,  /* a required check could not be exercised here */
    HW_EXIT_UNSUPPORTED = 3,  /* the hardware/backend is absent on this host */
    HW_EXIT_USAGE       = 4,  /* bad arguments */
};
int hw_exit_for(hw_result overall);

/* ---- checks with evidence ------------------------------------------- */
typedef struct {
    char group[24];
    char name[34];
    char evidence[160];       /* what was actually read or measured */
    hw_result result;
    bool required;
} hw_check;

typedef struct { hw_check v[96]; int n; } hw_report;

void hw_report_init(hw_report *r);
hw_check *hw_report_add(hw_report *r, const char *group, const char *name, bool required,
                        hw_result res, const char *evidence_fmt, ...) ML_PRINTF_LIKE(6, 7);
void hw_report_note(hw_report *r, const char *group, const char *name,
                    const char *evidence_fmt, ...) ML_PRINTF_LIKE(4, 5);
const hw_check *hw_report_find(const hw_report *r, const char *group, const char *name);
hw_result hw_report_overall(const hw_report *r);       /* PARTIAL when anything is not PASS */
int hw_report_exit(const hw_report *r);
void hw_report_print(const hw_report *r, const char *title);
/* machine readable, one line per check: group|name|RESULT|required|evidence */
void hw_report_print_tsv(const hw_report *r);

/* ---- codecs ---------------------------------------------------------- */
enum {
    ML_CODEC_H264   = 1u << 0,
    ML_CODEC_MPEG2  = 1u << 1,
    ML_CODEC_MPEG4  = 1u << 2,   /* MPEG-4 Part 2 (DivX/Xvid era) */
    ML_CODEC_VC1    = 1u << 3,
    ML_CODEC_HEVC   = 1u << 4,
    ML_CODEC_VP9    = 1u << 5,
    ML_CODEC_AV1    = 1u << 6,
    ML_CODEC_MJPEG  = 1u << 7,
};
const char *ml_codec_name(uint32_t bit);
uint32_t ml_codec_bit(const char *name);              /* 0 when unknown */
/* human list, e.g. "H.264 MPEG-2 VC-1" */
void ml_codec_list(uint32_t mask, char *out, size_t outlen);

/* ---- GPU capability database ----------------------------------------- */
typedef struct {
    uint32_t vendor, device;
    const char *model;
    const char *kdriver;      /* kernel driver that binds this PCI id */
    const char *gallium;      /* mesa driver name (r600/radeonsi/i965/nouveau...) */
    uint32_t gl_ver_x10;      /* 33 == OpenGL 3.3 */
    uint32_t decode;          /* ML_CODEC_* the fixed-function block really has */
    const char *decode_api;   /* "vdpau" / "vaapi" / "none" */
    const char *note;         /* known-good/known-bad notes for this part */
} hw_gpu_cap;

const hw_gpu_cap *hw_gpu_lookup(uint32_t vendor, uint32_t device);
size_t hw_gpu_db_size(void);
const hw_gpu_cap *hw_gpu_db_at(size_t i);

/* ---- backend selection ----------------------------------------------- */
typedef enum { ML_PRESENT_HEADLESS = 0, ML_PRESENT_KMS, ML_PRESENT_FBDEV } ml_present_kind;
const char *ml_present_name(ml_present_kind k);
/* requested is "auto"|"kms"|"fbdev"|"headless" (NULL == auto). Returns the
 * first backend that can actually be opened, and *why in reason (may be NULL). */
ml_present_kind hw_pick_present(const char *requested, char *reason, size_t reason_len);

typedef enum { ML_HWDEC_NONE = 0, ML_HWDEC_VDPAU, ML_HWDEC_VAAPI, ML_HWDEC_AUTO } ml_hwdec;
const char *ml_hwdec_name(ml_hwdec h);
ml_hwdec hw_pick_hwdec(uint32_t vendor, uint32_t device, bool render_node_present,
                       char *reason, size_t reason_len);

/* performance mode picked from real capability, not from a model string */
uint32_t hw_pick_mode_caps(bool kms_ok, bool hw_render, uint32_t cores, uint32_t ram_mb,
                           char *reason, size_t reason_len);

/* ---- sysfs/procfs root overrides (used by tests/test_hardware.c) ------- */
const char *hw_sysfs_root(void);     /* $ML_SYSFS_ROOT or "/sys"   */
const char *hw_proc_root(void);      /* $ML_PROC_ROOT  or "/proc"  */
const char *hw_dev_root(void);       /* $ML_DEV_ROOT   or "/dev"   */
const char *hw_etc_root(void);       /* $ML_ETC_ROOT   or "/etc"   */
/* Join a root with a relative path. Returns a pointer into a small rotating
 * ring of static buffers, so use it immediately and never free it. Probes only
 * run at startup or on explicit user action, never in the render path. */
const char *hw_path(const char *root, const char *rel);
const char *hw_sys(const char *rel);
const char *hw_proc(const char *rel);
const char *hw_dev(const char *rel);
const char *hw_etc(const char *rel);
bool hw_using_fixture(void);         /* true when any root override is set */

#endif /* MICA_HWCAP_H */
