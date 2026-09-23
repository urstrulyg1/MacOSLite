/* maclite-video — Primary local multimedia player (VLC) and video diagnostic utility.
 *
 * Implements the minimal multimedia stack requirements (spec §9, §10, §15, §16):
 *   - Unified local media player powered by VLC
 *   - Auto-configures hardware acceleration (VDPAU on Radeon UVD2, VA-API where supported,
 *     or software decode fallback)
 *   - Eliminates duplicate players (replaces multiple competing media engines)
 *   - Diagnostic functionality reporting runtime hardware acceleration and decoders
 *   - Performance mode integration (beautiful, balanced, performance)
 *
 * Usage:
 *   maclite-video [file]
 *   maclite-video --audio [file]
 *   maclite-video --diagnostics | diagnostics
 *   maclite-video status
 */
#include "ml/common.h"
#include "ml/log.h"
#include "ml/util.h"
#include "../hardware/hwprobe.h"
#include "../hardware/media.h"
#include "../hardware/hwcap.h"
#include <unistd.h>

static bool command_exists(const char *cmd)
{
    char buf[300];
    snprintf(buf, sizeof buf, "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

static const char *get_active_mode(void)
{
    const char *m = getenv("MICA_MODE");
    if (m && m[0]) return m;
    FILE *f = fopen("/etc/maca-lite/mode", "r");
    if (f) {
        static char mode_buf[32];
        if (fgets(mode_buf, sizeof mode_buf, f)) {
            char *nl = strchr(mode_buf, '\n');
            if (nl) *nl = 0;
            fclose(f);
            return mode_buf;
        }
        fclose(f);
    }
    return "balanced";
}

/* Diagnostic command (spec §9) */
static void run_diagnostics(void)
{
    mica_gpu_info g;
    mica_gpu_probe(&g);

    ml_hwdec_state hd;
    ml_hwdec_probe(&hd);

    char h264_detail[192];
    ml_cap cap = ml_codec_capability(g.vendor_id, g.device_id, ML_CODEC_H264, &hd,
                                     h264_detail, sizeof h264_detail);

    bool have_vlc = command_exists("vlc");
    char vlc_ver[128] = "not installed";
    if (have_vlc) {
        FILE *p = popen("vlc --version 2>/dev/null | head -1", "r");
        if (p) {
            if (fgets(vlc_ver, sizeof vlc_ver, p)) {
                char *nl = strchr(vlc_ver, '\n');
                if (nl) *nl = 0;
            }
            pclose(p);
        }
    }

    printf("=====================================================\n");
    printf("MacLiteOS Video & Local Playback Diagnostics (spec §9)\n");
    printf("=====================================================\n");
    printf("VLC installed:          %s (%s)\n", have_vlc ? "YES" : "NO", vlc_ver);
    printf("Decoder:                %s\n", have_vlc ? "VLC (libavcodec backend)" : "NOT INSTALLED");
    
    /* Hardware acceleration */
    if (g.vendor_id == 0x1002 && (g.device_id == 0x9488 || g.device_id == 0x68d8)) {
        if (hd.vdpau_lib && hd.probed) {
            printf("Hardware acceleration:  PASS (VDPAU UVD2 hardware acceleration active)\n");
        } else if (hd.vdpau_lib) {
            printf("Hardware acceleration:  NOT TESTED (VDPAU library present; hardware query unrun)\n");
        } else {
            printf("Hardware acceleration:  UNSUPPORTED (libvdpau not present; using software decode)\n");
        }
    } else if (hd.vaapi_lib && hd.probed) {
        printf("Hardware acceleration:  PASS (VA-API hardware acceleration active)\n");
    } else if (g.gl_probed && !g.gl_software) {
        printf("Hardware acceleration:  PARTIAL (OpenGL scanout ready; video decode on CPU)\n");
    } else {
        printf("Hardware acceleration:  UNSUPPORTED (software decode fallback)\n");
    }

    printf("GPU:                    %s\n", g.present ? g.device : "NOT AVAILABLE");
    printf("Driver:                 %s\n", g.driver[0] ? g.driver : "none");
    printf("Codec:                  H.264 (%s), MPEG-2, AAC, MP3, FLAC, OGG, WAV\n",
           cap == ML_CAP_HW ? "HW accelerated" : "software fallback");
    printf("Performance mode:       %s\n", get_active_mode());
    printf("=====================================================\n");
}

/* Play media file using VLC */
static void play_media(int argc, char **argv)
{
    if (!command_exists("vlc")) {
        fprintf(stderr, "maclite-video: VLC media player is not installed.\n"
                        "VLC is the primary media player for MacLiteOS.\n");
        exit(1);
    }

    mica_gpu_info g;
    mica_gpu_probe(&g);

    ml_hwdec_state hd;
    ml_hwdec_probe(&hd);

    const char *mode = get_active_mode();

    char cmd[2048];
    size_t len = 0;
    len += snprintf(cmd + len, sizeof cmd - len, "vlc");

    /* Auto-configure hardware decode (spec §9) */
    if (g.vendor_id == 0x1002 && hd.vdpau_lib) {
        /* ATI Radeon HD 4670/5670 UVD2 engine via VDPAU */
        len += snprintf(cmd + len, sizeof cmd - len, " --avcodec-hw=vdpau");
    } else if (hd.vaapi_lib) {
        len += snprintf(cmd + len, sizeof cmd - len, " --avcodec-hw=vaapi");
    } else {
        len += snprintf(cmd + len, sizeof cmd - len, " --avcodec-hw=none");
    }

    /* Integration with performance mode (spec §16) */
    if (!strcmp(mode, "performance")) {
        /* Drop late frames, disable OSD animations, reduce post-processing */
        len += snprintf(cmd + len, sizeof cmd - len,
                        " --no-video-title-show --drop-late-frames --skip-frames");
    } else if (!strcmp(mode, "balanced")) {
        len += snprintf(cmd + len, sizeof cmd - len, " --no-video-title-show");
    }

    /* Pass media files */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--diagnostics") || !strcmp(argv[i], "diagnostics") ||
            !strcmp(argv[i], "status"))
            continue;
        len += snprintf(cmd + len, sizeof cmd - len, " '%s'", argv[i]);
    }

    len += snprintf(cmd + len, sizeof cmd - len, " &");

    printf("maclite-video: launching VLC (mode: %s)...\n", mode);
    system(cmd);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "--diagnostics") || !strcmp(argv[1], "diagnostics") || !strcmp(argv[1], "-d")) {
            run_diagnostics();
            return 0;
        }
        if (!strcmp(argv[1], "status") || !strcmp(argv[1], "--status")) {
            run_diagnostics();
            return 0;
        }
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            printf("Usage: maclite-video [file]\n"
                   "       maclite-video --diagnostics      (runtime video decoder diagnostics)\n"
                   "       maclite-video status             (check player status)\n");
            return 0;
        }
    }

    play_media(argc, argv);
    return 0;
}
