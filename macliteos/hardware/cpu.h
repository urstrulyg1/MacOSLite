#ifndef MICA_CPU_H
#define MICA_CPU_H
#include "ml/common.h"

/* Deep CPU layer (spec §1, §6, §7).
 *
 * The compositor only needs cores/threads/ISA (hwprobe.c). *This* module is the
 * one that answers the questions a driver/performance report has to answer:
 * which exact part is installed, is Hyper-Threading on, is Turbo Boost available
 * and enabled, what is the cpufreq stack doing, which microcode revision is
 * loaded versus what the image ships, and what does the thermal hardware say.
 *
 * Two rules from the spec are enforced here:
 *
 *   1. "Do NOT assume which CPU is installed" — every field is read from this
 *      machine. The known-part table below is used only to *cross-check* the
 *      observation (and to say "observed 2 cores, i3-540 has 2" or "MISMATCH"),
 *      never to fill a field in.
 *   2. "Do not install incompatible microcode" — microcode is matched by
 *      family-model-stepping signature. A blob for a different signature is
 *      reported as INCOMPATIBLE and is never selected for loading.
 */

typedef struct {
    /* identity — all from /proc/cpuinfo + CPUID-derived kernel fields */
    char vendor[16];            /* "GenuineIntel" */
    char brand[80];             /* raw model name line */
    char part[40];              /* normalised, e.g. "Intel Core i3-540" */
    uint32_t family, model, stepping;
    uint32_t signature;         /* (family<<8)|(model<<4)|stepping, Intel form */
    char ucode_sig[16];         /* "06-25-05": the file name of this stepping */
    /* topology */
    uint32_t cores, threads, sockets, cores_per_socket;
    bool hyperthreading;        /* htt flag AND threads > cores */
    bool ht_flag;               /* the htt capability bit itself */
    /* clocks (from cpufreq when present, /proc/cpuinfo otherwise) */
    uint32_t mhz_cur, mhz_max, mhz_min, mhz_base;
    bool mhz_from_cpufreq;
    /* ISA that matters on a 2010 Nehalem/Westmere part */
    bool lm64, sse2, ssse3, sse41, sse42, avx, avx2, vmx, aes, pclmulqdq,
         monitor, constant_tsc, est, tsc_deadline, nonstop_tsc, popcnt, movbe;
    char isa[160];              /* human list of the positive flags */
    char isa_absent[80];        /* notable absent ones (AVX on this class) */
    /* cpufreq */
    bool cpufreq;
    char scaling_driver[24];
    char governor[24];
    char governors[128];        /* available_governors, as read */
    bool governor_writable;
    /* Turbo Boost (exposed via cpufreq/boost on this kernel) */
    bool turbo_knob;            /* /sys/.../cpufreq/boost exists */
    bool turbo_enabled;
    bool turbo_capable;         /* the part supports it at all (table cross-check
                                 * or the presence of the knob) */
    /* microcode */
    bool ucode_loaded_known;
    uint32_t ucode_loaded;
    char ucode_file[192];       /* the blob that matches this signature, "" if none */
    bool ucode_file_present;
    uint32_t ucode_available;   /* revision inside that blob */
    bool ucode_available_known;
    uint32_t ucode_date;        /* BCD yyyymmdd of the blob */
    bool ucode_early;           /* the blob is in the initrd path we ship */
    char ucode_dir[160];        /* directory searched (evidence for the report) */
    char ucode_note[256];
    /* thermal */
    bool thermal;
    char thermal_source[128];
    char thermal_label[32];
    int thermal_c;              /* current package/core temperature, -1 unknown */
    int thermal_crit_c;         /* -1 unknown */
    int thermal_trip_max_c;     /* highest trip point seen, -1 unknown */
    /* idle / power */
    bool cpuidle;
    char idle_driver[24];
    int idle_states;
    char idle_names[128];
    /* packaging / platform */
    char dmi_product[48];       /* iMac11,2 */
    char dmi_vendor[32];
    char dmi_board[48];
    char kernel[48];            /* /proc/version first token block */
    char cmdline[256];          /* for the mitigations/acpi_backlight context */
    bool mitigations_off;
    /* one-line verdict inputs */
    char vulnerabilities[192];  /* compact summary of /sys/.../vulnerabilities */
    int vuln_affected, vuln_mitigated;   /* counts, -1 when unreadable */
} ml_cpu_state;

/* Read everything above. Returns false only when /proc/cpuinfo is unreadable —
 * a machine with no cpufreq still returns true with cpufreq=false. */
bool ml_cpu_probe(ml_cpu_state *out);
/* the normalised part name for a brand string: "Intel Core i5-680" */
void ml_cpu_normalise_brand(const char *brand, char out[40]);
/* Cross-check the observed topology against the known part, if it is known.
 * Writes a human explanation into why (never used to overwrite an observation).
 * Returns HW_UNSUPPORTED-style tri-state: PASS match, PARTIAL unknown part,
 * FAIL mismatch. */
typedef enum { ML_CPU_MATCH = 0, ML_CPU_UNKNOWN_PART, ML_CPU_MISMATCH } ml_cpu_xcheck;
ml_cpu_xcheck ml_cpu_crosscheck(const ml_cpu_state *c, char *why, size_t why_len);

/* ---- microcode ------------------------------------------------------- */
/* Fill the microcode fields of `c` (signature-derived filename, blob revision,
 * loaded revision). Separate from ml_cpu_probe so tests can point it at a
 * fixture firmware root. */
void ml_cpu_microcode_probe(ml_cpu_state *c, const char *fw_root);
/* Parse an Intel microcode blob header: revision + date. False when the file
 * does not look like a microcode update (so a random file can never be
 * "verified" as microcode). */
bool ml_ucode_blob_info(const char *path, uint32_t *revision, uint32_t *date,
                        uint32_t *sig_out, uint32_t *expected_sig);
/* Which blob (if any) in fw_root matches this signature. Returns a path via
 * `out`; false when the microcode for this stepping is not in the image. */
bool ml_ucode_find_for(const char *fw_root, uint32_t signature, char *out, size_t outlen,
                       uint32_t *revision, uint32_t *date);
/* Intel microcode file name for a stepping: "06-25-05" for Clarkdale K0. */
void ml_cpu_ucode_filename(uint32_t family, uint32_t model, uint32_t stepping, char out[16]);

/* ---- cpufreq / power ------------------------------------------------- */
/* Write a governor to every CPU. Returns the number of CPUs that accepted it;
 * the caller re-reads to verify (a write is never a result by itself). */
int ml_cpu_set_governor(ml_cpu_state *c, const char *governor);
/* Event-driven turbo toggle: writes cpufreq/boost. Returns true when the value
 * was written AND read back. */
bool ml_cpu_set_turbo(ml_cpu_state *c, bool enabled);
/* The governor MacLiteOS prefers for this part and this kernel, with the reason
 * (spec §7: responsive at idle, no daemon, no polling). */
const char *ml_cpu_recommend_governor(const ml_cpu_state *c, char *why, size_t why_len);
/* Re-read only the live frequency fields (for maclite-cpu / maclite-hardware
 * after a load, or right after wiring a governor). Cheap: a few sysfs reads. */
void ml_cpu_refresh_clocks(ml_cpu_state *c);

#endif /* MICA_CPU_H */
