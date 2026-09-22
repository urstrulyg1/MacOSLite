#include "hwcap.h"
#include "ml/log.h"
#include <stdarg.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>

/* ---------------------------------------------------------------- results -- */
const char *hw_result_str(hw_result r)
{
    switch (r) {
    case HW_PASS:        return "PASS";
    case HW_PARTIAL:     return "PARTIAL";
    case HW_FAIL:        return "FAIL";
    case HW_UNSUPPORTED: return "UNSUPPORTED";
    default:             return "NOT TESTED";
    }
}
bool hw_result_ok(hw_result r) { return r == HW_PASS || r == HW_PARTIAL; }

hw_result hw_result_worst(hw_result a, hw_result b)
{
    /* ordering: NOT_TESTED and UNSUPPORTED are "unknown-ish" but must never be
     * swallowed by a PASS; FAIL outranks both. */
    static const int rank[] = { 1 /*NOT_TESTED*/, 1 /*UNSUPPORTED*/, 3 /*FAIL*/, 2 /*PARTIAL*/, 0 /*PASS*/ };
    return rank[a] >= rank[b] ? a : b;
}

int hw_exit_for(hw_result overall)
{
    switch (overall) {
    case HW_PASS:        return HW_EXIT_PASS;
    case HW_PARTIAL:     return HW_EXIT_NOT_TESTED;
    case HW_FAIL:        return HW_EXIT_FAIL;
    case HW_UNSUPPORTED: return HW_EXIT_UNSUPPORTED;
    default:             return HW_EXIT_NOT_TESTED;
    }
}

/* ---------------------------------------------------------------- report --- */
void hw_report_init(hw_report *r) { r->n = 0; }

hw_check *hw_report_add(hw_report *r, const char *group, const char *name, bool required,
                        hw_result res, const char *fmt, ...)
{
    if (r->n >= (int)ML_ARRAY_SIZE(r->v)) return NULL;
    hw_check *c = &r->v[r->n++];
    memset(c, 0, sizeof *c);
    snprintf(c->group, sizeof c->group, "%s", group);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->result = res;
    c->required = required;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c->evidence, sizeof c->evidence, fmt, ap);
        va_end(ap);
    }
    return c;
}

hw_check *hw_report_set_evidence(hw_report *r, const char *group, const char *name,
                                 const char *fmt, ...)
{
    for (int i = 0; i < r->n; i++) {
        hw_check *c = &r->v[i];
        if (strcmp(c->group, group) || strcmp(c->name, name)) continue;
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(c->evidence, sizeof c->evidence, fmt, ap);
            va_end(ap);
        }
        return c;
    }
    return NULL;
}

void hw_report_note(hw_report *r, const char *group, const char *name, const char *fmt, ...)
{
    /* an observation that is not a pass/fail claim; recorded as NOT_TESTED so
     * it can never be mistaken for verification */
    char ev[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev, sizeof ev, fmt, ap);
    va_end(ap);
    hw_report_add(r, group, name, false, HW_NOT_TESTED, "%s", ev);
}

const hw_check *hw_report_find(const hw_report *r, const char *group, const char *name)
{
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->v[i].group, group) && !strcmp(r->v[i].name, name)) return &r->v[i];
    return NULL;
}

hw_result hw_report_overall(const hw_report *r)
{
    hw_result worst = HW_PASS;
    for (int i = 0; i < r->n; i++)
        if (r->v[i].required) worst = hw_result_worst(worst, r->v[i].result);
    return worst;
}

int hw_report_exit(const hw_report *r) { return hw_exit_for(hw_report_overall(r)); }

void hw_report_print(const hw_report *r, const char *title)
{
    const char *last_group = "";
    printf("%s\n", title);
    for (size_t i = 0; i < strlen(title); i++) putchar('=');
    putchar('\n');
    for (int i = 0; i < r->n; i++) {
        const hw_check *c = &r->v[i];
        if (strcmp(c->group, last_group)) { printf("\n%s\n", c->group); last_group = c->group; }
        printf("  %-28s %-10s%s\n", c->name, hw_result_str(c->result), c->required ? "" : " (optional)");
        if (c->evidence[0]) printf("      %s\n", c->evidence);
    }
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(r)));
}

void hw_report_print_tsv(const hw_report *r)
{
    for (int i = 0; i < r->n; i++) {
        const hw_check *c = &r->v[i];
        printf("%s\t%s\t%s\t%s\t%s\n", c->group, c->name, hw_result_str(c->result),
               c->required ? "required" : "optional", c->evidence);
    }
}

