#include "media.h"
#include "hwprobe.h"
#include "ml/util.h"
#include "ml/log.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <strings.h>
#include <errno.h>

const char *ml_decoder_name(ml_decoder d)
{
    switch (d) {
    case ML_DEC_MPV:     return "mpv";
    case ML_DEC_FFMPEG:  return "ffmpeg";
    case ML_DEC_GST:     return "gstreamer";
    default:             return "none";
    }
}

bool ml_decoder_probe(ml_dec_state *out)
{
    memset(out, 0, sizeof *out);
    static const struct { const char *bin; ml_decoder kind; } cands[] = {
        { "ffmpeg", ML_DEC_FFMPEG }, { "mpv", ML_DEC_MPV }, { "gst-launch-1.0", ML_DEC_GST },
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(cands); i++) {
        char *p = ml_find_in_path(cands[i].bin);
        if (!p) continue;
        out->present = true;
        out->kind = cands[i].kind;
        snprintf(out->path, sizeof out->path, "%s", p);
        ml_free(p);
        char cmd[256];
        snprintf(cmd, sizeof cmd, "%s --version 2>/dev/null | head -n 1", out->path);
        FILE *f = popen(cmd, "r");
        if (f) {
            if (fgets(out->version, sizeof out->version, f)) {
                char *e = strpbrk(out->version, "\r\n");
                if (e) *e = 0;
            }
            pclose(f);
        }
        return true;
    }
    snprintf(out->note, sizeof out->note,
             "no ffmpeg/mpv/gstreamer on PATH: decode cannot be exercised on this host");
    return false;
}

static void add_codec(ml_hwdec_state *h, uint32_t bit, const char *src)
{
    h->runtime_mask |= bit;
    size_t n = strlen(h->detail);
    snprintf(h->detail + n, sizeof h->detail - n, "%s%s", n ? " " : "", src);
}

bool ml_hwdec_probe(ml_hwdec_state *out)
{
    memset(out, 0, sizeof *out);
    static const char *libs_vdpau[] = { "/usr/lib/libvdpau.so.1", "/usr/lib/x86_64-linux-gnu/libvdpau.so.1" };
    static const char *libs_va[] = { "/usr/lib/libva.so.2", "/usr/lib/x86_64-linux-gnu/libva.so.2" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(libs_vdpau); i++) if (access(libs_vdpau[i], F_OK) == 0) out->vdpau_lib = true;
    for (size_t i = 0; i < ML_ARRAY_SIZE(libs_va); i++) if (access(libs_va[i], F_OK) == 0) out->vaapi_lib = true;
    for (int i = 128; i < 136; i++) {
        char rel[32], p[64];
        snprintf(rel, sizeof rel, "dri/renderD%d", i);
        snprintf(p, sizeof p, "%s", hw_dev(rel));
        if (access(p, F_OK) == 0) {
            out->has_render_node = true;
            snprintf(out->render_node, sizeof out->render_node, "%s", p);
            break;
        }
    }

    /* real runtime queries: vdpauinfo lists decoder profiles, vainfo lists
     * VAProfile*. Neither is available in a headless sandbox -> not probed. */
    char *v = ml_find_in_path("vdpauinfo");
    if (v) {
        FILE *f = popen("vdpauinfo 2>&1", "r");
        ml_free(v);
        if (f) {
            char line[256];
            bool saw_table = false;
            while (fgets(line, sizeof line, f)) {
                if (strstr(line, "H264")) { add_codec(out, ML_CODEC_H264, "H.264"); saw_table = true; }
                if (strstr(line, "MPEG2") || strstr(line, "MPEG_2")) { add_codec(out, ML_CODEC_MPEG2, "MPEG-2"); saw_table = true; }
                if (strstr(line, "MPEG4")) { add_codec(out, ML_CODEC_MPEG4, "MPEG-4"); saw_table = true; }
                if (strstr(line, "VC1")) { add_codec(out, ML_CODEC_VC1, "VC-1"); saw_table = true; }
                if (strstr(line, "HEVC")) { add_codec(out, ML_CODEC_HEVC, "HEVC"); saw_table = true; }
                if (strstr(line, "VP9")) { add_codec(out, ML_CODEC_VP9, "VP9"); saw_table = true; }
            }
            pclose(f);
            if (saw_table) { out->probed = true; snprintf(out->source, sizeof out->source, "vdpauinfo"); }
        }
    }
    if (!out->probed) {
        char *va = ml_find_in_path("vainfo");
        if (va) {
            FILE *f = popen("vainfo 2>&1", "r");
            ml_free(va);
            if (f) {
                char line[256];
                bool saw = false;
                while (fgets(line, sizeof line, f)) {
                    if (strstr(line, "VAProfileH264")) { add_codec(out, ML_CODEC_H264, "H.264"); saw = true; }
                    if (strstr(line, "VAProfileMPEG2")) { add_codec(out, ML_CODEC_MPEG2, "MPEG-2"); saw = true; }
                    if (strstr(line, "VAProfileVC1")) { add_codec(out, ML_CODEC_VC1, "VC-1"); saw = true; }
                    if (strstr(line, "VAProfileHEVC")) { add_codec(out, ML_CODEC_HEVC, "HEVC"); saw = true; }
                    if (strstr(line, "VAProfileVP9")) { add_codec(out, ML_CODEC_VP9, "VP9"); saw = true; }
                }
                pclose(f);
                if (saw) { out->probed = true; snprintf(out->source, sizeof out->source, "vainfo"); }
            }
        }
    }
    if (!out->probed)
        snprintf(out->note, sizeof out->note,
                 "no vdpauinfo/vainfo output on this host: hardware decode is NOT TESTED "
                 "(libs: vdpau=%d vaapi=%d render_node=%s)",
                 out->vdpau_lib, out->vaapi_lib, out->has_render_node ? "yes" : "no");
    return out->probed;
}

