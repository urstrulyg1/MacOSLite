#include "backlight.h"
#include "hwcap.h"
#include "ml/util.h"
#include "ml/log.h"
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>

const char *bl_kind_name(bl_kind k)
{
    switch (k) {
    case BL_KIND_GPU_NATIVE: return "gpu-native (DRM driver backlight)";
    case BL_KIND_PLATFORM:   return "platform (vendor/SMBus)";
    case BL_KIND_ACPI_VIDEO: return "acpi_video (ACPI _BCL)";
    case BL_KIND_OTHER:      return "other";
    default:                 return "none";
    }
}
const char *bl_set_result_str(bl_set_result r)
{
    switch (r) {
    case BL_SET_OK:            return "OK (verified by read-back)";
    case BL_SET_NO_DEVICE:     return "NO DEVICE (no backlight interface exposed)";
    case BL_SET_UNSUPPORTED:   return "UNSUPPORTED (device refuses writes)";
    case BL_SET_WRITE_FAILED:  return "WRITE FAILED";
    case BL_SET_VERIFY_FAILED: return "VERIFY FAILED (write accepted, value did not stick)";
    case BL_SET_NOT_TESTED:    return "NOT TESTED (fixture roots: no write attempted)";
    default:                   return "?";
    }
}
bool bl_set_ok(bl_set_result r) { return r == BL_SET_OK; }

static bl_kind classify(const char *name)
{
    if (!strncmp(name, "acpi_video", 10)) return BL_KIND_ACPI_VIDEO;
    if (!strncmp(name, "radeon_bl", 9) || !strncmp(name, "amdgpu_bl", 9) ||
        !strncmp(name, "intel_backlight", 15) || !strncmp(name, "nv_backlight", 12) ||
        !strncmp(name, "nouveau_bl", 10) || !strncmp(name, "i915_bl", 7))
        return BL_KIND_GPU_NATIVE;
    if (!strncmp(name, "apple_bl", 8) || !strncmp(name, "mbp_nvidia_bl", 13) ||
        !strncmp(name, "gmux_backlight", 14) || !strncmp(name, "smu-bl", 6) ||
        !strncmp(name, "ddcci", 5))
        return BL_KIND_PLATFORM;
    return BL_KIND_OTHER;
}

static long read_long(const char *dir, const char *file, long def)
{
    char p[360];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    return ml_sysfs_long(p, def);
}
static bool writable(const char *dir, const char *file)
{
    char p[360];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    return access(p, W_OK) == 0;
}

static void read_dmi(bl_state *st)
{
    char *v = ml_sysfs_str(hw_sys("class/dmi/id/sys_vendor"), "");
    snprintf(st->dmi_vendor, sizeof st->dmi_vendor, "%s", v);
    ml_free(v);
    char *p = ml_sysfs_str(hw_sys("class/dmi/id/product_name"), "");
    snprintf(st->dmi_product, sizeof st->dmi_product, "%s", p);
    ml_free(p);
    st->apple_machine = strcasestr(st->dmi_vendor, "Apple") != NULL;
    st->efi_boot = ml_is_dir(hw_sys("firmware/efi"));
    char *cmd = ml_read_file(hw_proc("cmdline"), NULL);
    st->acpi_backlight_arg[0] = 0;
    if (cmd) {
        const char *q = strstr(cmd, "acpi_backlight=");
        if (q) {
            q += strlen("acpi_backlight=");
            size_t i = 0;
            while (*q && *q != ' ' && *q != '\n' && i + 1 < sizeof st->acpi_backlight_arg)
                st->acpi_backlight_arg[i++] = *q++;
            st->acpi_backlight_arg[i] = 0;
        }
        ml_free(cmd);
    }
}