/* ----------------------------------------------------------------- codecs -- */
static const struct { uint32_t bit; const char *name; } codecs[] = {
    { ML_CODEC_H264,  "H.264"  }, { ML_CODEC_MPEG2, "MPEG-2" },
    { ML_CODEC_MPEG4, "MPEG-4" }, { ML_CODEC_VC1,   "VC-1"   },
    { ML_CODEC_HEVC,  "HEVC"   }, { ML_CODEC_VP9,   "VP9"    },
    { ML_CODEC_AV1,   "AV1"    }, { ML_CODEC_MJPEG, "MJPEG"  },
};
const char *ml_codec_name(uint32_t bit)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(codecs); i++) if (codecs[i].bit == bit) return codecs[i].name;
    return "?";
}
/* "H.264", "h264", "H_264", "MPEG-2" all mean the same codec: normalise away
 * the punctuation and the case before comparing. */
static void normalise(const char *in, char *out, size_t outlen)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outlen; i++) {
        char c = in[i];
        if (c == '.' || c == '-' || c == '_' || c == ' ') continue;
        out[j++] = (char)tolower((unsigned char)c);
    }
    out[j] = 0;
}
uint32_t ml_codec_bit(const char *name)
{
    if (!name) return 0;
    char want[32];
    normalise(name, want, sizeof want);
    for (size_t i = 0; i < ML_ARRAY_SIZE(codecs); i++) {
        char have[32];
        normalise(codecs[i].name, have, sizeof have);
        if (!strcmp(have, want)) return codecs[i].bit;
    }
    if (!strcmp(want, "h265")) return ML_CODEC_HEVC;
    if (!strcmp(want, "mpeg4p2") || !strcmp(want, "divx") || !strcmp(want, "xvid") ||
        !strcmp(want, "mpeg4part2")) return ML_CODEC_MPEG4;
    if (!strcmp(want, "mp4")) return ML_CODEC_MPEG4;
    if (!strcmp(want, "vc1")) return ML_CODEC_VC1;
    return 0;
}
void ml_codec_list(uint32_t mask, char *out, size_t outlen)
{
    out[0] = 0;
    for (size_t i = 0; i < ML_ARRAY_SIZE(codecs); i++) {
        if (!(mask & codecs[i].bit)) continue;
        size_t n = strlen(out);
        snprintf(out + n, outlen - n, "%s%s", n ? " " : "", codecs[i].name);
    }
    if (!out[0]) snprintf(out, outlen, "none");
}

/* ------------------------------------------------------- capability DB ----- */
/* Silicon capability only. What the *driver* on a given boot actually exposes
 * is probed separately (vdpauinfo / vainfo / render node) and intersected with
 * this table by media.c — the table alone never produces a PASS. */
