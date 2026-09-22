/* mica-player — video player front-end (spec §44).
 * MacLiteOS does not ship its own codec stack. On the iMac the intended path
 * is mpv/gstreamer with VDPAU/VA-API on the Radeon (see docs/HARDWARE.md);
 * this binary delegates to whichever decoder exists and otherwise says so
 * honestly instead of pretending to play. */
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "../hardware/hwprobe.h"
#include "ml/font.h"

static mica_client *G;
static mica_win *WIN;
static char FILEPATH[256];

static bool have(const char *bin)
{
    char cmd[300];
    snprintf(cmd, sizeof cmd, "command -v %s >/dev/null 2>&1", bin);
    return system(cmd) == 0;
}
static void draw(const char *msg)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(12, 12, 14));
    ml_font *f = ml_font_get("mica-sans");
    ml_icon_draw(&c, "app-player", ml_rect_make(s->w / 2 - 32, 60, 64, 64), ml_rgba(255, 255, 255, 200));
    ml_draw_text_box(&c, f, ml_rect_make(20, 150, s->w - 40, 60), msg, 13, ml_rgb(214, 220, 232), ML_ALIGN_CENTER);
    mica_win_commit(WIN);
}
int main(int argc, char **argv)
{
    ml_log_init("mica-player", ML_LOG_WARN, getenv("MICA_LOG"));
    if (argc > 1) snprintf(FILEPATH, sizeof FILEPATH, "%s", argv[1]);
    G = mica_connect("player");
    if (!G) return 1;
    mica_gpu_info g;
    mica_gpu_probe(&g);
    char msg[400];
    if (FILEPATH[0] && have("mpv")) {
        /* real decoder present: hand over, keep HW decode request explicit */
        char cmd[512];
        snprintf(cmd, sizeof cmd, "mpv --hwdec=%s --fullscreen '%s' &",
                 !strcmp(mica_gpu_accel_name(&g), "VDPAU") ? "vdpau" : "auto", FILEPATH);
        mica_launch(G, cmd);
        snprintf(msg, sizeof msg, "Handed %s to mpv with hardware decode (%s).",
                 ml_path_base(FILEPATH), mica_gpu_accel_name(&g));
        draw(msg);
        return mica_run(G);
    }
    snprintf(msg, sizeof msg,
             "No video decoder on this host.\nOn the iMac (Radeon HD 4670/5670) playback uses VDPAU/VA-API H.264 + MPEG-2.\nFile: %s",
             FILEPATH[0] ? ml_path_base(FILEPATH) : "(none)");
    draw(msg);
    return mica_run(G);
}