const char *ml_cap_str(ml_cap c)
{
    switch (c) {
    case ML_CAP_HW:      return "hardware";
    case ML_CAP_SW:      return "software only";
    default:             return "not verified";
    }
}

ml_cap ml_codec_capability(uint32_t vendor, uint32_t device, uint32_t codec,
                           const ml_hwdec_state *h, char *detail, size_t detail_len)
{
    const hw_gpu_cap *cap = hw_gpu_lookup(vendor, device);
    bool silicon = cap && (cap->decode & codec);
    bool runtime = h && h->probed && (h->runtime_mask & codec);
    if (silicon && runtime) {
        if (detail) snprintf(detail, detail_len, "%s: silicon + %s both advertise it",
                             ml_codec_name(codec), h->source);
        return ML_CAP_HW;
    }
    if (silicon && (!h || !h->probed)) {
        if (detail) snprintf(detail, detail_len,
                             "%s: %s has it in silicon but no %s query ran here — NOT TESTED",
                             ml_codec_name(codec), cap ? cap->model : "GPU",
                             cap && !strcmp(cap->decode_api, "vdpau") ? "vdpauinfo" : "vainfo");
        return ML_CAP_UNKNOWN;
    }
    if (detail) {
        if (cap) snprintf(detail, detail_len, "%s: %s has no %s decode block", ml_codec_name(codec), cap->model,
                          ml_codec_name(codec));
        else snprintf(detail, detail_len, "%s: GPU not in capability DB and no runtime advertises it",
                      ml_codec_name(codec));
    }
    return ML_CAP_SW;
}

/* ----------------------------------------------------------- test runner -- */
static double rusage_ms(const struct rusage *r)
{
    return (double)r->ru_utime.tv_sec * 1000.0 + (double)r->ru_utime.tv_usec / 1000.0 +
           (double)r->ru_stime.tv_sec * 1000.0 + (double)r->ru_stime.tv_usec / 1000.0;
}

/* run a shell command, capture combined output into buf, return exit code and
 * fill wall/cpu times from wait4's rusage */
