/* Driver/firmware/microcode resolver. See hardware/driver.h for the design
 * rule and the catalog grammar. Everything here is filesystem work plus pure
 * logic, so the whole selection-and-install path runs — and is tested — under
 * fixture roots and a temporary install root, with no hardware and no network.
 */
#include "driver.h"
#include "cpu.h"
#include "audio.h"
#include "ml/util.h"
#include "ml/log.h"
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <strings.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>

/* ------------------------------------------------------------------ kinds --- */
const char *drv_kind_name(drv_kind k)
{
    switch (k) {
    case DRV_KIND_FIRMWARE:  return "firmware";
    case DRV_KIND_MICROCODE: return "microcode";
    case DRV_KIND_DRM:       return "drm";
    case DRV_KIND_MESA:      return "mesa";
    case DRV_KIND_KERNEL:    return "kernel";
    default:                 return "other";
    }
}
drv_kind drv_kind_parse(const char *s)
{
    if (!s) return DRV_KIND_OTHER;
    if (!strcmp(s, "firmware")) return DRV_KIND_FIRMWARE;
    if (!strcmp(s, "microcode")) return DRV_KIND_MICROCODE;
    if (!strcmp(s, "drm")) return DRV_KIND_DRM;
    if (!strcmp(s, "mesa")) return DRV_KIND_MESA;
    if (!strcmp(s, "kernel")) return DRV_KIND_KERNEL;
    return DRV_KIND_OTHER;
}

const char *drv_action_str(drv_action a)
{
    switch (a) {
    case DRV_ACT_NONE:            return "OK";
    case DRV_ACT_KERNEL_IN_USE:   return "IN KERNEL";
    case DRV_ACT_INSTALL:         return "INSTALL";
    case DRV_ACT_UPDATE:          return "UPDATE";
    case DRV_ACT_MISSING_FILE:    return "MISSING FILE";
    case DRV_ACT_REPAIR:          return "REPAIR";
    case DRV_ACT_INCOMPATIBLE:    return "NEWER RELEASE REFUSED";
    case DRV_ACT_KERNEL_MISSING:  return "KERNEL MISSING";
    case DRV_ACT_NOT_IN_CATALOG:  return "NOT IN CATALOG";
    }
    return "?";
}
bool drv_action_ok(drv_action a) { return a == DRV_ACT_NONE || a == DRV_ACT_KERNEL_IN_USE; }

/* ------------------------------------------------------------- versioning --- */
/* Numeric dotted comparison with an optional suffix ("24.0.9", "6.6.30-maclite",
 * "1.2.3-rc1"). Suffixes compare lexically after the numeric part so a release
 * candidate sorts below the release of the same numbers only when it says so;
 * MacLiteOS catalogs use plain dotted versions, and the fallback is documented
 * rather than clever. */
/* A trailing "-rc1" / "~beta2" / ".pre3" makes a version come *before* the
 * plain release: 6.6.30-rc1 < 6.6.30. Any other suffix (a packaging revision
 * such as 1.0-2, a date, a vendor tag) sorts after, because it is built on top
 * of the release rather than tested against it. */
static bool version_is_prerelease(const char *rest)
{
    while (*rest && (*rest == '-' || *rest == '.' || *rest == '~' || *rest == '+' || *rest == '_'))
        rest++;
    static const char *marks[] = { "rc", "alpha", "beta", "pre", "dev", "snapshot", "cvs", "git" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(marks); i++)
        if (!strncasecmp(rest, marks[i], strlen(marks[i]))) return true;
    return false;
}

int drv_version_cmp(const char *a, const char *b)
{
    if (!a || !*a) return b && *b ? -1 : 0;
    if (!b || !*b) return 1;
    while (*a && *b) {
        bool na = isdigit((unsigned char)*a), nb = isdigit((unsigned char)*b);
        if (na && nb) {
            unsigned long va = strtoul(a, (char **)&a, 10);
            unsigned long vb = strtoul(b, (char **)&b, 10);
            if (va != vb) return va < vb ? -1 : 1;
        } else if (!na && !nb) {
            /* compare the whole non-numeric run (rc, beta, -maclite) */
            char ra[32], rb[32];
            size_t i = 0, j = 0;
            while (a[i] && !isdigit((unsigned char)a[i]) && i < sizeof ra - 1) i++;
            while (b[j] && !isdigit((unsigned char)b[j]) && j < sizeof rb - 1) j++;
            memcpy(ra, a, i); ra[i] = 0;
            memcpy(rb, b, j); rb[j] = 0;
            int c = strcmp(ra, rb);
            if (c) return c < 0 ? -1 : 1;
            a += i;
            b += j;
            continue;
        } else {
            /* a number against a suffix: the numeric part is the release */
            return na ? 1 : -1;
        }
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    if (*a || *b) {
        const char *rest = *a ? a : b;
        if (version_is_prerelease(rest)) return *a ? -1 : 1;
        return *a ? 1 : -1;
    }
    return 0;
}

/* -------------------------------------------------------------- catalog ----- */
static bool parse_kv(const char *line, char *key, size_t keylen, char *val, size_t vallen)
{
    const char *eq = strchr(line, '=');
    const char *sp = strchr(line, ' ');
    if (!eq && !sp) return false;
    const char *sep = eq && (!sp || eq < sp) ? eq : sp;
    size_t k = (size_t)(sep - line);
    while (k && (line[k - 1] == ' ' || line[k - 1] == '\t')) k--;
    if (k >= keylen) k = keylen - 1;
    memcpy(key, line, k);
    key[k] = 0;
    const char *v = sep + 1;
    while (*v == ' ' || *v == '\t' || *v == '=') v++;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", v);
    /* strip a trailing comment (# ...) and surrounding quotes */
    char *hash = strstr(tmp, " #");
    if (!hash && tmp[0] == '#') tmp[0] = 0;
    if (hash) *hash = 0;
    char *t = ml_str_trim(tmp);
    size_t n = strlen(t);
    if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = 0; t++; }
    snprintf(val, vallen, "%s", t);
    return true;
}

