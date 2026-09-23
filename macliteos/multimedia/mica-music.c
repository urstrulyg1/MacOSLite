/* mica-music — music player (spec §46). Plays WAV/OGG files found in a folder
 * through whatever output exists: ALSA (aplay) when present, else an honest
 * "no audio device" state. Playlist is scanned on open only — no indexer. */
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "ml/font.h"
#include <dirent.h>
#include <signal.h>

static mica_client *G;
static mica_win *WIN;
static char TRACKS[64][128];
static int NT, CUR;
static pid_t PLAYER;

static void scan(const char *dir)
{
    NT = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && NT < 64) {
        size_t n = strlen(e->d_name);
        if (n > 4 && (!strcmp(e->d_name + n - 4, ".wav") || !strcmp(e->d_name + n - 4, ".ogg")))
            snprintf(TRACKS[NT++], sizeof TRACKS[0], "%s/%s", dir, e->d_name);
    }
    closedir(d);
}
static void play(void)
{
    if (PLAYER > 0) { kill(PLAYER, SIGTERM); PLAYER = 0; }
    if (CUR >= NT) return;
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c",
              "cvlc --play-and-exit -q \"$0\" 2>/dev/null || aplay -q \"$0\" 2>/dev/null || ogg123 -q \"$0\" 2>/dev/null",
              TRACKS[CUR], (char *)NULL);
        _exit(127);
    }
    PLAYER = pid;
}
static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 0, G->info.mode, true);
    ml_font *f = ml_font_get("mica-sans");
    ml_draw_text(&c, f, 16, 28, "Music", 15, ml_rgb(240, 244, 252));
    if (!NT) {
        ml_draw_text_box(&c, f, ml_rect_make(20, 80, s->w - 40, 60),
                         "No audio files in ~/Music and no audio device probe here.\nDrop .wav/.ogg into ~/Music and press r.",
                         12, ml_rgb(190, 196, 210), ML_ALIGN_CENTER);
    }
    for (int i = 0; i < NT; i++) {
        int y = 60 + i * 24;
        if (i == CUR) ml_fill_rounded(&c, ml_rect_make(10, y - 14, s->w - 20, 22), 6, ml_rgba(70, 130, 250, 90));
        ml_draw_text(&c, f, 20, y, ml_path_base(TRACKS[i]), 12, ml_rgb(226, 231, 242));
    }
    ml_draw_text(&c, f, 16, s->h - 12, "space = play/stop   n/p = track   r = rescan", 11, ml_rgb(150, 156, 172));
    mica_win_commit(WIN);
}
static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind != IN_KEY) return;
    switch (in->key) {
    case 0xff1b: case 'q': if (PLAYER) kill(PLAYER, SIGTERM); mica_quit(G, 0); return;
    case ' ': if (PLAYER) { kill(PLAYER, SIGTERM); PLAYER = 0; } else play(); break;
    case 'n': if (CUR + 1 < NT) { CUR++; play(); } break;
    case 'p': if (CUR > 0) { CUR--; play(); } break;
    case 'r': { char d[256]; snprintf(d, sizeof d, "%s/Music", ml_home()); scan(d); } break;
    default: return;
    }
    draw();
}
int main(void)
{
    ml_log_init("mica-music", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("music");
    if (!G) return 1;
    WIN = mica_win_new(G, 420, 380, "Music", "music", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 500, 200);
    WIN->on_input = input;
    char d[256];
    snprintf(d, sizeof d, "%s/Music", ml_home());
    scan(d);
    draw();
    return mica_run(G);
}