static int run_capture(const char *cmd, char *buf, size_t buflen, double *wall, double *cpu)
{
    buf[0] = 0;
    char full[1024];
    snprintf(full, sizeof full, "( %s ) 2>&1", cmd);
    uint64_t t0 = ml_now_ns();
    FILE *f = popen(full, "r");
    if (!f) return -1;
    size_t n = 0;
    while (n + 1 < buflen) {
        size_t r = fread(buf + n, 1, buflen - 1 - n, f);
        if (!r) break;
        n += r;
    }
    buf[n] = 0;
    int rc = pclose(f);
    double wms = ml_elapsed_ms(t0);
    if (wall) *wall = wms;
    if (cpu) *cpu = wms;   /* popen hides the child's rusage; refined below */
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool ml_video_test_run(ml_vtest *out, const char *codec, int width, int height,
                       int frames, bool want_hw, int *exit_rc)
{
    memset(out, 0, sizeof *out);
    snprintf(out->codec, sizeof out->codec, "%s", codec);
    out->width = width;
    out->height = height;
    out->frames = frames;
    out->hw_requested = want_hw;
    out->cap = ML_CAP_UNKNOWN;
    if (exit_rc) *exit_rc = 0;

    ml_dec_state dec;
    ml_decoder_probe(&dec);
    if (!dec.present) {
        snprintf(out->note, sizeof out->note, "%s", dec.note);
        return false;
    }
    uint32_t bit = ml_codec_bit(codec);
    if (!bit) {
        snprintf(out->note, sizeof out->note, "unknown codec '%s'", codec);
        return false;
    }
    if (dec.kind != ML_DEC_FFMPEG) {
        snprintf(out->note, sizeof out->note,
                 "%s found (%.80s); this test drives ffmpeg — see docs/multimedia.md",
                 ml_decoder_name(dec.kind), dec.path);
        return false;
    }

    /* 1. create a real encoded stream of the requested codec/resolution */
    char src[128], cmd[768];
    snprintf(src, sizeof src, "/tmp/maclite-%s-%dx%d.mp4", codec, width, height);
    const char *enc = !strcasecmp(codec, "h264") ? "libx264"
                    : !strcasecmp(codec, "hevc") || !strcasecmp(codec, "h265") ? "libx265"
                    : !strcasecmp(codec, "mpeg4") ? "mpeg4"
                    : !strcasecmp(codec, "mpeg2") ? "mpeg2video"
                    : !strcasecmp(codec, "vp9") ? "libvpx-vp9" : NULL;
    if (!enc) {
        snprintf(out->note, sizeof out->note, "no encoder known for %s", codec);
        return false;
    }
    snprintf(cmd, sizeof cmd,
             "%s -y -loglevel error -f lavfi -i testsrc2=size=%dx%d:rate=30:duration=%.2f "
             "-c:v %s -pix_fmt yuv420p '%s'",
             dec.path, width, height, frames / 30.0, enc, src);
    double wall = 0;
    int rc = run_capture(cmd, out->decoder_line, sizeof out->decoder_line, &wall, NULL);
    if (rc != 0 || !ml_file_exists(src)) {
        snprintf(out->note, sizeof out->note,
                 "cannot create a %s test stream (ffmpeg exit %d): %.120s",
                 codec, rc, out->decoder_line);
        return false;
    }

    /* 2. decode it, with or without the hardware path, and time the child */
    const char *hw = want_hw ? "-hwaccel auto -hwaccel_output_format auto" : "-hwaccel none";
    snprintf(cmd, sizeof cmd, "%s -hide_banner -loglevel info %s -i '%s' -f null -", dec.path, hw, src);
    char log[4096];
    double cpu = 0;
    /* fork/exec directly so wait4 gives us the child's real CPU time */
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) { dup2(devnull, 1); close(devnull); }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof ru);
    uint64_t t0 = ml_now_ns();
    wait4(pid, &status, 0, &ru);
    wall = ml_elapsed_ms(t0);
    cpu = rusage_ms(&ru);
    rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    /* ffmpeg prints the chosen decoder to stderr; capture it separately */
    snprintf(cmd, sizeof cmd, "%s -hide_banner -loglevel info %s -i '%s' -f null - 2>&1 | head -c 3800",
             dec.path, hw, src);
    run_capture(cmd, log, sizeof log, NULL, NULL);
    char *q = strstr(log, "Video: ");
    if (q) {
        char *e = strchr(q, '\n');
        if (e) *e = 0;
        snprintf(out->decoder_line, sizeof out->decoder_line, "%s", q);
    }
    out->hw_used = strstr(log, "hwaccel") != NULL || strstr(log, "vdpau") != NULL ||
                   strstr(log, "vaapi") != NULL;
    out->ran = true;
    out->wall_ms = wall;
    out->cpu_ms = cpu;
    out->fps = wall > 0 ? frames / (wall / 1000.0) : 0;
    out->cpu_pct = wall > 0 ? (cpu / wall) * 100.0 : 0;
    char *drop = strstr(log, "dropped=");
    if (drop) out->dropped = atoi(drop + 8);
    snprintf(out->note, sizeof out->note, "%s decode %s: %.0f fps, child CPU %.0f ms of %.0f ms wall (%.0f%%), exit %d",
             want_hw ? "hardware" : "software", rc == 0 ? "succeeded" : "FAILED",
             out->fps, cpu, wall, out->cpu_pct, rc);
    unlink(src);
    if (exit_rc) *exit_rc = rc;
    return rc == 0;
}