void bl_probe(bl_state *st)
{
    memset(st, 0, sizeof *st);
    st->chosen = -1;
    read_dmi(st);

    DIR *d = opendir(hw_sys("class/backlight"));
    if (!d) {
        snprintf(st->mechanism, sizeof st->mechanism,
                 "none: %s/class/backlight does not exist on this host", hw_sysfs_root());
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) && st->n < (int)ML_ARRAY_SIZE(st->dev)) {
        if (e->d_name[0] == '.') continue;
        bl_device *b = &st->dev[st->n];
        memset(b, 0, sizeof *b);
        snprintf(b->name, sizeof b->name, "%.31s", e->d_name);
        snprintf(b->path, sizeof b->path, "%s", hw_path(hw_sys("class/backlight"), e->d_name));
        b->kind = classify(b->name);
        b->max = read_long(b->path, "max_brightness", -1);
        b->cur = read_long(b->path, "brightness", -1);
        b->actual = read_long(b->path, "actual_brightness", -1);
        b->writable = writable(b->path, "brightness");
        /* Known-bad combination, documented in docs/gpu.md: on Apple iMacs
         * booted through EFI, the ACPI _BCL interface is exposed but drives
         * nothing — the panel is behind an SMBus/I2C backlight controller. */
        if (b->kind == BL_KIND_ACPI_VIDEO && st->apple_machine && st->efi_boot &&
            (!st->acpi_backlight_arg[0] || !strcmp(st->acpi_backlight_arg, "video"))) {
            b->suspect = true;
            snprintf(b->note, sizeof b->note,
                     "acpi_video0 on Apple EFI is a documented no-op; boot with acpi_backlight=native");
        } else if (b->kind == BL_KIND_ACPI_VIDEO) {
            snprintf(b->note, sizeof b->note, "ACPI _BCL interface (acpi_backlight=%s)",
                     st->acpi_backlight_arg[0] ? st->acpi_backlight_arg : "default");
        }
        if (b->max <= 0) snprintf(b->note, sizeof b->note, "%smax_brightness unreadable", b->note[0] ? "; " : "");
        st->n++;
    }
    closedir(d);

    /* preference: gpu-native > platform > acpi_video (never suspect) > other */
    bl_kind pref[] = { BL_KIND_GPU_NATIVE, BL_KIND_PLATFORM, BL_KIND_ACPI_VIDEO, BL_KIND_OTHER };
    for (size_t k = 0; k < ML_ARRAY_SIZE(pref) && st->chosen < 0; k++)
        for (int i = 0; i < st->n; i++)
            if (st->dev[i].kind == pref[k] && !st->dev[i].suspect && st->dev[i].max > 0) {
                st->chosen = i;
                break;
            }
    if (st->chosen < 0) {
        /* only suspect devices exist: report them but refuse to use them */
        for (int i = 0; i < st->n; i++)
            if (st->dev[i].suspect && st->dev[i].max > 0) { st->chosen = i; break; }
    }

    if (st->chosen >= 0) {
        const bl_device *b = &st->dev[st->chosen];
        snprintf(st->mechanism, sizeof st->mechanism, "%s via %s/%s%s",
                 bl_kind_name(b->kind), b->path, "brightness",
                 b->suspect ? " [SUSPECT: not expected to move this panel]" : "");
    } else if (st->n > 0) {
        snprintf(st->mechanism, sizeof st->mechanism,
                 "%d backlight device(s) present but none usable (no readable max_brightness)", st->n);
    } else {
        snprintf(st->mechanism, sizeof st->mechanism,
                 "no backlight device under %s/class/backlight", hw_sysfs_root());
    }
}

const bl_device *bl_active(const bl_state *st) { return st->chosen >= 0 ? &st->dev[st->chosen] : NULL; }

const bl_device *bl_find(const bl_state *st, const char *name)
{
    for (int i = 0; i < st->n; i++) if (!strcmp(st->dev[i].name, name)) return &st->dev[i];
    return NULL;
}

int bl_percent_of(const bl_device *d, long raw)
{
    if (!d || d->max <= 0) return -1;
    long pct = (raw * 100 + d->max / 2) / d->max;
    return (int)ML_CLAMP(pct, 0, 100);
}

long bl_raw_for(const bl_device *d, int pct)
{
    if (!d || d->max <= 0) return -1;
    pct = ML_CLAMP(pct, 0, 100);
    /* never map 0..100 onto 0..max in a way that can switch the panel off from
     * a user "set 0"; keep the lowest usable step at 1 raw unit when max >= 8 */
    long raw = (long)((pct * (double)d->max) / 100.0 + 0.5);
    if (raw < 0) raw = 0;
    if (pct > 0 && raw == 0 && d->max >= 8) raw = 1;
    return raw;
}

