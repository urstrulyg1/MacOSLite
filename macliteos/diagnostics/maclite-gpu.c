/* maclite-gpu — GPU detection and acceleration validation.
 *
 * The rule this tool exists to enforce: never report acceleration because a
 * device node happens to exist. DRM/KMS is PASS only when a card is bound to a
 * driver *and* the compositor/DRM path can enumerate connectors; hardware
 * rendering is PASS only when a real GL query returned a non-software renderer;
 * anything else is NOT TESTED and the exit code says so.
 */
#include "diag_common.h"
#include "../performance/perf_common.h"
#include "../hardware/media.h"

static const char *vendor_name(uint32_t vid)
{
    switch (vid) {
    case 0x1002: return "ATI/AMD";
    case 0x8086: return "Intel";
    case 0x10de: return "NVIDIA";
    case 0x1af4: return "Red Hat (virtio)";
    case 0x15ad: return "VMware";
    case 0x1234: return "QEMU/bochs";
    default:     return "Unknown";
    }
}

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [--tsv] [--no-live]\n"
        "  --tsv      machine readable output (group<TAB>name<TAB>RESULT<TAB>required<TAB>evidence)\n"
        "  --no-live  do not query a running compositor\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    bool tsv = diag_flag(argc, argv, "--tsv");
    bool live = !diag_flag(argc, argv, "--no-live");

    mica_gpu_info g;
    mica_drm_state drm;
    mica_gpu_probe(&g);
    mica_drm_probe(&drm);

    hw_report rep;
    hw_report_init(&rep);

    /* ---- GPU identity -------------------------------------------------- */
    if (!g.present) {
        hw_report_add(&rep, "GPU", "Detection", true, HW_UNSUPPORTED,
                      "no PCI display controller under %s", hw_sys("bus/pci/devices"));
    } else {
        hw_report_add(&rep, "GPU", "Detection", true, HW_PASS, "%s [%s] at %s",
                      g.device, g.driver[0] ? g.driver : "no driver bound", g.pci_addr);
        hw_report_add(&rep, "GPU", "Driver bound", true,
                      g.driver[0] ? HW_PASS : HW_FAIL,
                      g.driver[0] ? "%s (from the PCI device's driver link)" :
                                    "no driver bound: KMS and acceleration cannot work", g.driver);
        hw_report_add(&rep, "GPU", "DRM device", true,
                      g.has_kms ? HW_PASS : HW_FAIL, "%s",
                      g.card_node[0] ? g.card_node : "none");
        hw_report_add(&rep, "GPU", "Render node", false,
                      g.has_render_node ? HW_PASS : HW_UNSUPPORTED, "%s",
                      g.render_node[0] ? g.render_node : "none");
        hw_report_add(&rep, "GPU", "Capability DB entry", false,
                      g.db_model[0] ? HW_PASS : HW_UNSUPPORTED,
                      g.db_model[0] ? "gallium=%s decode_api=%s" : "GPU not in the capability database",
                      g.gallium, g.accel_api);
    }

    /* ---- acceleration -------------------------------------------------- */
    bool kms_ok = g.has_kms && drm.any;
    hw_report_add(&rep, "Acceleration", "DRM/KMS", true,
                  kms_ok ? HW_PASS : (drm.any ? HW_FAIL : HW_UNSUPPORTED),
                  kms_ok ? "%s with %d connector(s), driver %s" : "%s",
                  kms_ok ? drm.card_node : "", drm.n,
                  drm.driver[0] ? drm.driver : "-");

    gl_probe_result glr = mica_gl_probe(&g);
    hw_report_add(&rep, "Acceleration", "OpenGL", true,
                  glr == GL_HW ? HW_PASS : glr == GL_SW ? HW_PARTIAL
                  : glr == GL_NONE ? HW_UNSUPPORTED : HW_NOT_TESTED,
                  g.gl_probed ? "%s %s (queried with %s)" :
                  glr == GL_NONE ? "no libGL/libEGL on this host"
                                 : "GL stack present but no context could be created here",
                  g.gl_version, g.gl_renderer, g.gl_source);
    hw_report_add(&rep, "Acceleration", "Hardware Rendering", true,
                  glr == GL_HW ? HW_PASS : glr == GL_SW ? HW_FAIL
                  : glr == GL_NONE ? HW_UNSUPPORTED : HW_NOT_TESTED,
                  g.gl_probed ? "renderer: %s" : "not verified — see docs/gpu.md",
                  g.gl_renderer);
    hw_report_add(&rep, "Acceleration", "Software Rendering Fallback", false, HW_PASS,
                  "always available: damage-tracking CPU raster (measured by maclite-gpu-benchmark)");

    /* ---- video acceleration (spec §3: the report has a Video section) --- */
    ml_hwdec_state hd;
    ml_hwdec_probe(&hd);
    char vdetail[192];
    ml_cap vcap = ml_codec_capability(g.vendor_id, g.device_id, ML_CODEC_H264, &hd,
                                      vdetail, sizeof vdetail);
    {
        char codecs[128] = "";
        ml_codec_list(g.decode_mask, codecs, sizeof codecs);
        hw_report_add(&rep, "Video", "Silicon", false, g.decode_mask ? HW_PASS : HW_UNSUPPORTED,
                      g.decode_mask ? "decoder for %.90s; driver exposes %.12s"
                                    : "no fixed-function video decoder in this GPU",
                      codecs[0] ? codecs : "(none listed)", g.accel_api[0] ? g.accel_api : "no API");
    }
    hw_report_add(&rep, "Video", "H.264 decode", true,
                  vcap == ML_CAP_HW ? HW_PASS : vcap == ML_CAP_UNKNOWN ? HW_NOT_TESTED : HW_UNSUPPORTED,
                  "%s", vdetail);
    hw_report_add(&rep, "Video", "Decode API", false,
                  (hd.vdpau_lib || hd.vaapi_lib) ? HW_PASS : HW_NOT_TESTED, "%.120s",
                  hd.vdpau_lib || hd.vaapi_lib
                      ? (hd.probed ? hd.detail : "a decode client library is installed; no runtime query ran")
                      : "no VDPAU/VA-API client library on this machine");
    hw_report_add(&rep, "Video", "1080p playback", false, HW_NOT_TESTED,
                  "measured by maclite-video-test h264 1920x1080 --perf, not from a datasheet");

    /* ---- display ------------------------------------------------------- */
    if (drm.primary >= 0) {
        mica_connector *c = &drm.c[drm.primary];
        hw_report_add(&rep, "Display", "Connected", true, HW_PASS, "%s (%s)", c->connector, c->status);
        const edid_mode *p = edid_preferred(&c->edid);
        hw_report_add(&rep, "Display", "Native mode", true,
                      p ? HW_PASS : (c->nmodes ? HW_PARTIAL : HW_FAIL),
                      p ? "%dx%d@%d Hz from EDID" : c->nmodes ? "%dx%d (first KMS mode, no usable EDID)" : "none",
                      p ? p->w : (c->nmodes ? c->mode_w[0] : 0),
                      p ? p->h : (c->nmodes ? c->mode_h[0] : 0),
                      p ? edid_refresh_hz(p) : 60);
    } else {
        hw_report_add(&rep, "Display", "Connected", true,
                      drm.any ? HW_FAIL : HW_UNSUPPORTED,
                      drm.any ? "DRM present but no connected connector" : "no DRM device here");
    }

    /* ---- compositor ---------------------------------------------------- */
    msg_stats st = { 0 };
    bool have_live = live && comp_stats(&st);
    if (have_live) {
        const char *backend = st.backend == 1 ? "kms" : st.backend == 2 ? "fbdev" : "headless";
        hw_report_add(&rep, "Compositor", "GPU Backend", true,
                      st.backend ? HW_PASS : HW_FAIL, "%s (reported by the running compositor)", backend);
        hw_report_add(&rep, "Compositor", "Hardware Accelerated", true,
                      st.accel ? HW_PASS : HW_PARTIAL, "%s",
                      st.accel ? "GPU compositing" : "CPU raster into GPU-scanned-out buffer");
        hw_report_add(&rep, "Compositor", "Performance mode", true,
                      st.mode_pinned ? HW_PASS : HW_PARTIAL,
                      "%s%s", mica_mode_name(st.mode),
                      st.mode_pinned ? " (chosen explicitly)"
                                     : " (picked from the detected GPU)");
        hw_report_add(&rep, "Compositor", "Effect reduction", false, HW_PASS,
                      st.mode_steps ? "%u step(s) down: frames were over the %.0f ms bar"
                                    : "none needed (%u step(s))",
                      st.mode_steps, 25.0);
        hw_report_add(&rep, "Compositor", "Frame pacing", false, HW_PASS,
                      "%.2f fps, mean %u us, worst %u us, %llu dropped",
                      st.fps_x100 / 100.0, st.frame_us, st.worst_us, (unsigned long long)st.dropped);
    } else {
        hw_report_note(&rep, "Compositor", "GPU Backend",
                       "no live session (start mica-comp, or run maclite-gpu-benchmark for the offline path)");
    }
    hw_report_note(&rep, "Compositor", "Target FPS", "60 (16.6 ms budget; see docs/gpu.md)");

    if (tsv) { hw_report_print_tsv(&rep); }
    else {
        diag_title("MacLiteOS GPU Diagnostics");
        diag_section("Graphics");
        diag_kv("Vendor", "%s (0x%04x)", g.present ? vendor_name(g.vendor_id) : "-", g.vendor_id);
        diag_kv("Model", "%s", or_dash(g.device));
        diag_kv("PCI ID", "%s (%04x:%04x)", g.pci_addr, g.vendor_id, g.device_id);
        diag_kv("Driver", "%s", or_dash(g.driver));
        diag_kv("DRM Device", "%s", g.card_node[0] ? g.card_node : "-");
        diag_kv("Render Node", "%s", g.render_node[0] ? g.render_node : "-");
        diag_kv("VRAM", g.vram_bytes ? "%llu MB" : "unknown", (unsigned long long)(g.vram_bytes >> 20));
        diag_kv("OpenGL renderer", "%s", g.gl_probed ? g.gl_renderer : "not queried here");
        diag_kv("OpenGL version", "%s", g.gl_probed ? or_dash(g.gl_version) : "not queried here");
        putchar('\n');
        diag_section("Acceleration");
        const hw_check *c;
        if ((c = hw_report_find(&rep, "Acceleration", "DRM/KMS")))
            diag_res("DRM/KMS", c->result, "%s", c->evidence);
        if ((c = hw_report_find(&rep, "Acceleration", "OpenGL")))
            diag_res("OpenGL", c->result, "%s", c->evidence);
        if ((c = hw_report_find(&rep, "Acceleration", "Hardware Rendering")))
            diag_res("Hardware Rendering", c->result, "%s", c->evidence);
        diag_res("Software Rendering Fallback", HW_PASS, "AVAILABLE");
        putchar('\n');
        diag_section("Video");
        if ((c = hw_report_find(&rep, "Video", "Silicon")))
            diag_res("Video Engine", c->result, "%s", c->evidence);
        if ((c = hw_report_find(&rep, "Video", "H.264 decode")))
            diag_res("H.264 Decode", c->result, "%s", c->evidence);
        if ((c = hw_report_find(&rep, "Video", "Decode API")))
            diag_res("Decode API", c->result, "%s", c->evidence);
        if ((c = hw_report_find(&rep, "Video", "1080p playback")))
            diag_res("1080p Playback", c->result, "%s", c->evidence);
        putchar('\n');
        diag_section("Display");
        if (drm.primary >= 0) {
            mica_connector *cc = &drm.c[drm.primary];
            const edid_mode *p = edid_preferred(&cc->edid);
            diag_kv("Resolution", p ? "%dx%d" : cc->nmodes ? "%dx%d" : "-",
                    p ? p->w : (cc->nmodes ? cc->mode_w[0] : 0),
                    p ? p->h : (cc->nmodes ? cc->mode_h[0] : 0));
            diag_kv("Refresh Rate", "%d Hz", p ? edid_refresh_hz(p) : 60);
            diag_kv("Connected", "%s", diag_yn(cc->connected));
            diag_kv("Modes offered", "%d", cc->nmodes);
        } else {
            diag_kv("Resolution", "-");
            diag_kv("Refresh Rate", "-");
            diag_kv("Connected", "NO");
        }
        putchar('\n');
        diag_section("Compositor");
        if (have_live) {
            diag_kv("GPU Backend", "%s", st.backend == 1 ? "kms" : st.backend == 2 ? "fbdev" : "headless");
            diag_kv("Hardware Accelerated", "%s", diag_yn(st.accel != 0));
            diag_kv("Performance Mode", "%s%s", mica_mode_name(st.mode),
                    st.mode_pinned ? " (pinned)" : "");
            diag_kv("Auto-reduced", "%s", st.mode_steps ? "yes" : "no");
            diag_kv("Target FPS", "60");
            diag_kv("Measured", "%.2f fps, worst frame %u us", st.fps_x100 / 100.0, st.worst_us);
        } else {
            diag_kv("GPU Backend", "no live session");
            diag_kv("Hardware Accelerated", "NOT TESTED");
            diag_kv("Target FPS", "60");
        }
        putchar('\n');
        printf("Overall: %s\n", hw_result_str(hw_report_overall(&rep)));
        if (hw_report_overall(&rep) != HW_PASS) {
            printf("\nChecks that are not PASS:\n");
            for (int i = 0; i < rep.n; i++)
                if (rep.v[i].required && !hw_result_ok(rep.v[i].result))
                    printf("  %-24s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        }
    }
    return hw_report_exit(&rep);
}
