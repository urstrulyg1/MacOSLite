#ifndef MICA_DRIVER_H
#define MICA_DRIVER_H
#include "ml/common.h"
#include "ml/sha256.h"
#include "hwcap.h"
#include "hwprobe.h"

/* Driver, firmware and microcode resolver (spec §4, §5, §25, §26, §27, §28, §30).
 *
 * The rule this module exists to enforce:
 *
 *   detect the exact hardware  ->  identify the component that supports it
 *   ->  choose the NEWEST COMPATIBLE version (not the newest)  ->  verify the
 *   digest (and the catalog signature when a verifier exists)  ->  install  ->
 *   validate  ->  remember the known-good configuration so it can be rolled
 *   back.
 *
 * Everything is data-driven from a catalog file (drivers/catalog/NAME.cat) so a
 * support decision is a diffable line rather than a rule buried in C:
 *
 *   package b43-firmware 2015-12-15
 *     kind firmware
 *     status stable
 *     provider linux-firmware
 *     target pci:14e4:4353
 *     install lib/firmware
 *     file b43/ucode29_mimo.fw sha256=<64 hex>
 *     note "Broadcom N-PHY rev 29 ucode for BCM43224"
 *
 * A package that would *drop* support can say so, and the resolver will then
 * pick the newest release that still carries the device:
 *
 *   package mesa-r600 25.0.0
 *     status broken
 *     breaks gpu:1002:9490          # RV730 UVD regression
 *     ...
 *
 * No network access happens in this library: it reads a local trusted repo
 * directory. `maclite-drivers update --online` is the only path that fetches,
 * and it refuses to install anything whose digest or signature does not check.
 */

#define DRV_MAX_FILES   12
#define DRV_MAX_TARGETS 8

typedef enum {
    DRV_KIND_FIRMWARE = 0, DRV_KIND_MICROCODE, DRV_KIND_DRM, DRV_KIND_MESA,
    DRV_KIND_KERNEL, DRV_KIND_OTHER,
} drv_kind;
const char *drv_kind_name(drv_kind k);
drv_kind drv_kind_parse(const char *s);

typedef struct {
    char path[192];         /* relative to the package's install prefix */
    char sha256[65];        /* "" when the catalog does not pin a digest */
    uint64_t size;          /* 0 when not given */
} drv_file;

typedef struct {
    char name[48];
    char version[32];
    drv_kind kind;
    char status[16];        /* stable | testing | broken */
    char provider[48];
    char install[96];       /* prefix, relative to the install root */
    char source[160];       /* directory under the repo, or an https URL */
    char note[192];
    char targets[DRV_MAX_TARGETS][32];
    int ntargets;
    char breaks[DRV_MAX_TARGETS][32];   /* ids this release no longer supports */
    int nbreaks;
    char requires[128];     /* "kernel>=4.19 mesa>=20.0.0" */
    char driver[24];        /* kernel driver this release speaks for ("" = a file set) */
    char label_hint[48];    /* how the machine's report should name this component */
    drv_file files[DRV_MAX_FILES];
    int nfiles;
    int line;               /* 1-based catalog line, for diagnostics */
} drv_package;

typedef struct {
    drv_package v[64];
    int n;
    char path[256];
    char repo[64];
    char key[256];          /* key file referenced by the catalog, "" when none */
    bool loaded;
    char error[192];        /* the first thing that was wrong, verbatim */
    int lines;
} drv_catalog;

bool drv_catalog_load(const char *path, drv_catalog *out);

/* ---- the machine, as the resolver sees it --------------------------- */
typedef struct {
    bool present;
    char iface[16];
    char chipset[64];
    uint32_t vendor, device;
    char driver[24];
    char firmware[192];
    bool firmware_present;
    bool in_table;
} drv_hw_net;

typedef struct {
    bool present;
    uint32_t vendor, device, revision;
    uint32_t subsystem_vendor, subsystem_device;
    char model[64];
    char model_no_db[64];
    char driver[24];
    char gallium[16];
    char mesa_version[40];
    uint64_t vram_bytes;
    uint32_t decode_mask;
    char decode_api[16];
    bool cpu_present;
    char cpu_part[40];
    uint32_t cpu_signature;
    char ucode_sig[16];
    uint32_t ucode_loaded;
    bool ucode_loaded_known;
    drv_hw_net wifi, eth;
    char audio_codec[48];
    char bt_chipset[64];
    char cam_chipset[64];
    char sd_chipset[64];
    char fw_chipset[64];
    char sata_chipset[64];
    /* the driver the kernel has actually bound to each part ("", when the part
     * is absent or unbound) — a package is only "in kernel" if its driver is
     * bound, so this cannot be inferred from the package alone */
    char sata_driver[24], sd_driver[24], fw_driver[24], audio_driver[24],
         bt_driver[24], cam_driver[24];
    char audio_alsa_driver[32];  /* /proc/asound/cards driver id, e.g. "HDA-Intel" */
    bool audio_card_present;     /* an ALSA card exists, i.e. a sound driver is bound */
    char kernel_release[64];   /* uname -r */
    char kernel_version[24];   /* numeric, for >= comparisons */
    char dmi_product[48];
} drv_hw;

