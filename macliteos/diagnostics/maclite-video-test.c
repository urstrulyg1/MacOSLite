/* maclite-video-test — codec capability and decode verification (spec §8/§9).
 *
 * Three layers, never conflated:
 *   silicon   capability DB entry for the detected GPU
 *   runtime   what vdpauinfo/vainfo actually advertise on this boot
 *   measured  a real encode+decode round trip with timing and CPU accounting
 * A codec is only PASS when the measured round trip succeeded. Without a
 * decoder on PATH the tool reports NOT TESTED and exits 2 — it never guesses.
 */
#include "diag_common.h"
#include <sys/wait.h>

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [codec ...] [--sw] [--res 720|1080] [--frames N] [--all]\n"
        "  codec      h264 | mpeg2 | mpeg4 | hevc | vp9 | av1   (default: h264 mpeg2)\n"
        "  --sw       force the software path (comparison run)\n"
        "  --frames N frames to decode (default 150 = 5 s at 30 fps)\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    bool sw = diag_flag(argc, argv, "--sw");
    const char *res = diag_opt(argc, argv, "--res");
    const char *fr = diag_opt(argc, argv, "--frames");
    int W = 1280, H = 720;
    if (res && atoi(res) == 1080) { W = 1920; H = 1080; }
    int frames = fr ? atoi(fr) : 150;

    const char *wanted[8];
    int nw = 0;
    for (int i = 1; i < argc && nw < 8; i++) {
        if (argv[i][0] == '-') { if (!strcmp(argv[i], "--res") || !strcmp(argv[i], "--frames")) i++; continue; }
        wanted[nw++] = argv[i];
    }
    if (!nw) { wanted[nw++] = "h264"; wanted[nw++] = "mpeg2"; }

    mica_gpu_info g;
    mica_gpu_probe(&g);
    ml_hwdec_state h;
    ml_hwdec_probe(&h);
    ml_dec_state dec;
    ml_decoder_probe(&dec);

    diag_title("MacLiteOS Video Decode Test");
    printf("GPU:            %s\n", or_dash(g.device));
    printf("Decode API:     %s\n", g.accel_api[0] ? g.accel_api : "none");
    printf("Render node:    %s\n", h.has_render_node ? h.render_node : "-");
    printf("Runtime query:  %s\n", h.probed ? h.source : "none available here");
    if (h.detail[0]) printf("Advertised:     %s\n", h.detail);
    printf("Decoder:        %s\n", dec.present ? dec.path : "none on PATH");
    if (dec.version[0]) printf("Version:        %s\n", dec.version);
    putchar('\n');

    hw_report rep;
    hw_report_init(&rep);
    char codecs_str[128];
    ml_codec_list(g.decode_mask, codecs_str, sizeof codecs_str);
    hw_report_add(&rep, "Capability", "Silicon decode", false,
                  g.decode_mask ? HW_PASS : HW_UNSUPPORTED, "%s: %s", or_dash(g.db_model), codecs_str);
    hw_report_add(&rep, "Capability", "Runtime decode", false,
                  h.probed ? HW_PASS : HW_NOT_TESTED, "%s",
                  h.probed ? h.detail : h.note);
    hw_report_add(&rep, "Capability", "Decoder binary", true,
                  dec.present ? HW_PASS : HW_UNSUPPORTED, "%s",
                  dec.present ? dec.version : dec.note);

    int any_pass = 0, any_fail = 0, any_untested = 0;
    for (int i = 0; i < nw; i++) {
        uint32_t bit = ml_codec_bit(wanted[i]);
        char detail[200];
        ml_cap cap = ml_codec_capability(g.vendor_id, g.device_id, bit, &h, detail, sizeof detail);
        printf("Codec: %s\n", wanted[i]);
        printf("  Capability:     %s\n", ml_cap_str(cap));
        printf("  Evidence:       %s\n", detail);

        ml_vtest vt;
        int rc = 0;
        bool ran = ml_video_test_run(&vt, wanted[i], W, H, frames, !sw, &rc);
        if (!ran) {
            printf("  Hardware Decode: NOT TESTED\n  Software Decode: NOT TESTED\n");
            printf("  Reason:         %s\n\n", vt.note);
            hw_report_add(&rep, "Decode", wanted[i], true, HW_NOT_TESTED, "%s", vt.note);
            any_untested++;
            continue;
        }
        printf("  Hardware Decode: %s\n", vt.hw_used ? "PASS" : "FAIL (ran, but no hw path engaged)");
        printf("  Software Decode: %s\n", "see --sw run");
        printf("  Renderer:       %s\n", or_dash(vt.decoder_line));
        printf("  Dropped Frames: %d\n", vt.dropped);
        printf("  CPU Usage:      %.0f%% of wall (%.0f ms CPU / %.0f ms wall)\n",
               vt.cpu_pct, vt.cpu_ms, vt.wall_ms);
        printf("  FPS:            %.0f at %dx%d\n\n", vt.fps, W, H);
        hw_report_add(&rep, "Decode", wanted[i], true,
                      (rc == 0 && (!sw ? vt.hw_used || cap != ML_CAP_HW : true)) ? HW_PASS : HW_FAIL,
                      "%dx%d %d frames: %.0f fps, CPU %.0f%%, decoder=%s", W, H, frames, vt.fps,
                      vt.cpu_pct, vt.hw_used ? "hardware" : "software");
        if (rc == 0) any_pass++; else any_fail++;
    }

    printf("Overall: %s\n", hw_result_str(hw_report_overall(&rep)));
    if (any_untested && !any_pass && !any_fail)
        printf("\nNothing was decoded on this host. On the iMac run:\n"
               "  maclite-video-test h264 mpeg2 --res 1080\n"
               "and record the numbers in docs/testing.md.\n");
    (void)argc;
    return hw_report_exit(&rep);
}