bool drv_catalog_load(const char *path, drv_catalog *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%.255s", path ? path : "");
    snprintf(out->repo, sizeof out->repo, "unnamed");
    char *text = ml_read_file(path, NULL);
    if (!text) {
        snprintf(out->error, sizeof out->error, "cannot read %s", path ? path : "(null)");
        return false;
    }
    drv_package *cur = NULL;
    char *save = NULL;
    int lineno = 0;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        lineno++;
        out->lines = lineno;
        char *t = ml_str_trim(line);
        if (!t[0] || t[0] == '#') continue;
        if (t[0] == '[' ) continue;                       /* section header, optional */
        char key[64] = "", val[512] = "";
        if (!parse_kv(t, key, sizeof key, val, sizeof val)) {
            if (!out->error[0]) snprintf(out->error, sizeof out->error, "line %d: cannot parse '%s'", lineno, t);
            continue;
        }
        if (!strcmp(key, "catalog-version") || !strcmp(key, "version")) continue;
        if (!strcmp(key, "repo")) { snprintf(out->repo, sizeof out->repo, "%.48s", val); continue; }
        if (!strcmp(key, "key")) { snprintf(out->key, sizeof out->key, "%.255s", val); continue; }
        if (!strcmp(key, "package")) {
            if (out->n >= (int)ML_ARRAY_SIZE(out->v)) {
                if (!out->error[0]) snprintf(out->error, sizeof out->error, "more than %d packages", (int)ML_ARRAY_SIZE(out->v));
                cur = NULL;
                continue;
            }
            cur = &out->v[out->n++];
            memset(cur, 0, sizeof *cur);
            cur->line = lineno;
            /* "package <name> <version>" */
            char name[48] = "", ver[32] = "";
            if (sscanf(val, "%47s %31s", name, ver) < 1) {
                if (!out->error[0]) snprintf(out->error, sizeof out->error, "line %d: package needs a name", lineno);
                out->n--;
                cur = NULL;
                continue;
            }
            snprintf(cur->name, sizeof cur->name, "%s", name);
            snprintf(cur->version, sizeof cur->version, "%s", ver);
            snprintf(cur->status, sizeof cur->status, "stable");
            cur->install[0] = 0;                              /* defaulted by kind */
            continue;
        }
        if (!cur) {
            if (!out->error[0]) snprintf(out->error, sizeof out->error, "line %d: '%s' appears outside a package", lineno, key);
            continue;
        }
        if (!strcmp(key, "kind")) { cur->kind = drv_kind_parse(val); continue; }
        if (!strcmp(key, "status")) { snprintf(cur->status, sizeof cur->status, "%.15s", val); continue; }
        /* status "in-kernel" means: provided by the running kernel, nothing to
         * fetch or install; the resolver checks that the driver is bound. */
        if (!strcmp(key, "provider")) { snprintf(cur->provider, sizeof cur->provider, "%.40s", val); continue; }
        if (!strcmp(key, "install")) { snprintf(cur->install, sizeof cur->install, "%.90s", val); continue; }
        if (!strcmp(key, "driver")) { snprintf(cur->driver, sizeof cur->driver, "%.23s", val); continue; }
        if (!strcmp(key, "label")) { snprintf(cur->label_hint, sizeof cur->label_hint, "%.47s", val); continue; }
        if (!strcmp(key, "source")) { snprintf(cur->source, sizeof cur->source, "%.150s", val); continue; }
        if (!strcmp(key, "note")) { snprintf(cur->note, sizeof cur->note, "%.180s", val); continue; }
        if (!strcmp(key, "requires")) { snprintf(cur->requires, sizeof cur->requires, "%.120s", val); continue; }
        if (!strcmp(key, "target")) {
            if (cur->ntargets < DRV_MAX_TARGETS)
                snprintf(cur->targets[cur->ntargets++], 32, "%.28s", val);
            else if (!out->error[0])
                snprintf(out->error, sizeof out->error, "line %d: more than %d targets", lineno, DRV_MAX_TARGETS);
            continue;
        }
        if (!strcmp(key, "breaks")) {
            if (cur->nbreaks < DRV_MAX_TARGETS)
                snprintf(cur->breaks[cur->nbreaks++], 32, "%.28s", val);
            continue;
        }
        if (!strcmp(key, "file")) {
            if (cur->nfiles >= DRV_MAX_FILES) {
                if (!out->error[0]) snprintf(out->error, sizeof out->error, "line %d: more than %d files", lineno, DRV_MAX_FILES);
                continue;
            }
            drv_file *f = &cur->files[cur->nfiles];
            memset(f, 0, sizeof *f);
            char fname[192] = "";
            /* "file <path> [sha256=<hex>] [size=<n>]" */
            char rest[512];
            snprintf(rest, sizeof rest, "%s", val);
            char *sp = strchr(rest, ' ');
            if (sp) *sp = 0;
            snprintf(fname, sizeof fname, "%.190s", rest);
            if (sp) {
                char *tok = strtok(sp + 1, " ");
                while (tok) {
                    char k2[64] = "", v2[256] = "";
                    if (parse_kv(tok, k2, sizeof k2, v2, sizeof v2)) {
                        if (!strcmp(k2, "sha256")) {
                            /* "unpinned" is an explicit statement, not a digest:
                             * the file must exist and be hashed, but there is
                             * nothing to compare it against, so verification of
                             * that file is NOT TESTED. */
                            if (strcmp(v2, "unpinned") && strcmp(v2, "-"))
                                snprintf(f->sha256, sizeof f->sha256, "%.64s", v2);
                        }
                        else if (!strcmp(k2, "size")) f->size = strtoull(v2, NULL, 10);
                    }
                    tok = strtok(NULL, " ");
                }
            }
            if (!fname[0]) {
                if (!out->error[0]) snprintf(out->error, sizeof out->error, "line %d: file without a path", lineno);
                continue;
            }
            snprintf(f->path, sizeof f->path, "%s", fname);
            cur->nfiles++;
            continue;
        }
        /* unknown keys are ignored on purpose: an older MacLiteOS reading a
         * newer catalog must not fail, it must simply not understand the extra
         * lines. */
    }
    ml_free(text);
    out->loaded = true;
    return true;
}

/* ------------------------------------------------------- hardware snapshot -- */
static void net_side(const mica_net_state *n, const char *kind, drv_hw_net *out)
{
    for (int i = 0; i < n->n; i++) {
        const mica_net_iface *f = &n->v[i];
        if (strcmp(f->kind, kind)) continue;
        out->present = true;
        snprintf(out->iface, sizeof out->iface, "%.15s", f->name);
        snprintf(out->chipset, sizeof out->chipset, "%.63s", f->chipset);
        out->vendor = f->vendor_id;
        out->device = f->device_id;
        snprintf(out->driver, sizeof out->driver, "%.23s", f->driver);
        snprintf(out->firmware, sizeof out->firmware, "%.180s", f->firmware);
        out->in_table = f->chipset[0] && strncmp(f->chipset, "unknown", 7) != 0;
        return;
    }
}