bool bl_get_percent(const bl_state *st, int *pct)
{
    const bl_device *b = bl_active(st);
    if (!b || b->cur < 0 || b->max <= 0) return false;
    *pct = bl_percent_of(b, b->cur);
    return true;
}

static bl_set_result write_and_verify(bl_device *b, long raw, int *applied)
{
    char p[400];
    snprintf(p, sizeof p, "%s/brightness", b->path);
    char val[32];
    int n = snprintf(val, sizeof val, "%ld", raw);
    int fd = open(p, O_WRONLY);
    if (fd < 0) {
        b->writable = false;
        snprintf(b->note, sizeof b->note, "open failed: %s", strerror(errno));
        return (errno == EACCES || errno == EPERM) ? BL_SET_UNSUPPORTED : BL_SET_WRITE_FAILED;
    }
    ssize_t w = write(fd, val, (size_t)n);
    int werr = errno;
    close(fd);
    if (w != n) {
        snprintf(b->note, sizeof b->note, "write failed: %s", strerror(werr));
        return BL_SET_WRITE_FAILED;
    }
    long back = read_long(b->path, "brightness", -1);
    b->cur = back;
    long act = read_long(b->path, "actual_brightness", -1);
    if (act >= 0) b->actual = act;
    if (applied) *applied = back >= 0 ? bl_percent_of(b, back) : -1;
    if (back < 0) {
        snprintf(b->note, sizeof b->note, "read-back of %s failed", b->name);
        return BL_SET_VERIFY_FAILED;
    }
    if (back != raw) {
        snprintf(b->note, sizeof b->note, "asked for %ld, sysfs reports %ld", raw, back);
        return BL_SET_VERIFY_FAILED;
    }
    /* actual_brightness lags on some panels; a mismatch is reported, not hidden */
    if (act >= 0 && act != raw)
        snprintf(b->note, sizeof b->note, "brightness=%ld but actual_brightness=%ld (panel may lag)", raw, act);
    else
        b->note[0] = 0;
    return BL_SET_OK;
}

static bl_set_result set_percent_impl(bl_state *st, int pct, int *applied_pct, bool enforce_hardware)
{
    if (st->chosen < 0) { if (applied_pct) *applied_pct = -1; return BL_SET_NO_DEVICE; }
    bl_device *b = &st->dev[st->chosen];
    /* Policy first: a device that is documented not to move the panel is refused
     * whatever the roots are — the refusal is what stops a fake "change". */
    if (b->suspect) {
        if (applied_pct) *applied_pct = bl_percent_of(b, b->cur);
        char nm[32];
        snprintf(nm, sizeof nm, "%s", b->name);
        snprintf(b->note, sizeof b->note, "refusing to fake a change: %s is a documented no-op here", nm);
        return BL_SET_UNSUPPORTED;
    }
    /* Then honest capability: under fixture roots the "device" is a file in a
     * test tree. Writing it and reading it back would produce a perfect
     * verification of nothing, which is exactly the fake this layer exists to
     * prevent, so no write happens and the answer is NOT TESTED. */
    if (enforce_hardware && hw_using_fixture()) {
        if (applied_pct) *applied_pct = bl_percent_of(b, b->cur);
        snprintf(b->note, sizeof b->note,
                 "fixture roots: no write attempted — a fixture file is not panel hardware");
        return BL_SET_NOT_TESTED;
    }
    if (b->max <= 0) { if (applied_pct) *applied_pct = -1; return BL_SET_UNSUPPORTED; }
    return write_and_verify(b, bl_raw_for(b, pct), applied_pct);
}

bl_set_result bl_set_percent(bl_state *st, int pct, int *applied_pct)
{
    return set_percent_impl(st, pct, applied_pct, true);
}

bl_set_result bl_set_percent_test(bl_state *st, int pct, int *applied_pct)
{
    /* test-only hook: exercises write_and_verify() itself, which needs a
     * writable file and is unreachable from the tools under fixture roots */
    return set_percent_impl(st, pct, applied_pct, false);
}

bl_set_result bl_step(bl_state *st, int delta_pct, int *applied_pct)
{
    int pct = 0;
    if (!bl_get_percent(st, &pct)) { if (applied_pct) *applied_pct = -1; return BL_SET_NO_DEVICE; }
    return bl_set_percent(st, ML_CLAMP(pct + delta_pct, 0, 100), applied_pct);
}