static const hw_gpu_cap gpu_db[] = {
    /* --- the machines MacLiteOS targets: iMac Mid-2010 family --- */
    { 0x1002, 0x9490, "ATI Radeon HD 4670 (RV730)", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_VC1, "vdpau",
      "iMac11,2 21.5\". UVD2. Mesa r600 VDPAU exposes H.264+MPEG-2; VC-1 is driver-dependent." },
    { 0x1002, 0x9498, "ATI Radeon HD 4670 (RV730, secondary)", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2, "vdpau", "UVD2" },
    { 0x1002, 0x68c1, "ATI Radeon HD 5670 (Redwood)", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_MPEG4 | ML_CODEC_VC1, "vdpau",
      "iMac11,3 27\". UVD2.2 adds MPEG-4 Part 2." },
    { 0x1002, 0x68c9, "ATI Radeon HD 5670 (Redwood, alt id)", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_MPEG4 | ML_CODEC_VC1, "vdpau", "UVD2.2" },
    { 0x1002, 0x689e, "ATI Radeon HD 5750 (Juniper)", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_MPEG4 | ML_CODEC_VC1, "vdpau",
      "iMac11,3 27\" option. UVD2.2." },
    { 0x1002, 0x6741, "AMD Radeon HD 6750M/6770M", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_MPEG4 | ML_CODEC_VC1, "vdpau",
      "iMac12,x. UVD3." },
    { 0x1002, 0x6720, "AMD Radeon HD 6970M", "radeon", "r600", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_MPEG4 | ML_CODEC_VC1, "vdpau", "iMac12,2. UVD3." },
    /* --- neighbouring Mac generations kept so the DB is not single-machine --- */
    { 0x10de, 0x0861, "NVIDIA GeForce 9400M (MCP79)", "nouveau", "nouveau", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_VC1, "vdpau", "iMac9,1/10,1. VP2." },
    { 0x10de, 0x0a34, "NVIDIA ION / GT 218", "nouveau", "nouveau", 33,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_VC1, "vdpau", "VP2/3." },
    { 0x8086, 0x0046, "Intel HD Graphics (Ironlake)", "i915", "i965", 21,
      ML_CODEC_H264 | ML_CODEC_MPEG2, "vaapi", "Clarkdale IGP; not wired to the panel in iMacs." },
    { 0x8086, 0x0166, "Intel HD 4000 (Ivy Bridge)", "i915", "i965", 40,
      ML_CODEC_H264 | ML_CODEC_MPEG2 | ML_CODEC_VC1, "vaapi", "" },
    /* --- virtual GPUs, so a VM boot reports honestly instead of guessing --- */
    { 0x1af4, 0x1010, "virtio-gpu (QEMU/KVM)", "virtio_gpu", "virtio", 0,
      0, "none", "no fixed-function decode; host-side virgl only." },
    { 0x1af4, 0x1050, "virtio-gpu (modern id)", "virtio_gpu", "virtio", 0,
      0, "none", "no fixed-function decode." },
    { 0x1234, 0x1111, "QEMU Standard VGA (bochs)", "bochs-drm", "softpipe", 0,
      0, "none", "software rendering only." },
    { 0x15ad, 0x0405, "VMware SVGA II", "vmwgfx", "svga", 33,
      0, "none", "no decode." },
};

const hw_gpu_cap *hw_gpu_lookup(uint32_t vendor, uint32_t device)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(gpu_db); i++)
        if (gpu_db[i].vendor == vendor && gpu_db[i].device == device) return &gpu_db[i];
    return NULL;
}
size_t hw_gpu_db_size(void) { return ML_ARRAY_SIZE(gpu_db); }
const hw_gpu_cap *hw_gpu_db_at(size_t i) { return i < ML_ARRAY_SIZE(gpu_db) ? &gpu_db[i] : NULL; }

/* ------------------------------------------------- non-GPU device table ---- */
/* Real IDs for the parts MacLiteOS targets, plus the near neighbours whose
 * driver/firmware situation a user is likely to hit. "firmware" is what has to
 * exist under /lib/firmware; an empty string means the driver needs no file
 * (the firmware is either in the driver or uploaded by the driver itself). */