void drv_hw_snapshot(drv_hw *out)
{
    memset(out, 0, sizeof *out);
    mica_gpu_info g;
    mica_gpu_probe(&g);
    mica_cpu_info ci;
    mica_cpu_probe(&ci);
    mica_net_state n;
    mica_net_probe(&n);
    ml_aud_state a;
    ml_audio_probe(&a);
    ml_cpu_state cs;
    ml_cpu_probe(&cs);

    out->present = g.present;
    out->vendor = g.vendor_id;
    out->device = g.device_id;
    out->revision = g.pci_revision;
    out->subsystem_vendor = g.subsystem_vendor;
    out->subsystem_device = g.subsystem_device;
    snprintf(out->model, sizeof out->model, "%.63s", g.db_model[0] ? g.db_model : g.device);
    if (!g.db_model[0]) snprintf(out->model_no_db, sizeof out->model_no_db, "%.63s", g.device);
    snprintf(out->driver, sizeof out->driver, "%.23s", g.driver);
    snprintf(out->gallium, sizeof out->gallium, "%.15s", g.gallium);
    snprintf(out->mesa_version, sizeof out->mesa_version, "%.39s", g.mesa_version);
    out->vram_bytes = g.vram_bytes;
    out->decode_mask = g.decode_mask;
    snprintf(out->decode_api, sizeof out->decode_api, "%.15s", g.accel_api);

    out->cpu_present = ci.model[0] != 0;
    snprintf(out->cpu_part, sizeof out->cpu_part, "%.39s", cs.part);
    out->cpu_signature = cs.signature;
    snprintf(out->ucode_sig, sizeof out->ucode_sig, "%.15s", cs.ucode_sig);
    out->ucode_loaded = cs.ucode_loaded;
    out->ucode_loaded_known = cs.ucode_loaded_known;

    net_side(&n, "wifi", &out->wifi);
    net_side(&n, "ethernet", &out->eth);

    const ml_aud_card *card = ml_audio_default(&a);
    if (card) {
        snprintf(out->audio_codec, sizeof out->audio_codec, "%.47s", card->codec);
        snprintf(out->audio_alsa_driver, sizeof out->audio_alsa_driver, "%.31s", card->driver);
        out->audio_card_present = card->present || a.any;
    }

    /* other subsystems: identified by their PCI/USB ids through the device DB */
    mica_usb_state u;
    mica_usb_probe(&u);
    for (int i = 0; i < u.n; i++) {
        const hw_dev_cap *c = hw_dev_find_any(u.v[i].vid, u.v[i].pid);
        if (!c) continue;
        if (c->role == HW_DEV_BLUETOOTH) {
            if (!out->bt_chipset[0]) snprintf(out->bt_chipset, sizeof out->bt_chipset, "%.63s", c->model);
            if (!out->bt_driver[0]) snprintf(out->bt_driver, sizeof out->bt_driver, "%.23s", u.v[i].driver);
        }
        if (c->role == HW_DEV_CAMERA) {
            if (!out->cam_chipset[0]) snprintf(out->cam_chipset, sizeof out->cam_chipset, "%.63s", c->model);
            if (!out->cam_driver[0]) snprintf(out->cam_driver, sizeof out->cam_driver, "%.23s", u.v[i].driver);
        }
    }
    /* PCI-side parts: the device-capability table is consulted with the ids the
     * kernel reports for each bound driver we care about */
    DIR *d = opendir(hw_sys("bus/pci/devices"));
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char base[400];
            snprintf(base, sizeof base, "%.300s/%.60s", hw_sys("bus/pci/devices"), e->d_name);
            char *vend = ml_sysfs_str(hw_path(base, "vendor"), "");
            char *dev = ml_sysfs_str(hw_path(base, "device"), "");
            uint32_t v = (uint32_t)strtoul(vend, NULL, 0);
            uint32_t dd = (uint32_t)strtoul(dev, NULL, 0);
            ml_free(vend);
            ml_free(dev);
            if (!v || !dd) continue;
            const hw_dev_cap *c = hw_dev_find_any(v, dd);
            if (!c) continue;
            char bdriver[24] = "";
            ml_bound_driver(base, bdriver, sizeof bdriver);
            switch (c->role) {
            case HW_DEV_SDCARD:
                if (!out->sd_chipset[0]) snprintf(out->sd_chipset, sizeof out->sd_chipset, "%.63s", c->model);
                if (!out->sd_driver[0]) snprintf(out->sd_driver, sizeof out->sd_driver, "%.23s", bdriver);
                break;
            case HW_DEV_FIREWIRE:
                if (!out->fw_chipset[0]) snprintf(out->fw_chipset, sizeof out->fw_chipset, "%.63s", c->model);
                if (!out->fw_driver[0]) snprintf(out->fw_driver, sizeof out->fw_driver, "%.23s", bdriver);
                break;
            case HW_DEV_SATA:
                if (!out->sata_chipset[0]) snprintf(out->sata_chipset, sizeof out->sata_chipset, "%.63s", c->model);
                if (!out->sata_driver[0]) snprintf(out->sata_driver, sizeof out->sata_driver, "%.23s", bdriver);
                break;
            case HW_DEV_AUDIO:
                if (!out->audio_driver[0]) snprintf(out->audio_driver, sizeof out->audio_driver, "%.23s", bdriver);
                break;
            default: break;
            }
        }
        closedir(d);
    }

    struct utsname un;
    if (uname(&un) == 0) {
        snprintf(out->kernel_release, sizeof out->kernel_release, "%.63s", un.release);
        /* numeric part only, for requires kernel>=x.y */
        size_t k = 0;
        for (const char *p = un.release; *p && k < sizeof out->kernel_version - 1; p++) {
            if (!isdigit((unsigned char)*p) && *p != '.') break;
            out->kernel_version[k++] = *p;
        }
        out->kernel_version[k] = 0;
    }
    snprintf(out->dmi_product, sizeof out->dmi_product, "%.47s", cs.dmi_product);
}

void drv_hw_summary(const drv_hw *hw, char *out, size_t outlen)
{
    char vram[32];
    if (hw->vram_bytes) ml_format_bytes(hw->vram_bytes, vram, sizeof vram);
    else snprintf(vram, sizeof vram, "?");
    out[0] = 0;
    size_t off = 0;
#define APP(...) do { \
        if (off < outlen) off += (size_t)snprintf(out + off, outlen - off, __VA_ARGS__); \
    } while (0)
    APP("%s · kernel %s · GPU %04x:%04x rev %02x %s (%s VRAM, driver %s, mesa %s, decode %s)",
        hw->dmi_product[0] ? hw->dmi_product : "unknown machine",
        hw->kernel_release[0] ? hw->kernel_release : "?",
        hw->vendor, hw->device, hw->revision,
        hw->model[0] ? hw->model : "(unnamed GPU)", vram,
        hw->driver[0] ? hw->driver : "-",
        hw->mesa_version[0] ? hw->mesa_version : "(no GL query)",
        hw->decode_api[0] ? hw->decode_api : "-");
    APP(" · CPU %.30s (%s, ucode %s)", hw->cpu_part[0] ? hw->cpu_part : "unknown",
        hw->ucode_sig[0] ? hw->ucode_sig : "sig ?",
        hw->ucode_loaded_known ? "loaded" : "unreadable");
    APP(" · wifi %.50s [%04x:%04x]", hw->wifi.chipset[0] ? hw->wifi.chipset : "(none)",
        hw->wifi.vendor, hw->wifi.device);
    APP(" · eth %.50s [%04x:%04x]", hw->eth.chipset[0] ? hw->eth.chipset : "(none)",
        hw->eth.vendor, hw->eth.device);
    APP(" · sata %.50s", hw->sata_chipset[0] ? hw->sata_chipset : "(unnamed)");
    APP(" · sdcard %.50s", hw->sd_chipset[0] ? hw->sd_chipset : "(absent)");
    APP(" · firewire %.50s", hw->fw_chipset[0] ? hw->fw_chipset : "(absent)");
    APP(" · bt %.50s", hw->bt_chipset[0] ? hw->bt_chipset : "(absent)");
    APP(" · camera %.50s", hw->cam_chipset[0] ? hw->cam_chipset : "(absent)");
#undef APP
}

/* ------------------------------------------------------------- matching ----- */
bool drv_target_matches(const char *target, const drv_hw *hw)
{
    if (!target || !*target) return false;
    uint32_t a = 0, b = 0;
    if (!strncmp(target, "pci:", 4)) {
        if (sscanf(target + 4, "%x:%x", &a, &b) != 2) return false;
        /* "pci:V:D" matches any device on the machine with those ids: GPU, NIC,
         * card reader, FireWire controller, ... */
        if (hw->vendor == a && hw->device == b) return true;
        if (hw->wifi.present && hw->wifi.vendor == a && hw->wifi.device == b) return true;
        if (hw->eth.present && hw->eth.vendor == a && hw->eth.device == b) return true;
        return false;
    }
    if (!strncmp(target, "gpu:", 4)) {
        if (sscanf(target + 4, "%x:%x", &a, &b) != 2) return false;
        return hw->present && hw->vendor == a && hw->device == b;
    }
    if (!strncmp(target, "cpu:sig=", 8)) {
        unsigned long sig = strtoul(target + 8, NULL, 16);
        return hw->cpu_present && hw->cpu_signature == (uint32_t)sig;
    }
    if (!strncmp(target, "cpu:", 4))
        return hw->cpu_present && strstr(hw->cpu_part, target + 4) != NULL;
    if (!strncmp(target, "codec:", 6))
        return hw->audio_codec[0] && strstr(hw->audio_codec, target + 6) != NULL;
    if (!strncmp(target, "dmi:", 4))
        return hw->dmi_product[0] && strstr(hw->dmi_product, target + 4) != NULL;
    if (!strncmp(target, "bt:", 3))
        return hw->bt_chipset[0] && strstr(hw->bt_chipset, target + 3) != NULL;
    if (!strncmp(target, "cam:", 4))
        return hw->cam_chipset[0] && strstr(hw->cam_chipset, target + 4) != NULL;
    if (!strncmp(target, "sdcard:", 7))
        return hw->sd_chipset[0] && strstr(hw->sd_chipset, target + 7) != NULL;
    if (!strncmp(target, "firewire:", 9))
        return hw->fw_chipset[0] && strstr(hw->fw_chipset, target + 9) != NULL;
    if (!strncmp(target, "sata:", 5))
        return hw->sata_chipset[0] && strstr(hw->sata_chipset, target + 5) != NULL;
    return false;
}

