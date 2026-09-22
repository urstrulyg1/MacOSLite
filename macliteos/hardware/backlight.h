#ifndef MICA_BACKLIGHT_H
#define MICA_BACKLIGHT_H
#include "ml/common.h"

/* Backlight / brightness backend (spec §5).
 *
 * Used by maclite-brightness AND by System Settings > Displays so the two can
 * never disagree about what the panel is doing.
 *
 * Design rules:
 *   - a set is only reported OK after the sysfs value has been read back;
 *   - a device that is known to be a no-op on this machine class is flagged
 *     `suspect` and never used silently;
 *   - when nothing real exists we say UNSUPPORTED, we do not emulate.
 */

typedef enum {
    BL_KIND_NONE = 0,
    BL_KIND_GPU_NATIVE,   /* radeon_bl0, amdgpu_bl*, intel_backlight, nv_backlight... */
    BL_KIND_PLATFORM,     /* apple_bl, mbp_nvidia_bl, gmux_backlight, ... */
    BL_KIND_ACPI_VIDEO,   /* acpi_video0: ACPI _BCL — a documented no-op on iMacs in EFI */
    BL_KIND_OTHER,
} bl_kind;
const char *bl_kind_name(bl_kind k);

typedef struct {
    char name[32];
    char path[320];
    bl_kind kind;
    long max;             /* max_brightness; -1 unknown */
    long cur;             /* brightness; -1 unknown */
    long actual;          /* actual_brightness; -1 when the device has none */
    bool writable;
    bool suspect;         /* known not to move the panel on this hardware */
    char note[160];
} bl_device;

typedef enum {
    BL_SET_OK = 0,          /* written and verified by read-back */
    BL_SET_NO_DEVICE,       /* no backlight device at all */
    BL_SET_UNSUPPORTED,     /* device exists but refuses writes (or is suspect) */
    BL_SET_WRITE_FAILED,    /* write() returned an error (permissions, EIO...) */
    BL_SET_VERIFY_FAILED,   /* accepted the write but the value did not stick */
    BL_SET_NOT_TESTED,      /* no write attempted: fixture roots are not hardware */
} bl_set_result;
const char *bl_set_result_str(bl_set_result r);
bool bl_set_ok(bl_set_result r);

typedef struct {
    bl_device dev[8];
    int n;
    int chosen;              /* index into dev, -1 = none usable */
    char dmi_vendor[32], dmi_product[32];
    bool apple_machine;
    bool efi_boot;
    char acpi_backlight_arg[32];   /* value of acpi_backlight= on /proc/cmdline, "" if absent */
    char mechanism[192];           /* exactly which mechanism is in use (docs/gpu.md) */
} bl_state;

void bl_probe(bl_state *st);
const bl_device *bl_active(const bl_state *st);
const bl_device *bl_find(const bl_state *st, const char *name);

int bl_percent_of(const bl_device *d, long raw);
long bl_raw_for(const bl_device *d, int pct);
bool bl_get_percent(const bl_state *st, int *pct);
bl_set_result bl_set_percent(bl_state *st, int pct, int *applied_pct);
bl_set_result bl_step(bl_state *st, int delta_pct, int *applied_pct);
/* Test-only: runs the same code path but does not refuse on fixture roots, so
 * tests/test_hardware.c can pin the read-back rule itself (write/verify/fail)
 * instead of only pinning the refusal. Never called by a tool. */
bl_set_result bl_set_percent_test(bl_state *st, int pct, int *applied_pct);

#endif /* MICA_BACKLIGHT_H */