static const hw_dev_cap dev_db[] = {
    /* --- Wi-Fi: identify the exact chipset before choosing a driver (§12) --- */
    { 0x14e4, 0x4353, HW_DEV_WIFI, "BCM43224 AirPort Extreme (802.11a/b/g/n)", "b43",
      "b43/ucode29_mimo.fw b43/ucode29_mimo_ps.fw b43/pcm5.fw",
      "PCIe N-PHY rev 29. brcmfmac does NOT drive this part; b43 needs external firmware, "
      "or the out-of-tree wl driver (whose code is closed). MacLiteOS ships b43 + the three files." },
    { 0x14e4, 0x432b, HW_DEV_WIFI, "BCM4322 AirPort Extreme (802.11a/b/g/n)", "b43",
      "b43/ucode29_mimo.fw b43/ucode29_mimo_ps.fw b43/pcm5.fw", "Same b43 family as the BCM43224." },
    { 0x14e4, 0x4358, HW_DEV_WIFI, "BCM43224/BCM43225 combo (as shipped on some iMacs)", "b43",
      "b43/ucode29_mimo.fw b43/ucode29_mimo_ps.fw b43/pcm5.fw", "" },
    { 0x14e4, 0x43a0, HW_DEV_WIFI, "BCM4360 (much later Macs)", "brcmfmac",
      "brcm/brcmfmac43602-pcie.bin", "Newer chip; the opposite trap: this one *wants* brcmfmac." },
    { 0x168c, 0x002e, HW_DEV_WIFI, "Atheros AR9287 (some 2009-2011 iMacs)", "ath9k", "",
      "No firmware file needed; ath9k is in-tree and free." },
    /* --- Ethernet ---------------------------------------------------------- */
    { 0x14e4, 0x1684, HW_DEV_ETHERNET, "BCM5764M Gigabit Ethernet", "tg3", "",
      "10/100/1000BASE-T. tg3 needs no firmware file on this part; it negotiates the link itself." },
    { 0x14e4, 0x1682, HW_DEV_ETHERNET, "BCM57762 Gigabit Ethernet", "tg3", "tigon/tg3_tso5.bin",
      "The 5776x generation wants TSO firmware for transmit offload (optional, not for the link)." },
    { 0x14e4, 0x16bc, HW_DEV_SDCARD, "BCM57765/57785 SDXC/MMC card reader", "sdhci-pci", "",
      "The SDXC slot. Cards appear as /dev/mmcblk0; mounting is done on demand." },
    /* --- Bluetooth: Apple's own controller over USB ------------------------- */
    { 0x05ac, 0x8215, HW_DEV_BLUETOOTH, "Apple Bluetooth 2.1+EDR (BC2046B1)", "btusb", "",
      "No .hcd file: btusb uploads the patch over the USB control endpoint itself." },
    { 0x05ac, 0x8218, HW_DEV_BLUETOOTH, "Apple Bluetooth 2.1 (BCM2046)", "btusb", "", "" },
    { 0x05ac, 0x8286, HW_DEV_BLUETOOTH, "Apple Bluetooth 4.0 (BCM20702)", "btusb",
      "brcm/BCM20702A1-05ac-8286.hcd", "This one *does* need a .hcd patch file." },
    /* --- Camera: the built-in iSight -------------------------------------- */
    { 0x05ac, 0x8502, HW_DEV_CAMERA, "Built-in iSight (UVC)", "uvcvideo", "",
      "UVC class complaint: no proprietary software, no firmware download." },
    { 0x05ac, 0x8507, HW_DEV_CAMERA, "Built-in iSight (older, UVC)", "uvcvideo", "", "" },
    /* --- FireWire ---------------------------------------------------------- */
    { 0x11c1, 0x5901, HW_DEV_FIREWIRE, "LSI FW643 FireWire 800 (OHCI)", "firewire_ohci", "",
      "In-tree since 2.6.22; nothing extra to install. Present but rarely used — §17 says say so." },
    { 0x104c, 0x8029, HW_DEV_FIREWIRE, "TI TSB82AA2 FireWire 800", "firewire_ohci", "", "" },
    /* --- SATA -------------------------------------------------------------- */
    { 0x8086, 0x3b22, HW_DEV_SATA, "Intel 5 Series/3400 AHCI SATA", "ahci", "",
      "The 7200 rpm HDD hangs off this. AHCI means NCQ; no firmware file." },
    /* --- Audio codec (PCI side of the HDA controller) ---------------------- */
    { 0x8086, 0x3b56, HW_DEV_AUDIO, "Intel 5 Series/3400 HD Audio controller (ALC889 codec)", "snd-hda-intel", "",
      "The codec is identified from /proc/asound, not from this id: the same controller shipped with "
      "several codecs." },
};

const char *hw_dev_role_name(hw_dev_role r)
{
    switch (r) {
    case HW_DEV_WIFI:       return "wifi";
    case HW_DEV_ETHERNET:   return "ethernet";
    case HW_DEV_BLUETOOTH:  return "bluetooth";
    case HW_DEV_CAMERA:     return "camera";
    case HW_DEV_SDCARD:     return "sdcard";
    case HW_DEV_FIREWIRE:   return "firewire";
    case HW_DEV_SATA:       return "sata";
    case HW_DEV_AUDIO:      return "audio";
    case HW_DEV_OPTICAL:    return "optical";
    }
    return "other";
}

const hw_dev_cap *hw_dev_lookup(uint32_t vendor, uint32_t device, hw_dev_role role)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(dev_db); i++)
        if (dev_db[i].vendor == vendor && dev_db[i].device == device && dev_db[i].role == role)
            return &dev_db[i];
    return NULL;
}
const hw_dev_cap *hw_dev_find_any(uint32_t vendor, uint32_t device)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(dev_db); i++)
        if (dev_db[i].vendor == vendor && dev_db[i].device == device) return &dev_db[i];
    return NULL;
}
size_t hw_dev_db_size(void) { return ML_ARRAY_SIZE(dev_db); }
const hw_dev_cap *hw_dev_db_at(size_t i) { return i < ML_ARRAY_SIZE(dev_db) ? &dev_db[i] : NULL; }