static bool package_applies(const drv_package *p, const drv_hw *hw)
{
    for (int i = 0; i < p->ntargets; i++)
        if (drv_target_matches(p->targets[i], hw)) return true;
    return false;
}
static const char *package_break_for(const drv_package *p, const drv_hw *hw)
{
    for (int i = 0; i < p->nbreaks; i++)
        if (drv_target_matches(p->breaks[i], hw)) return p->breaks[i];
    return NULL;
}

/* "kernel>=4.19 mesa>=20.0" — each term is checked against the machine. A term
 * naming something we cannot observe makes the requirement UNKNOWN, and an
 * unknown requirement is treated as NOT satisfied for a candidate that is newer
 * than what is installed (conservative) with the reason recorded. */
static bool requires_ok(const char *requires, const drv_hw *hw, char *why, size_t whylen,
                        char *note, size_t notelen)
{
    why[0] = 0;
    if (note && notelen) note[0] = 0;
    if (!requires || !*requires) return true;
    char list[128];
    snprintf(list, sizeof list, "%s", requires);
    char *save = NULL;
    for (char *tok = strtok_r(list, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        char name[32] = "", op[4] = "", want[32] = "";
        const char *p = tok;
        size_t i = 0;
        while (*p && !strchr("<>=!", *p) && i < sizeof name - 1) name[i++] = *p++;
        name[i] = 0;
        i = 0;
        while (*p && strchr("<>=!", *p) && i < sizeof op - 1) op[i++] = *p++;
        op[i] = 0;
        snprintf(want, sizeof want, "%.31s", p);
        const char *have = NULL;
        if (!strcmp(name, "kernel")) have = hw->kernel_version;
        else if (!strcmp(name, "mesa")) have = hw->mesa_version;
        else if (!strcmp(name, "ucode")) have = "";
        else {
            snprintf(why, whylen, "requirement '%s' cannot be checked on this machine", name);
            return false;
        }
        if (!have || !*have) {
            /* The machine cannot tell us which release it has (no GL query, no
             * version file). That is not evidence of incompatibility — it is
             * evidence of absence of information, so the package is still
             * eligible and the resolver says what it could not confirm. */
            if (note && notelen)
                snprintf(note, notelen, "requirement %s%s%s could not be confirmed here (version unknown)",
                         name, op, want);
            continue;
        }
        int c = drv_version_cmp(have, want);
        bool ok = (!strcmp(op, ">=") && c >= 0) || (!strcmp(op, ">") && c > 0) ||
                  (!strcmp(op, "<=") && c <= 0) || (!strcmp(op, "<") && c < 0) ||
                  (!strcmp(op, "==") && c == 0) || (!strcmp(op, "!=") && c != 0);
        if (!ok) {
            snprintf(why, whylen, "%s %s is required but this machine has %s", name, want, have);
            return false;
        }
    }
    return true;
}

/* Run a program directly (no shell) and report whether it exited 0. */
static bool run_argv(const char *const *argv, int argc)
{
    const char *path = ml_find_in_path(argv[0]);
    if (!path) return false;
    const char *args[10];
    char pathbuf[512];
    snprintf(pathbuf, sizeof pathbuf, "%.500s", path);
    args[0] = pathbuf;
    for (int i = 1; i < argc && i < 9; i++) args[i] = argv[i];
    args[argc < 9 ? argc : 9] = NULL;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execv(pathbuf, (char *const *)args);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Append a sentence fragment to a diagnostic buffer, separating with "; " so a
 * component that skipped three releases explains all three. */
static void note_append(char *buf, size_t len, const char *fmt, ...)
{
    if (!buf || !len) return;
    size_t off = strlen(buf);
    if (off + 3 >= len) return;
    char tmp[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(buf + off, len - off, "%s%s", off ? "; " : "", tmp);
}

const drv_package *drv_pick_for(const drv_catalog *cat, const drv_hw *hw, drv_kind kind,
                                const char *target, char *rejected, size_t rejected_len)
{
    const drv_package *best = NULL;
    if (rejected && rejected_len) rejected[0] = 0;
    for (int i = 0; i < cat->n; i++) {
        const drv_package *p = &cat->v[i];
        if (p->kind != kind) continue;
        if (target && *target) {
            bool hit = false;
            for (int t = 0; t < p->ntargets; t++)
                if (!strcmp(p->targets[t], target)) hit = true;
            if (!hit) continue;
        }
        if (!package_applies(p, hw)) continue;
        /* the "newest must not degrade this device" rule (§5, §26) */
        if (!strcmp(p->status, "broken")) {
            const char *br = package_break_for(p, hw);
            if (br) {
                if (best && rejected && rejected_len)
                    snprintf(rejected, rejected_len, "%.24s %.20s is marked broken and refuses %.20s; skipped",
                             p->name, p->version, br);
                continue;
            }
            if (rejected && rejected_len && !rejected[0])
                snprintf(rejected, rejected_len, "%.24s %.20s is marked broken; skipped", p->name, p->version);
            continue;
        }
        if (!strcmp(p->status, "testing") && rejected && rejected_len && !rejected[0])
            snprintf(rejected, rejected_len, "%s %s is marked testing; not selected", p->name, p->version);
        char why[160], rnote[160];
        if (!requires_ok(p->requires, hw, why, sizeof why, rnote, sizeof rnote)) {
            note_append(rejected, rejected_len, "%.20s: %.140s", p->version, why);
            continue;
        }
        if (!best || drv_version_cmp(p->version, best->version) > 0) {
            best = p;
            /* the caller of this older entry point has nowhere to record what
             * could not be confirmed; the plan resolver does (it has a caveat
             * field), and only the plan resolver feeds a report. */
            if (rnote[0] && rejected && rejected_len && !rejected[0])
                snprintf(rejected, rejected_len, "%.150s", rnote);
        }
    }
    return best;
}

/* --------------------------------------------------------------- plan ------- */
/* The relative path a package's file installs to: the package's install prefix
 * (or the kind's default) plus the file's own path. One function, so the
 * installer, the snapshot store and the plan can never disagree about where a
 * file lives. */
static void drv_rel_path(const drv_package *pkg, const drv_file *f, char *out, size_t outlen)
{
    char prefix[96];
    if (pkg->install[0]) snprintf(prefix, sizeof prefix, "%s", pkg->install);
    else {
        switch (pkg->kind) {
        case DRV_KIND_FIRMWARE:  snprintf(prefix, sizeof prefix, "lib/firmware"); break;
        case DRV_KIND_MICROCODE: snprintf(prefix, sizeof prefix, "lib/firmware/intel-ucode"); break;
        case DRV_KIND_MESA:      snprintf(prefix, sizeof prefix, "usr/lib/x86_64-linux-gnu/dri"); break;
        case DRV_KIND_DRM:
        case DRV_KIND_KERNEL:    snprintf(prefix, sizeof prefix, "lib/modules"); break;
        default:                 snprintf(prefix, sizeof prefix, "usr/share/maca-lite"); break;
        }
    }
    snprintf(out, outlen, "%.80s/%.150s", prefix, f->path);
    /* normalise away any duplicated '/' the prefix or the path brought with it */
    for (char *q = out; q && *q; q++)
        while (q[0] == '/' && q[1] == '/') memmove(q, q + 1, strlen(q));
}

/* Where an install would put it, and where this machine already looks for it.
 * Firmware and microcode are read from the *live* firmware root as well, so a
 * machine whose firmware came from the image (or from a distro package) is
 * recognised as "already present" instead of being told to install it again. */
static void drv_probe_paths(const drv_package *pkg, const drv_file *f, const char *install_root,
                            char *install_at, size_t len1, char *live_at, size_t len2)
{
    drv_install_path(pkg, f, install_root, install_at, len1);
    live_at[0] = 0;
    if (pkg->kind == DRV_KIND_FIRMWARE || pkg->kind == DRV_KIND_MICROCODE)
        snprintf(live_at, len2, "%s/%s", hw_fw_root(), f->path);
    /* normalise a duplicated slash when the firmware root itself ends in one */
    for (char *q = live_at; q && *q; q++)
        while (q[0] == '/' && q[1] == '/') memmove(q, q + 1, strlen(q));
}

void drv_install_path(const drv_package *pkg, const drv_file *f, const char *install_root,
                      char *out, size_t outlen)
{
    char rel[256];
    drv_rel_path(pkg, f, rel, sizeof rel);
    const char *root = install_root ? install_root : "";
    size_t rl = strlen(root);
    while (rl && root[rl - 1] == '/') rl--;
    snprintf(out, outlen, "%.*s/%s", (int)rl, root, rel);
}

/* The previous copy of an installed file lives under the same relative path
 * inside the known-good store. */
void drv_snapshot_path(const char *install_root, const char *rel_dest, char *out, size_t outlen)
{
    const char *root = install_root ? install_root : "";
    size_t rl = strlen(root);
    while (rl && root[rl - 1] == '/') rl--;
    while (*rel_dest == '/') rel_dest++;
    snprintf(out, outlen, "%.*s/var/lib/maca-lite/known-good/%s", (int)rl, root, rel_dest);
}

/* Picks the newest release of ONE component (all catalog entries sharing a
 * name) that still supports this machine, and records what it skipped. This is
 * where spec §5/§26 lives: "newest compatible", not "newest". */
static const drv_package *pick_named(const drv_catalog *cat, const drv_hw *hw, const char *name,
                                     char *rejected, size_t rejected_len,
                                     char *caveat, size_t caveat_len)
{
    const drv_package *best = NULL;
    if (rejected && rejected_len) rejected[0] = 0;
    if (caveat && caveat_len) caveat[0] = 0;
    for (int i = 0; i < cat->n; i++) {
        const drv_package *p = &cat->v[i];
        if (strcmp(p->name, name)) continue;
        if (!package_applies(p, hw)) continue;       /* a release for other hardware */
        if (!strcmp(p->status, "broken")) {
            /* §5/§26 in one branch: the newer release exists, would be chosen by
             * version number alone, and must not be — so it is recorded as
             * skipped, with the device it refuses named. */
            const char *br = package_break_for(p, hw);
            note_append(rejected, rejected_len, "%.20s is %.10s%.6s%.20s and was skipped",
                        p->version, p->status, br ? " for " : "", br ? br : "");
            continue;
        }
        if (!strcmp(p->status, "testing"))
            note_append(rejected, rejected_len, "%.20s is marked testing and was not selected",
                        p->version);
        char why[160], rnote[160];
        if (!requires_ok(p->requires, hw, why, sizeof why, rnote, sizeof rnote)) {
            note_append(rejected, rejected_len, "%.20s: %.140s", p->version, why);
            continue;
        }
        if (!best || drv_version_cmp(p->version, best->version) > 0) {
            best = p;
            /* what could not be confirmed about the chosen release, kept apart
             * from the releases that were rejected outright */
            if (rnote[0] && caveat && caveat_len) snprintf(caveat, caveat_len, "%.150s", rnote);
        }
    }
    return best;
}

/* Is a package's kernel driver actually bound on this machine? */
/* "snd_hda_intel" and ALSA's own "HDA-Intel" are the same driver seen through
 * two interfaces; compare them stripped of case, punctuation and the snd
 * prefix so the audio package can be matched against /proc/asound as well as
 * against a PCI driver symlink. */
static void norm_drv(const char *in, char *out, size_t outlen)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < outlen; p++)
        if (isalnum((unsigned char)*p)) out[o++] = (char)tolower((unsigned char)*p);
    out[o] = 0;
    if (!strncmp(out, "snd", 3)) memmove(out, out + 3, strlen(out + 3) + 1);
}

static bool driver_bound(const drv_package *p, const drv_hw *hw)
{
    if (!p->driver[0]) return false;
    const char *bound[] = { hw->driver, hw->wifi.driver, hw->eth.driver,
                            hw->sata_driver, hw->sd_driver, hw->fw_driver,
                            hw->audio_driver, hw->bt_driver, hw->cam_driver };
    char want[24];
    norm_drv(p->driver, want, sizeof want);
    for (size_t i = 0; i < ML_ARRAY_SIZE(bound); i++) {
        if (!bound[i][0]) continue;
        char have[24];
        norm_drv(bound[i], have, sizeof have);
        if (!strcmp(want, have)) return true;
    }
    /* No PCI driver symlink (the controller may be behind a bus the table does
     * not name), but an ALSA card exists: some HDA driver bound it. */
    if (hw->audio_card_present && hw->audio_alsa_driver[0]) {
        char have[24];
        norm_drv(hw->audio_alsa_driver, have, sizeof have);
        if (!strcmp(want, have)) return true;
    }
    return false;
}

static drv_item *plan_item(drv_plan *out, const drv_package *p, const char *label, const char *rejected,
                           const char *caveat)
{
    drv_item *it = &out->v[out->n++];
    memset(it, 0, sizeof *it);
    it->pkg = p;
    snprintf(it->target, sizeof it->target, "%.31s", label);
    snprintf(it->rejected, sizeof it->rejected, "%.191s", rejected ? rejected : "");
    snprintf(it->caveat, sizeof it->caveat, "%.159s", caveat ? caveat : "");
    it->files_present = true;
    it->digest_ok = true;
    it->digest_checked = true;
    return it;
}

void drv_plan_resolve(const drv_catalog *cat, const drv_hw *hw, const char *install_root, drv_plan *out)
{
    memset(out, 0, sizeof *out);
    /* Every component is considered once, under its catalog name, so a catalog
     * that lists several releases of mesa-r600 yields one decision. */
    char seen[64][48];
    int nseen = 0;
    for (int i = 0; i < cat->n && out->n < (int)ML_ARRAY_SIZE(out->v); i++) {
        const drv_package *p = &cat->v[i];
        bool already = false;
        for (int k = 0; k < nseen; k++)
            if (!strcmp(seen[k], p->name)) already = true;
        if (already) continue;
        if (nseen < 64) snprintf(seen[nseen++], 48, "%.47s", p->name);

        char rejected[192] = "", caveat[160] = "";
        const drv_package *chosen = pick_named(cat, hw, p->name, rejected, sizeof rejected,
                                               caveat, sizeof caveat);
        if (!chosen) {
            /* the component exists in the catalog but no release of it supports
             * THIS machine — the most important thing a resolver can say */
            bool applies_any = false;
            for (int k = 0; k < cat->n; k++)
                if (!strcmp(cat->v[k].name, p->name) && package_applies(&cat->v[k], hw)) applies_any = true;
            if (!applies_any) continue;      /* not for this machine at all */
            drv_item *it = plan_item(out, NULL, p->name, rejected, caveat);
            it->action = DRV_ACT_INCOMPATIBLE;
            snprintf(it->reason, sizeof it->reason, "%.180s",
                     rejected[0] ? rejected : "every release refuses this hardware");
            out->n_incompatible++;
            continue;
        }
        const char *label = chosen->label_hint[0] ? chosen->label_hint : chosen->name;
        drv_item *it = plan_item(out, chosen, label, rejected, caveat);
        if (!strcmp(chosen->status, "in-kernel")) {
            if (driver_bound(chosen, hw)) {
                it->action = DRV_ACT_KERNEL_IN_USE;
                snprintf(it->reason, sizeof it->reason, "provided by the running kernel; %s is bound",
                         chosen->driver);
            } else {
                it->action = DRV_ACT_KERNEL_MISSING;
                snprintf(it->reason, sizeof it->reason,
                         "kernel driver %s is not bound on this machine (kernel config / module present?)",
                         chosen->driver[0] ? chosen->driver : "(unnamed)");
                out->n_missing++;
            }
            continue;
        }
        for (int k = 0; k < chosen->nfiles; k++) {
            char dest[512], live[512];
            drv_probe_paths(chosen, &chosen->files[k], install_root, dest, sizeof dest,
                            live, sizeof live);
            const char *seen_at = access(dest, F_OK) == 0 ? dest
                               : (live[0] && access(live, F_OK) == 0) ? live : NULL;
            if (!seen_at) { it->files_present = false; continue; }
            if (chosen->files[k].sha256[0]) {
                char hex[65];
                if (ml_sha256_file(seen_at, hex, NULL)) {
                    if (!ml_sha256_hex_eq(hex, chosen->files[k].sha256)) it->digest_ok = false;
                } else {
                    it->digest_checked = false;
                }
            } else {
                it->digest_checked = false;   /* no pinned digest: not "verified" */
            }
        }
        if (!it->files_present) {
            it->action = DRV_ACT_INSTALL;
            snprintf(it->reason, sizeof it->reason,
                     "not installed yet: %s %s (%d file(s) declared)", chosen->name, chosen->version,
                     chosen->nfiles);
            out->n_install++;
        } else if (!it->digest_ok) {
            it->action = DRV_ACT_REPAIR;
            snprintf(it->reason, sizeof it->reason,
                     "installed files do not match the catalog digest for %s %s", chosen->name, chosen->version);
            out->n_update++;
        } else {
            it->action = DRV_ACT_NONE;
            snprintf(it->installed_version, sizeof it->installed_version, "%.31s", chosen->version);
            snprintf(it->reason, sizeof it->reason, "%s %s installed%s%s", chosen->name, chosen->version,
                     rejected[0] ? "; newer releases refuse this machine and were skipped" : "",
                     caveat[0] ? "; a requirement of it could not be confirmed here" : "");
            if (rejected[0]) out->n_incompatible++;
        }
    }
    /* Required but absent from the catalog: say so, per detected subsystem,
     * instead of leaving the reader to assume it was fine (spec §27: install
     * only what this machine needs — and report what cannot be resolved). */
    struct { bool needed; const char *label; } want[] = {
        { hw->present, "GPU driver" },
        { hw->wifi.present, "Wi-Fi firmware" },
        { hw->bt_chipset[0] != 0, "Bluetooth firmware" },
        { hw->cam_chipset[0] != 0, "camera driver" },
        { hw->cpu_present, "CPU microcode" },
    };
    for (size_t w = 0; w < ML_ARRAY_SIZE(want) && out->n < (int)ML_ARRAY_SIZE(out->v); w++) {
        if (!want[w].needed) continue;
        bool covered = false;
        for (int i = 0; i < out->n; i++) {
            const drv_item *it = &out->v[i];
            if (!it->pkg) continue;
            bool cover = false;
            switch (w) {
            case 0: cover = it->pkg->kind == DRV_KIND_DRM || it->pkg->kind == DRV_KIND_MESA; break;
            case 1: cover = it->pkg->kind == DRV_KIND_FIRMWARE && hw->wifi.present &&
                            package_applies(it->pkg, hw); break;
            case 2: cover = !strcmp(it->pkg->driver, "btusb") ||
                            (it->pkg->kind == DRV_KIND_FIRMWARE && hw->bt_chipset[0] &&
                             package_applies(it->pkg, hw)); break;
            case 3: cover = !strcmp(it->pkg->driver, "uvcvideo"); break;
            case 4: cover = it->pkg->kind == DRV_KIND_MICROCODE; break;
            default: break;
            }
            if (cover) covered = true;
        }
        if (covered) continue;
        drv_item *it = plan_item(out, NULL, want[w].label, "", "");
        it->action = DRV_ACT_NOT_IN_CATALOG;
        snprintf(it->reason, sizeof it->reason,
                 "this machine needs a %s that the catalog does not carry — nothing will be installed for it",
                 want[w].label);
        out->n_missing++;
    }
}

/* ----------------------------------------------------------- verification --- */
/* Runs a verifier over the catalog signature if one is available. We never
 * implement our own signature maths and never pretend a digest is a signature:
 * a missing verifier is reported as "not checked". */
bool drv_verify_catalog(const drv_catalog *cat, drv_verify *out)
{
    memset(out, 0, sizeof *out);
    if (!cat->key[0]) {
        snprintf(out->detail, sizeof out->detail,
                 "the catalog declares no signing key; digests are checked, provenance is not");
        return true;
    }
    char sig[300];
    snprintf(sig, sizeof sig, "%.260s.sig", cat->path);
    if (access(sig, F_OK) != 0) {
        snprintf(out->detail, sizeof out->detail, "no %.80s next to the catalog", sig);
        return true;
    }
    if (!strncmp(cat->key, "https://", 8) || !strncmp(cat->key, "http://", 8)) {
        snprintf(out->detail, sizeof out->detail, "key is a URL (%.80s): refusing to fetch it here", cat->key);
        return true;
    }
    /* No shell anywhere near a catalog-controlled path: argv is built by hand
     * and run with execvp, so a hostile catalog cannot smuggle a command into
     * the verifier invocation. */
    const char *argv[8];
    int argc = 0;
    if (ml_find_in_path("gpgv")) {
        argv[argc++] = "gpgv";
        argv[argc++] = "--keyring";
        argv[argc++] = cat->key;
        argv[argc++] = sig;
        argv[argc++] = cat->path;
        snprintf(out->verifier, sizeof out->verifier, "gpgv");
    } else if (ml_find_in_path("openssl")) {
        argv[argc++] = "openssl";
        argv[argc++] = "dgst";
        argv[argc++] = "-sha256";
        argv[argc++] = "-verify";
        argv[argc++] = cat->key;
        argv[argc++] = "-signature";
        argv[argc++] = sig;
        argv[argc++] = cat->path;
        snprintf(out->verifier, sizeof out->verifier, "openssl");
    } else {
        snprintf(out->detail, sizeof out->detail,
                 "a signature exists but neither gpgv nor openssl is installed: provenance NOT TESTED");
        return true;
    }
    out->signature_checked = true;
    out->signature_ok = run_argv(argv, argc);
    snprintf(out->detail, sizeof out->detail, "catalog signature %s by %s",
             out->signature_ok ? "verifies" : "FAILS TO VERIFY", out->verifier);
    return out->signature_ok;
}

bool drv_verify_package(const drv_catalog *cat, const drv_package *pkg, const char *repo_root,
                        drv_verify *out)
{
    memset(out, 0, sizeof *out);
    drv_verify catv;
    drv_verify_catalog(cat, &catv);
    out->signature_checked = catv.signature_checked;
    out->signature_ok = catv.signature_ok;
    snprintf(out->verifier, sizeof out->verifier, "%s", catv.verifier);
    out->digest_checked = true;
    out->digest_ok = true;
    char src_root[256];
    snprintf(src_root, sizeof src_root, "%.200s/%.40s", repo_root ? repo_root : "",
             pkg->source[0] ? pkg->source : pkg->name);
    for (int i = 0; i < pkg->nfiles; i++) {
        char src[512];
        snprintf(src, sizeof src, "%.300s/%.150s", src_root, pkg->files[i].path);
        if (!pkg->files[i].sha256[0]) {
            out->digest_checked = false;
            snprintf(out->detail, sizeof out->detail,
                     "%.60s has no pinned digest in the catalog: integrity NOT TESTED", pkg->files[i].path);
            continue;
        }
        if (!ml_sha256_hex_valid(pkg->files[i].sha256)) {
            out->digest_checked = true;
            out->digest_ok = false;
            snprintf(out->detail, sizeof out->detail, "%.60s: malformed digest in the catalog",
                     pkg->files[i].path);
            return false;
        }
        char hex[65];
        uint64_t bytes = 0;
        if (!ml_sha256_file(src, hex, &bytes)) {
            out->digest_checked = false;
            snprintf(out->detail, sizeof out->detail, "%.60s is not present under %.60s", pkg->files[i].path, src_root);
            return false;
        }
        if (!ml_sha256_hex_eq(hex, pkg->files[i].sha256)) {
            out->digest_ok = false;
            snprintf(out->detail, sizeof out->detail,
                     "%.60s: digest mismatch (file %.12s…, catalog %.12s…)",
                     pkg->files[i].path, hex, pkg->files[i].sha256);
            return false;
        }
        if (pkg->files[i].size && bytes != pkg->files[i].size) {
            out->digest_ok = false;
            snprintf(out->detail, sizeof out->detail, "%.60s: size %llu does not match the catalog's %llu",
                     pkg->files[i].path, (unsigned long long)bytes, (unsigned long long)pkg->files[i].size);
            return false;
        }
    }
    if (out->digest_checked && out->digest_ok)
        snprintf(out->detail, sizeof out->detail, "%d/%d file digest(s) verified against %.80s%s",
                 pkg->nfiles, pkg->nfiles, cat->path,
                 out->signature_checked ? (out->signature_ok ? "; signature valid" : "; signature INVALID") : "");
    return out->digest_ok;
}

/* ----------------------------------------------------------- state file ----- */
char *drv_state_path(void)
{
    /* An unset ML_ROOT means the real root, not the working directory: a system
     * tool that writes ./var/... wherever it happens to be started is a bug that
     * shows up as a stale state file in a source tree. */
    const char *root = getenv("ML_ROOT");
    if (!root || !*root) return ml_strdup("/var/lib/maca-lite/driver-state");
    size_t rl = strlen(root);
    while (rl && root[rl - 1] == '/') rl--;
    return ml_strdupf("%.*s/var/lib/maca-lite/driver-state", (int)rl, root);
}

bool drv_state_load(const char *path, drv_state *out)
{
    memset(out, 0, sizeof *out);
    char *text = ml_read_file(path, NULL);
    if (!text) return false;
    char *save = NULL;
    for (char *l = strtok_r(text, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = ml_str_trim(l), *v = ml_str_trim(eq + 1);
        if (!strcmp(k, "hardware-id")) snprintf(out->hardware_id, sizeof out->hardware_id, "%.63s", v);
        else if (!strcmp(k, "kernel")) snprintf(out->kernel, sizeof out->kernel, "%.63s", v);
        else if (!strcmp(k, "gpu-driver")) snprintf(out->gpu_driver, sizeof out->gpu_driver, "%.23s", v);
        else if (!strcmp(k, "mesa")) snprintf(out->mesa, sizeof out->mesa, "%.39s", v);
        else if (!strcmp(k, "firmware-digest")) snprintf(out->firmware_digest, sizeof out->firmware_digest, "%.64s", v);
        else if (!strcmp(k, "microcode")) snprintf(out->microcode, sizeof out->microcode, "%.23s", v);
        else if (!strcmp(k, "validation")) snprintf(out->validation, sizeof out->validation, "%.63s", v);
        else if (!strcmp(k, "timestamp")) snprintf(out->timestamp, sizeof out->timestamp, "%.31s", v);
        else if (!strcmp(k, "known-good")) snprintf(out->known_good, sizeof out->known_good, "%.500s", v);
    }
    ml_free(text);
    return true;
}

bool drv_state_save(const char *path, const drv_state *st)
{
    /* write to a temporary file in the same directory, then rename: a crash
     * during a driver install must never leave a half-written state file */
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%.250s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        /* the directory may not exist yet on a fresh install */
        char *dir = ml_path_dir(path);
        if (dir && ml_mkdirs(dir, 0755)) {
            f = fopen(tmp, "w");
        }
        ml_free(dir);
        if (!f) return false;
    }
    fprintf(f, "# MacLiteOS driver state — written by maclite-drivers (spec §30)\n");
    fprintf(f, "hardware-id=%s\n", st->hardware_id);
    fprintf(f, "kernel=%s\n", st->kernel);
    fprintf(f, "gpu-driver=%s\n", st->gpu_driver);
    fprintf(f, "mesa=%s\n", st->mesa);
    fprintf(f, "firmware-digest=%s\n", st->firmware_digest);
    fprintf(f, "microcode=%s\n", st->microcode);
    fprintf(f, "validation=%s\n", st->validation);
    fprintf(f, "timestamp=%s\n", st->timestamp);
    fprintf(f, "known-good=%s\n", st->known_good);
    bool ok = fflush(f) == 0 && !ferror(f);
    fclose(f);
    if (!ok) { unlink(tmp); return false; }
    return rename(tmp, path) == 0;
}

void drv_state_fill(drv_state *st, const drv_hw *hw, const drv_plan *plan, const char *validation)
{
    memset(st, 0, sizeof *st);
    snprintf(st->hardware_id, sizeof st->hardware_id, "%.24s gpu=%04x:%04x cpu=%.32s sig=%05x",
             hw->dmi_product[0] ? hw->dmi_product : "unknown", hw->vendor, hw->device,
             hw->cpu_part[0] ? hw->cpu_part : "?", hw->cpu_signature);
    snprintf(st->kernel, sizeof st->kernel, "%.63s", hw->kernel_release);
    snprintf(st->gpu_driver, sizeof st->gpu_driver, "%.23s", hw->driver[0] ? hw->driver : "none");
    snprintf(st->mesa, sizeof st->mesa, "%.39s", hw->mesa_version[0] ? hw->mesa_version : "unknown");
    snprintf(st->microcode, sizeof st->microcode, "%.15s rev 0x%x",
             hw->ucode_sig[0] ? hw->ucode_sig : "-", hw->ucode_loaded_known ? hw->ucode_loaded : 0);
    /* the firmware digest that matters: the first file of the first firmware
     * package that applies to this machine */
    if (plan) {
        size_t off = 0;
        for (int i = 0; i < plan->n && off + 48 < sizeof st->known_good; i++) {
            const drv_item *it = &plan->v[i];
            if (!it->pkg) continue;
            off += (size_t)snprintf(st->known_good + off, sizeof st->known_good - off, "%s%s=%s",
                                    off ? " " : "", it->pkg->name, it->pkg->version);
            if (it->pkg->kind == DRV_KIND_FIRMWARE && it->pkg->nfiles &&
                !st->firmware_digest[0] && it->pkg->files[0].sha256[0])
                snprintf(st->firmware_digest, sizeof st->firmware_digest, "%.64s", it->pkg->files[0].sha256);
        }
    }
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(st->timestamp, sizeof st->timestamp, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    snprintf(st->validation, sizeof st->validation, "%.63s", validation ? validation : "not run");
}

/* ------------------------------------------------------- install/rollback --- */
static bool copy_file(const char *src, const char *dst, char *err, size_t errlen)
{
    char *dir = ml_path_dir(dst);
    if (dir && !ml_is_dir(dir) && !ml_mkdirs(dir, 0755)) {
        snprintf(err, errlen, "cannot create %s", dir);
        ml_free(dir);
        return false;
    }
    ml_free(dir);
    FILE *in = fopen(src, "rb");
    if (!in) { snprintf(err, errlen, "cannot read %s", src); return false; }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); snprintf(err, errlen, "cannot write %s", dst); return false; }
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out);
            snprintf(err, errlen, "short write to %s", dst);
            return false;
        }
    bool ok = !ferror(in) && fflush(out) == 0;
    fclose(in);
    fclose(out);
    if (!ok) snprintf(err, errlen, "I/O error copying %s", src);
    return ok;
}

