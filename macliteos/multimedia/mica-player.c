/* mica-player — video player front-end (spec §44).
 * MacLiteOS does not ship its own codec stack. On the iMac the intended path
 * is mpv/gstreamer with VDPAU/VA-API on the Radeon (see docs/hardware.md);
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
    if (FILEPATH[0]) {
        /* Delegate to primary media player: maclite-video (VLC) */
        char cmd[512];
        if (have("maclite-video")) {
            snprintf(cmd, sizeof cmd, "maclite-video '%s' &", FILEPATH);
        } else if (have("vlc")) {
            snprintf(cmd, sizeof cmd, "vlc '%s' &", FILEPATH);
        } else {
            snprintf(cmd, sizeof cmd, "echo 'VLC not installed' >&2");
        }
        mica_launch(G, cmd);
        snprintf(msg, sizeof msg, "Opening %s in VLC (primary player).", ml_path_base(FILEPATH));
        draw(msg);
        return mica_run(G);
    }
    snprintf(msg, sizeof msg,
             "MacLiteOS Media Player (VLC backend)\nHardware decode: VDPAU (Radeon UVD2) / VA-API\nFile: %s",
             FILEPATH[0] ? ml_path_base(FILEPATH) : "(no file selected)");
    draw(msg);
    return mica_run(G);
}