/* ------------------------------------------------------- root overrides ---- */
const char *hw_sysfs_root(void) { const char *v = getenv("ML_SYSFS_ROOT"); return v && *v ? v : "/sys"; }
const char *hw_proc_root(void)  { const char *v = getenv("ML_PROC_ROOT");  return v && *v ? v : "/proc"; }
const char *hw_dev_root(void)   { const char *v = getenv("ML_DEV_ROOT");   return v && *v ? v : "/dev"; }
const char *hw_etc_root(void)   { const char *v = getenv("ML_ETC_ROOT");   return v && *v ? v : "/etc"; }
const char *hw_fw_root(void)    { const char *v = getenv("ML_FW_ROOT");    return v && *v ? v : "/lib/firmware"; }
const char *hw_lib_root(void)   { const char *v = getenv("ML_LIB_ROOT");   return v && *v ? v : "/usr/lib"; }

bool hw_using_fixture(void)
{
    return getenv("ML_SYSFS_ROOT") || getenv("ML_PROC_ROOT") || getenv("ML_DEV_ROOT") ||
           getenv("ML_ETC_ROOT") || getenv("ML_FW_ROOT");
}

#define HW_RING 16
static char ring[HW_RING][512];
static int ring_i;

const char *hw_path(const char *root, const char *rel)
{
    char *b = ring[ring_i = (ring_i + 1) % HW_RING];
    if (!rel) rel = "";
    while (*rel == '/') rel++;
    if (!root || !*root) root = "";
    if (!root[0]) snprintf(b, sizeof ring[0], "/%s", rel);
    else if (rel[0]) snprintf(b, sizeof ring[0], "%s/%s", root, rel);
    else snprintf(b, sizeof ring[0], "%s", root);
    return b;
}
const char *hw_sys(const char *rel)  { return hw_path(hw_sysfs_root(), rel); }
const char *hw_proc(const char *rel) { return hw_path(hw_proc_root(), rel); }
const char *hw_dev(const char *rel)  { return hw_path(hw_dev_root(), rel); }
const char *hw_etc(const char *rel)  { return hw_path(hw_etc_root(), rel); }
const char *hw_fw(const char *rel)   { return hw_path(hw_fw_root(), rel); }
const char *hw_lib(const char *rel)  { return hw_path(hw_lib_root(), rel); }

/* ------------------------------------------------------ backend picking --- */
const char *ml_present_name(ml_present_kind k)
{
    switch (k) {
    case ML_PRESENT_KMS:      return "kms";
    case ML_PRESENT_FBDEV:    return "fbdev";
    default:                  return "headless";
    }
}

bool ml_present_parse(const char *name, ml_present_kind *out)
{
    if (!name || !strcmp(name, "auto")) return false;
    if (!strcmp(name, "kms") || !strcmp(name, "drm")) { if (out) *out = ML_PRESENT_KMS; return true; }
    if (!strcmp(name, "fbdev") || !strcmp(name, "fb")) { if (out) *out = ML_PRESENT_FBDEV; return true; }
    if (!strcmp(name, "headless") || !strcmp(name, "none")) { if (out) *out = ML_PRESENT_HEADLESS; return true; }
    return false;
}

static bool any_drm_card(char *found, size_t len)
{
    const char *dir = hw_sys("class/drm");
    DIR *d = opendir(dir);
    if (!d) {
        /* class symlinks may be absent in a fixture; fall back to /dev nodes */
        for (int i = 0; i < 8; i++) {
            char rel[32];
            snprintf(rel, sizeof rel, "dri/card%d", i);
            if (access(hw_dev(rel), F_OK) == 0) { snprintf(found, len, "%s", hw_dev(rel)); return true; }
        }
        return false;
    }
    struct dirent *e;
    bool ok = false;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-')) continue;
        char rel[64];
        snprintf(rel, sizeof rel, "dri/%.31s", e->d_name);
        snprintf(found, len, "%s", hw_dev(rel));
        ok = true;
        break;
    }
    closedir(d);
    return ok;
}