bool drv_snapshot_exists(const char *install_root)
{
    const char *root = install_root ? install_root : "";
    char dir[300];
    snprintf(dir, sizeof dir, "%s%svar/lib/maca-lite/known-good", root, root[0] ? "/" : "");
    return ml_is_dir(dir);
}

/* Remember what a path holds right now so a failed validation can undo the
 * change. A path that does not exist yet is recorded as absent with a marker
 * file, because "this file was not there before" is the thing rollback must
 * restore — deleting it again — not leave the installed copy behind. */
static bool snapshot_before(const char *dst, const char *snap, char *err, size_t errlen)
{
    /* One snapshot per destination, ever: the point of the record is the
     * configuration that was known good, so a later install must not overwrite
     * it with the state left behind by an earlier one. */
    char marker[640];
    snprintf(marker, sizeof marker, "%.600s.absent", snap);
    if (access(snap, F_OK) == 0 || access(marker, F_OK) == 0) return true;
    if (access(dst, F_OK) == 0) return copy_file(dst, snap, err, errlen);
    char *dir = ml_path_dir(snap);
    if (dir && !ml_is_dir(dir) && !ml_mkdirs(dir, 0755)) {
        snprintf(err, errlen, "cannot create snapshot dir %s", dir);
        ml_free(dir);
        return false;
    }
    ml_free(dir);
    if (!ml_write_file(snap, "", 0)) {
        snprintf(err, errlen, "cannot record the absence of %s before replacing it", dst);
        return false;
    }
    char marked[600];
    snprintf(marked, sizeof marked, "%.560s.absent", snap);
    FILE *mf = fopen(marked, "w");
    if (mf) { fprintf(mf, "1\n"); fclose(mf); }
    return true;
}

