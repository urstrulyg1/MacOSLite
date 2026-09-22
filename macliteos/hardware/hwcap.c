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

/* ------------------------------------------------------- root overrides ---- */
const char *hw_sysfs_root(void) { const char *v = getenv("ML_SYSFS_ROOT"); return v && *v ? v : "/sys"; }
const char *hw_proc_root(void)  { const char *v = getenv("ML_PROC_ROOT");  return v && *v ? v : "/proc"; }
const char *hw_dev_root(void)   { const char *v = getenv("ML_DEV_ROOT");   return v && *v ? v : "/dev"; }
const char *hw_etc_root(void)   { const char *v = getenv("ML_ETC_ROOT");   return v && *v ? v : "/etc"; }

bool hw_using_fixture(void)
{
    return getenv("ML_SYSFS_ROOT") || getenv("ML_PROC_ROOT") || getenv("ML_DEV_ROOT") || getenv("ML_ETC_ROOT");
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

/* ------------------------------------------------------ backend picking --- */
const char *ml_present_name(ml_present_kind k)
{
    switch (k) {
    case ML_PRESENT_KMS:      return "kms";
    case ML_PRESENT_FBDEV:    return "fbdev";
    default:                  return "headless";
    }
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
    bool want_kms = !requested || !strcmp(requested, "auto") || !strcmp(requested, "kms");
    bool want_fb  = !requested || !strcmp(requested, "auto") || !strcmp(requested, "fbdev");

    if (requested && !strcmp(requested, "headless")) {
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