ml_present_kind hw_pick_present(const char *requested, char *reason, size_t reason_len)
{
    char node[256] = "";
    ml_present_kind want = ML_PRESENT_HEADLESS;
    bool explicit_kind = ml_present_parse(requested, &want);
    /* An explicit request is a requirement, not a hint: it narrows the search
     * but never selects a backend the caller did not ask for. The caller (the
     * compositor) turns "asked for kms, got headless" into a hard error -- the
     * rule is that a requested backend is either provided or reported missing. */
    bool want_kms = !explicit_kind || want == ML_PRESENT_KMS;
    bool want_fb  = !explicit_kind || want == ML_PRESENT_FBDEV;

    if (explicit_kind && want == ML_PRESENT_HEADLESS) {
        if (reason) snprintf(reason, reason_len, "headless requested");
        return ML_PRESENT_HEADLESS;
    }
    if (want_kms && any_drm_card(node, sizeof node)) {
        if (reason) snprintf(reason, reason_len, "DRM device %s", node);
        return ML_PRESENT_KMS;
    }
    if (want_kms && !want_fb && reason)
        snprintf(reason, reason_len, "no /dev/dri/card* under %s", hw_dev_root());
    if (want_fb && access(hw_dev("fb0"), F_OK) == 0) {
        if (reason) snprintf(reason, reason_len, "framebuffer %s", hw_dev("fb0"));
        return ML_PRESENT_FBDEV;
    }
    if (reason) snprintf(reason, reason_len, "no DRM device and no %s -> software/headless", hw_dev("fb0"));
    return ML_PRESENT_HEADLESS;
}

const char *ml_hwdec_name(ml_hwdec h)
{
    switch (h) {
    case ML_HWDEC_VDPAU: return "vdpau";
    case ML_HWDEC_VAAPI: return "vaapi";
    case ML_HWDEC_AUTO:  return "auto";
    default:             return "no";
    }
}

static bool lib_present(const char *soname)
{
    static const char *dirs[] = { "/usr/lib", "/lib", "/usr/lib/x86_64-linux-gnu", "/usr/local/lib" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(dirs); i++) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", dirs[i], soname);
        if (access(p, F_OK) == 0) return true;
    }
    return false;
}

ml_hwdec hw_pick_hwdec(uint32_t vendor, uint32_t device, bool render_node, char *reason, size_t reason_len)
{
    const hw_gpu_cap *c = hw_gpu_lookup(vendor, device);
    if (!c) {
        if (reason) snprintf(reason, reason_len, "GPU %04x:%04x not in capability DB", vendor, device);
        return render_node ? ML_HWDEC_AUTO : ML_HWDEC_NONE;
    }
    if (!c->decode) {
        if (reason) snprintf(reason, reason_len, "%s has no fixed-function decoder", c->model);
        return ML_HWDEC_NONE;
    }
    bool vdpau_lib = lib_present("libvdpau.so.1");
    bool va_lib = lib_present("libva.so.2");
    if (!strcmp(c->decode_api, "vdpau") && (vdpau_lib || render_node)) {
        if (reason) snprintf(reason, reason_len, "%s UVD via VDPAU (%s)", c->model,
                             vdpau_lib ? "libvdpau present" : "render node present, libvdpau unverified");
        return vdpau_lib ? ML_HWDEC_VDPAU : ML_HWDEC_AUTO;
    }
    if (!strcmp(c->decode_api, "vaapi") && (va_lib || render_node)) {
        if (reason) snprintf(reason, reason_len, "%s via VA-API (%s)", c->model,
                             va_lib ? "libva present" : "render node present, libva unverified");
        return va_lib ? ML_HWDEC_VAAPI : ML_HWDEC_AUTO;
    }
    if (reason) snprintf(reason, reason_len, "%s supports %s but no userspace API found on this host",
                         c->model, c->decode_api);
    return ML_HWDEC_NONE;
}

uint32_t hw_pick_mode_caps(bool kms_ok, bool hw_render, uint32_t cores, uint32_t ram_mb,
                           char *reason, size_t reason_len)
{
    /* MODE_BEAUTIFUL=0, MODE_BALANCED=1, MODE_PERFORMANCE=2 (compositor/proto.h) */
    if (!kms_ok) {
        if (reason) snprintf(reason, reason_len, "no KMS: software scanout, animations trimmed");
        return 2;
    }
    if (!hw_render) {
        if (reason) snprintf(reason, reason_len, "KMS present but rendering is CPU-side: balanced");
        return 1;
    }
    if (cores < 2 || ram_mb < 1024) {
        if (reason) snprintf(reason, reason_len, "GPU ok but %u cores / %u MB RAM: balanced", cores, ram_mb);
        return 1;
    }
    if (reason) snprintf(reason, reason_len, "KMS + hardware rendering + %u cores + %u MB: full animations",
                         cores, ram_mb);
    return 0;
}