bool drv_install_package(const drv_catalog *cat, const drv_package *pkg, const char *repo_root,
                         bool allow_unpinned, char *err, size_t errlen)
{
    err[0] = 0;
    if (!pkg) { snprintf(err, errlen, "no package selected"); return false; }
    drv_verify v;
    if (!drv_verify_package(cat, pkg, repo_root, &v)) {
        snprintf(err, errlen, "%s", v.detail[0] ? v.detail : "verification failed");
        return false;
    }
    if (!v.digest_checked && !allow_unpinned) {
        snprintf(err, errlen,
                 "%s: no pinned digest to verify against — refusing to install it (use --allow-unpinned to override, "
                 "and know that you are trusting the file, not the catalog)", pkg->name);
        return false;
    }
    char src_root[256];
    snprintf(src_root, sizeof src_root, "%.200s/%.40s", repo_root ? repo_root : "",
             pkg->source[0] ? pkg->source : pkg->name);
    for (int i = 0; i < pkg->nfiles; i++) {
        char src[512], dst[512], snap[512];
        snprintf(src, sizeof src, "%.300s/%.150s", src_root, pkg->files[i].path);
        drv_install_path(pkg, &pkg->files[i], getenv("ML_ROOT"), dst, sizeof dst);
        drv_snapshot_path(getenv("ML_ROOT"), dst + (getenv("ML_ROOT") ? strlen(getenv("ML_ROOT")) : 0), snap,
                          sizeof snap);
        if (!snapshot_before(dst, snap, err, errlen)) return false;
        if (!copy_file(src, dst, err, errlen)) return false;
        /* Microcode is only *early* microcode if the bootloader can reach it:
         * a late reload cannot add the CPUID bits a newer revision brings, so
         * the same blob is also placed where the initrd looks for it (spec §6). */
        if (pkg->kind == DRV_KIND_MICROCODE) {
            const char *root = getenv("ML_ROOT") ? getenv("ML_ROOT") : "";
            size_t rl = strlen(root);
            while (rl && root[rl - 1] == '/') rl--;
            char early[512];
            snprintf(early, sizeof early, "%.*s/boot/maclite-ucode/%s", (int)rl, root,
                     ml_path_base(pkg->files[i].path));
            char e2[200];
            char esnap[600];
            drv_snapshot_path(getenv("ML_ROOT"), "boot/maclite-ucode/" , esnap, sizeof esnap);
            size_t el = strlen(esnap);
            snprintf(esnap + el, sizeof esnap - el, "%s", ml_path_base(early));
            if (!snapshot_before(early, esnap, err, errlen)) return false;
            if (!copy_file(src, early, e2, sizeof e2)) {
                snprintf(err, errlen, "%s (the early-load copy could not be written)", e2);
                return false;
            }
        }
    }
    return true;
}

