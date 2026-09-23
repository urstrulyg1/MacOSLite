/* mica-sysinfo — System Information (spec §50): hardware + live state.
 * All values are read on open and on explicit refresh only. */
#include <stdarg.h>
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "../hardware/hwprobe.h"
#include "ml/font.h"

static mica_client *G;
static mica_win *WIN;
static char LINES[40][96];
static int NL;

static void add(const char *fmt, ...)
{
    if (NL >= 40) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(LINES[NL], sizeof LINES[NL], fmt, ap);
    va_end(ap);
    NL++;
}
static void gather(void)
{
    NL = 0;
    mica_cpu_info c; mica_mem_info m; mica_gpu_info g;
    mica_cpu_probe(&c); mica_mem_probe(&m); mica_gpu_probe(&g);
    add("G1OS System Information — Giving life to older machines.");
    add("");
    add("Processor     %s", c.model);
    add("              %u cores / %u threads, %u MHz max, SSE4.2 %s, AVX %s",
        c.cores, c.threads, c.mhz_max, c.sse42 ? "yes" : "no", c.avx ? "yes" : "no (not required)");
    add("Memory        %llu MB total, %llu MB available", m.total_kb / 1024, m.avail_kb / 1024);
    add("Graphics      %s", g.present ? g.device : "(none detected)");
    add("              driver=%s kms=%s vram=%s", g.driver[0] ? g.driver : "-",
        g.has_kms ? "yes" : "no", g.vram_bytes ? "known" : "unknown");
    add("Decode        %s (hardware table; verify per docs/testing.md)", mica_gpu_accel_name(&g));
    add("Display       %dx%d (compositor backend: see maclite-performance)",
        G->info.screen_w, G->info.screen_h);
    add("Audio         %s", ml_file_exists("/proc/asound/cards") ? "ALSA present" : "not present here");
    add("Wireless      level %d", shell_wifi_level());
    add("Battery       %d%%", shell_battery_pct());
    add("");
    add("Testing note: values above were read on THIS machine.");
    add("Real-iMac measurements live in docs/testing.md and are separate.");
    ml_surface_damage_all(WIN->surf);
}
static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 0, G->info.mode, true);
    ml_font *f = ml_font_get("mica-sans");
    for (int i = 0; i < NL; i++)
        ml_draw_text(&c, f, 18, 30 + i * 19, LINES[i], i == 0 ? 15 : 12,
                     i == 0 ? ml_rgb(244, 247, 255) : ml_rgb(214, 220, 232));
    ml_draw_text(&c, f, s->w - 90, s->h - 12, "r = refresh", 11, ml_rgb(150, 156, 172));
    mica_win_commit(WIN);
}
static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind != IN_KEY) return;
    if (in->key == 0xff1b || in->key == 'q') { mica_quit(G, 0); return; }
    if (in->key == 'r') { gather(); draw(); }
}
int main(void)
{
    ml_log_init("mica-sysinfo", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("sysinfo");
    if (!G) return 1;
    WIN = mica_win_new(G, 620, 560, "System Information", "sysinfo", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 420, 120);
    WIN->on_input = input;
    gather();
    draw();
    return mica_run(G);
}
