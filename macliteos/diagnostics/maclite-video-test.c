/* maclite-video-test — codec capability and decode verification (spec §8/§9).
 *
 * Four layers, never conflated:
 *   silicon   capability DB entry for the detected GPU
 *   runtime   what vdpauinfo/vainfo actually advertise on this boot
 *   measured  a real encode+decode round trip with timing and CPU accounting
 *   playback  seek latency and the video/audio decode skew (see ml_vperf)
 *
 * A codec row is PASS only when the hardware path really engaged (the decoder's
 * own output named a hardware decoder), FAIL when the silicon has the block but
 * the decoder fell back, PARTIAL when it decoded in software while hardware
 * support is unknown or absent, and NOT TESTED when nothing could run at all.
 * It never guesses, and never calls a software decode "hardware accelerated".
 *
 *   maclite-video-test                       h264 mpeg2 at 720p
 *   maclite-video-test h264 mpeg2 --res 1080 1080p
 *   maclite-video-test --sw                  force the software path (comparison)
 *   maclite-video-test --perf --seek 2       seek + A/V decode measurements
 */
#include "diag_common.h"
#include <sys/wait.h>

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [codec ...] [--sw] [--res 720|1080] [--frames N] [--perf] [--seek SEC]\n"
        "  codec      h264 | mpeg2 | mpeg4 | hevc | vp9 | av1   (default: h264 mpeg2)\n"
        "  --sw       force the software path (comparison run)\n"
        "  --frames N frames to decode (default 150 = 5 s at 30 fps)\n"
        "  --perf     also measure seek latency and the video/audio decode skew\n"
        "  --seek SEC seek target for --perf (default 2)\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    bool sw = diag_flag(argc, argv, "--sw");
    bool perf = diag_flag(argc, argv, "--perf");
    const char *res = diag_opt(argc, argv, "--res");
    const char *fr = diag_opt(argc, argv, "--frames");
    const char *sk = diag_opt(argc, argv, "--seek");
    int W = 1280, H = 720;
    if (res && atoi(res) == 1080) { W = 1920; H = 1080; }
    int frames = fr ? atoi(fr) : 150;
    double seek_s = sk ? atof(sk) : 2.0;

    const char *wanted[8];
    int nw = 0;
    for (int i = 1; i < argc && nw < 8; i++) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "--res") || !strcmp(argv[i], "--frames") || !strcmp(argv[i], "--seek")) i++;
            continue;
        }
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
    printf("Requested path: %s\n", sw ? "software (--sw)" : "hardware preferred, software fallback");
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
            /* nothing was decoded: every decode row is NOT TESTED, never PASS */
            printf("  Hardware Decode: NOT TESTED\n");
            printf("  Software Decode: NOT TESTED\n");
            printf("  Reason:         %s\n\n", vt.note);
            hw_report_add(&rep, "Decode", wanted[i], true, HW_NOT_TESTED, "%s", vt.note);
            any_untested++;
            continue;
        }
        /* the decoder ran; classify by what it actually did */
        hw_result res_codec;
        const char *verdict;
        if (vt.hw_used) {
            res_codec = HW_PASS;
            verdict = "PASS (a hardware decoder really engaged)";
        } else if (cap == ML_CAP_HW) {
            res_codec = HW_FAIL;
            verdict = "FAIL (silicon has this codec, the decode ran in software)";
        } else {
            /* software decode: the codec plays, but that is not what was asked
             * for — PARTIAL, and the reason is stated */
            res_codec = HW_PARTIAL;
            verdict = cap == ML_CAP_UNKNOWN
                      ? "PARTIAL (decoded in software; hardware support not verified here)"
                      : "PARTIAL (software only: this GPU has no hardware block for it)";
        }
        printf("  Hardware Decode: %s\n", vt.hw_used ? "PASS" : "not engaged");
        printf("  Software Decode: %s\n", vt.hw_used ? "not exercised in this run" : "PASS (this run)");
        printf("  Verdict:        %s\n", verdict);
        printf("  Renderer:       %s\n", or_dash(vt.decoder_line));
        printf("  Dropped Frames: %d\n", vt.dropped);
        printf("  CPU Usage:      %.0f%% of wall (%.0f ms CPU / %.0f ms wall)\n",
               vt.cpu_pct, vt.cpu_ms, vt.wall_ms);
        printf("  FPS:            %.0f at %dx%d\n", vt.fps, W, H);
        if (vt.note[0]) printf("  Note:           %s\n", vt.note);
        putchar('\n');
        hw_report_add(&rep, "Decode", wanted[i], true, res_codec,
                      "%dx%d %d frames: %.0f fps, CPU %.0f%%, decoder=%s, exit=%d",
                      W, H, frames, vt.fps, vt.cpu_pct, vt.hw_used ? "hardware" : "software", rc);
        if (res_codec == HW_PASS) any_pass++;
        else if (res_codec == HW_FAIL) any_fail++;
    }

    if (perf) {
        diag_section("Playback performance (seek + A/V decode skew)");
        for (int i = 0; i < nw; i++) {
            ml_vperf p;
            if (!ml_video_perf_run(&p, wanted[i], W, H, frames, seek_s)) {
                printf("  %-8s NOT TESTED  %s\n", wanted[i], p.note);
                hw_report_add(&rep, "Playback", wanted[i], false, HW_NOT_TESTED, "%s", p.note);
                continue;
            }
            printf("  %-8s seek %.1f s -> first frame %.0f ms | decode video %.0f ms, audio %.0f ms"
                   " (skew %.0f ms)%s\n",
                   wanted[i], p.seek_s, p.seek_ms, p.video_ms, p.audio_ms, p.skew_ms,
                   p.hw_used ? " [hw path engaged]" : "");
            hw_report_add(&rep, "Playback", wanted[i], false, HW_PASS,
                          "%.1f s @%dx%d: seek %.0f ms, video %.0f ms vs audio %.0f ms (decode skew %.0f ms)",
                          p.duration_s, W, H, p.seek_ms, p.video_ms, p.audio_ms, p.skew_ms);
        }
        printf("\n  Note: A/V sync here is decode-completion skew; lip sync needs eyes and\n"
               "        ears on the machine (scripts/hardware-check.sh records it as a human check).\n");
        putchar('\n');
    }

    printf("Overall: %s\n", hw_result_str(hw_report_overall(&rep)));
    if (any_untested && !any_pass && !any_fail)
        printf("\nNothing was decoded on this host. On the iMac run:\n"
               "  maclite-video-test h264 mpeg2 --res 1080 --perf\n"
               "and record the numbers in docs/testing.md.\n");
    (void)argc;
    return hw_report_exit(&rep);
}