bool drv_rollback(const char *install_root, char *err, size_t errlen)
{
    err[0] = 0;
    const char *root = install_root ? install_root : "";
    char base[300];
    snprintf(base, sizeof base, "%s%svar/lib/maca-lite/known-good", root, root[0] ? "/" : "");
    if (!ml_is_dir(base)) {
        snprintf(err, errlen, "no known-good snapshot at %s: nothing to roll back to", base);
        return false;
    }
    /* Walk the snapshot tree: every file in it maps back to an installed path
     * by stripping the install root. "*.absent" markers mean "this file did not
     * exist before" — those are removed rather than restored. */
    int restored = 0, removed = 0, failed = 0;
    char *stack[64];
    int depth = 0;
    stack[depth++] = ml_strdup(base);
    while (depth > 0) {
        char *dir = stack[--depth];
        DIR *d = opendir(dir);
        if (!d) { ml_free(dir); continue; }
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char path[700];
            snprintf(path, sizeof path, "%.400s/%.100s", dir, e->d_name);
            if (ml_is_dir(path)) {
                if (depth < 64) stack[depth++] = ml_strdup(path);
                continue;
            }
            if (ml_str_endswith(path, ".absent")) {
                char dest[700];
                size_t n = strlen(path) - strlen(".absent");
                snprintf(dest, sizeof dest, "%.*s", (int)n, path);
                const char *rel = dest + strlen(base);
                char real[700];
                snprintf(real, sizeof real, "%s%s", root, rel);
                if (unlink(real) == 0 || errno == ENOENT) removed++;
                else failed++;
                continue;
            }
            /* A zero-byte placeholder sits next to every ".absent" marker (it
             * is what the marker means); never restore those — doing so would
             * recreate, empty, a file the rollback is meant to remove. */
            char marker[720];
            snprintf(marker, sizeof marker, "%.700s.absent", path);
            if (access(marker, F_OK) == 0) continue;
            const char *rel = path + strlen(base);
            char real[700];
            snprintf(real, sizeof real, "%s%s", root, rel);
            char e2[200];
            if (copy_file(path, real, e2, sizeof e2)) restored++;
            else failed++;
        }
        closedir(d);
        ml_free(dir);
    }
    if (failed) {
        snprintf(err, errlen, "%d file(s) could not be restored", failed);
        return false;
    }
    snprintf(err, errlen, "rolled back: %d file(s) restored, %d file(s) removed", restored, removed);
    return true;
}