void drv_hw_snapshot(drv_hw *out);
/* one-line-per-item human summary, e.g. for the header of a report */
void drv_hw_summary(const drv_hw *hw, char *out, size_t outlen);

/* ---- selection ------------------------------------------------------ */
typedef enum {
    DRV_ACT_NONE = 0,        /* nothing to do: the newest compatible is installed */
    DRV_ACT_KERNEL_IN_USE,   /* provided by the running kernel and bound (nothing to fetch) */
    DRV_ACT_KERNEL_MISSING,  /* the kernel in this image does not provide/bind it */
    DRV_ACT_INSTALL,         /* required and absent */
    DRV_ACT_UPDATE,          /* a newer compatible release exists */
    DRV_ACT_MISSING_FILE,    /* package installed but a declared file is absent */
    DRV_ACT_REPAIR,          /* installed file fails its digest */
    DRV_ACT_INCOMPATIBLE,    /* newest candidate refuses this hardware — newer is skipped */
    DRV_ACT_NOT_IN_CATALOG,  /* this machine needs something the catalog does not carry */
} drv_action;
const char *drv_action_str(drv_action a);
bool drv_action_ok(drv_action a);   /* OK and KERNEL_IN_USE count as satisfied */

typedef struct {
    const drv_package *pkg;      /* chosen (newest compatible) release */
    drv_action action;
    char target[32];             /* the hardware id this item exists for */
    char installed_version[32];  /* "" when not installed */
    char rejected[192];          /* every newer release that was skipped, and why */
    char caveat[160];            /* what could not be confirmed about the chosen one */
    char reason[192];
    bool files_present;
    bool digest_ok;
    bool digest_checked;
} drv_item;

typedef struct {
    drv_item v[32];
    int n;
    int n_install, n_update, n_incompatible, n_missing;
} drv_plan;

void drv_plan_resolve(const drv_catalog *cat, const drv_hw *hw, const char *install_root, drv_plan *out);
/* Does the resolver know this component exists for this machine at all? Used to
 * distinguish "the catalog has no opinion" from "the catalog says unsupported". */
const drv_package *drv_pick_for(const drv_catalog *cat, const drv_hw *hw, drv_kind kind,
                                const char *target, char *rejected, size_t rejected_len);

/* ---- verification ---------------------------------------------------- */
typedef struct {
    bool digest_checked, digest_ok;
    bool signature_checked, signature_ok;
    char verifier[64];           /* the tool that checked the catalog, "" when none */
    char detail[192];
} drv_verify;
/* Checks the catalog's own signature when the catalog names a key and a
 * verifier exists, then every declared file digest. A missing verifier is
 * reported as "not checked" — it is never treated as a pass. */
bool drv_verify_catalog(const drv_catalog *cat, drv_verify *out);
bool drv_verify_package(const drv_catalog *cat, const drv_package *pkg, const char *repo_root,
                        drv_verify *out);

/* ---- install / rollback / state -------------------------------------- */
typedef struct {
    char hardware_id[112];
    char kernel[64];
    char gpu_driver[24];
    char mesa[40];
    char firmware_digest[65];
    char microcode[24];
    char validation[64];
    char timestamp[32];
    char known_good[512];        /* a compact list of name=version pairs */
} drv_state;

char *drv_state_path(void);                     /* $ML_ROOT/var/lib/maca-lite/driver-state */
bool drv_state_load(const char *path, drv_state *out);
bool drv_state_save(const char *path, const drv_state *st);
void drv_state_fill(drv_state *st, const drv_hw *hw, const drv_plan *plan, const char *validation);

/* Installs the files of one package from the local repo into the install root,
 * taking a snapshot of whatever was there first so `rollback` can restore it.
 * Refuses (returns false) when a pinned digest does not match, or when no digest
 * is pinned and allow_unpinned is false. */
bool drv_install_package(const drv_catalog *cat, const drv_package *pkg, const char *repo_root,
                         bool allow_unpinned, char *err, size_t errlen);
/* Restores every file the last install replaced or added. */
bool drv_rollback(const char *install_root, char *err, size_t errlen);
bool drv_snapshot_exists(const char *install_root);

/* the path a package file lands at, given the install root */
void drv_install_path(const drv_package *pkg, const drv_file *f, const char *install_root,
                      char *out, size_t outlen);
/* where the previous version of an installed file is kept for rollback */
void drv_snapshot_path(const char *install_root, const char *rel_dest, char *out, size_t outlen);

int drv_version_cmp(const char *a, const char *b);      /* -1/0/1, dotted numeric */
bool drv_target_matches(const char *target, const drv_hw *hw);
#endif /* MICA_DRIVER_H */
