/* mica-settings — System Settings with on-demand panels (spec §38).
 * The panel list is static; a panel's content is built only when opened and
 * freed when closed. Changing the performance mode talks to the compositor,
 * which re-benchmarks and re-tunes animation budgets globally. */
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "../hardware/hwprobe.h"
#include "../hardware/backlight.h"
#include "ml/font.h"
#include "ml/icon.h"

static mica_client *G;
static mica_win *WIN;
static int PANEL;             /* -1 = list */
static ml_source *refresh;

/* Displays panel state. The brightness slider talks to the SAME backend
 * maclite-brightness uses (hardware/backlight.c), so the panel and the tool can
 * never disagree, and a set is only shown as done after sysfs read-back. */
static bl_state BLS;
static bool BLS_READY;
static char BLS_MSG[200];

enum { P_NONE = -1, P_APPEARANCE, P_DISPLAYS, P_DOCK, P_PERF, P_ABOUT };
static const char *PANELS[][2] = {
    { "Appearance",     "app-settings" },
    { "Displays",       "app-display" },
    { "Dock",           "app-dock" },
    { "Performance",    "app-perf" },
    { "About This Mac", "app-about" },
};
#define NPANELS ((int)ML_ARRAY_SIZE(PANELS))

static void row(ml_ctx *c, ml_font *f, int y, const char *label, const char *value)
{
    ml_draw_text(c, f, 220, y, label, 13, ml_rgb(168, 174, 190));
    ml_draw_text(c, f, 400, y, value, 13, ml_rgb(232, 236, 246));
}

static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 0, G->info.mode, true);
    ml_font *f = ml_font_get("mica-sans");
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, 44), ml_rgba(38, 41, 52, 250));
    ml_draw_text(&c, f, 16, 28, PANEL < 0 ? "System Settings" : PANELS[PANEL][0], 15, ml_rgb(240, 244, 252));
    if (PANEL >= 0) ml_draw_text(&c, f, s->w - 70, 28, "< back", 12, ml_rgb(150, 190, 250));
    /* The window background is translucent (v0.1 vibrancy). Panels swap in
     * place, so without an opaque body the previous panel ghosts through the
     * new one. Vibrancy stays at the edges; the content area is solid. */
    ml_fill_rect(&c, ml_rect_make(0, 44, s->w, s->h - 44), ml_rgba(30, 32, 42, 250));

    if (PANEL < 0) {
        for (int i = 0; i < NPANELS; i++) {
            int y = 64 + i * 52;
            ml_fill_rounded(&c, ml_rect_make(16, y, s->w - 32, 44), 8, ml_rgba(52, 56, 70, 160));
            ml_icon_draw(&c, PANELS[i][1], ml_rect_make(28, y + 8, 28, 28), ml_rgba(255, 255, 255, 235));
            ml_draw_text(&c, f, 68, y + 28, PANELS[i][0], 14, ml_rgb(232, 236, 246));
        }
    } else if (PANEL == P_PERF || PANEL == P_APPEARANCE) {
        mica_gpu_info g; mica_cpu_info ci; mica_mem_info mi;
        mica_gpu_probe(&g); mica_cpu_probe(&ci); mica_mem_probe(&mi);
        int y = 80;
        ml_draw_text(&c, f, 24, y, "Performance mode", 14, ml_rgb(240, 244, 252)); y += 28;
        static const char *modes[] = { "Beautiful", "Balanced", "Performance" };
        for (int m = 0; m < 3; m++) {
            bool on = G->info.mode == (uint32_t)m;
            ml_fill_rounded(&c, ml_rect_make(24, y, 150, 34), 8,
                            on ? ml_rgba(70, 130, 250, 220) : ml_rgba(52, 56, 70, 160));
            ml_draw_text(&c, f, 40, y + 23, modes[m], 13, ml_rgb(240, 244, 252));
            y += 42;
        }
        y += 12;
        row(&c, f, y, "GPU", g.present ? g.device : "none"); y += 22;
        row(&c, f, y, "Acceleration", mica_gpu_accel_name(&g)); y += 22;
        row(&c, f, y, "CPU", ci.model); y += 22;
        char ram[64]; snprintf(ram, sizeof ram, "%llu MB total", (unsigned long long)(mi.total_kb / 1024));
        row(&c, f, y, "Memory", ram); y += 22;
        ml_draw_text(&c, f, 24, y + 8, "Mode changes re-tune animation and shadow budgets", 11, ml_rgb(150, 156, 172));
    } else if (PANEL == P_DISPLAYS) {
        char b[96];
        snprintf(b, sizeof b, "%d x %d   scale %u", G->info.screen_w, G->info.screen_h, G->info.scale);
        row(&c, f, 90, "Resolution", b);
        row(&c, f, 118, "Compositor", "damage-tracking; backend per maclite-performance");
        row(&c, f, 146, "Workspaces", "4 (Meta+arrows)");

        if (!BLS_READY) { bl_probe(&BLS); BLS_READY = true; }
        const bl_device *bd = bl_active(&BLS);
        int pct = -1;
        bool readable = bl_get_percent(&BLS, &pct);
        char val[128];
        if (bd && readable)      snprintf(val, sizeof val, "%d%%  (%s)", pct, bd->name);
        else if (bd)             snprintf(val, sizeof val, "unreadable (%s)", bd->name);
        else                     snprintf(val, sizeof val, "not on this machine");
        row(&c, f, 174, "Brightness", val);

        /* the buttons are only live when there is a real device to write to:
         * a machine whose acpi_video0 is a documented no-op must not offer a
         * control that would pretend to work */
        bool usable = bd && bd->writable && !bd->suspect;
        ml_fill_rounded(&c, ml_rect_make(430, 200, 34, 24), 6,
                        usable ? ml_rgba(70, 130, 250, 220) : ml_rgba(60, 64, 78, 140));
        ml_draw_text(&c, f, 443, 217, "-", 15, ml_rgb(240, 244, 252));
        ml_fill_rounded(&c, ml_rect_make(472, 200, 34, 24), 6,
                        usable ? ml_rgba(70, 130, 250, 220) : ml_rgba(60, 64, 78, 140));
        ml_draw_text(&c, f, 485, 217, "+", 15, ml_rgb(240, 244, 252));

        const char *why = !bd ? "no /sys/class/backlight device: brightness cannot be controlled"
                              : bd->suspect ? "device is a documented no-op on this machine (not used)"
                                            : bd->writable ? bl_kind_name(bd->kind) : "device is read-only for this user";
        ml_draw_text(&c, f, 24, 248, why, 11, ml_rgb(150, 156, 172));
        if (BLS_MSG[0]) ml_draw_text(&c, f, 24, 270, BLS_MSG, 11, ml_rgb(196, 204, 220));
        ml_draw_text(&c, f, 24, 294, "same backend as `maclite-brightness` (verified by read-back)", 11,
                     ml_rgb(120, 126, 142));
    } else if (PANEL == P_DOCK) {
        row(&c, f, 90, "Magnification", "event-driven gaussian (spec §35)");
        row(&c, f, 118, "Autohide", "available; toggled from the Dock menu");
        row(&c, f, 146, "Minimise", "genie-lite scale+fade into Dock");
    } else if (PANEL == P_ABOUT) {
        mica_cpu_info ci; mica_mem_info m; mica_gpu_info g;
        mica_cpu_probe(&ci); mica_mem_probe(&m); mica_gpu_probe(&g);
        ml_draw_text(&c, f, 24, 96, "MacLiteOS", 22, ml_rgb(244, 247, 255));
        char v[64]; snprintf(v, sizeof v, "version %s (built %s)", ML_VERSION, __DATE__);
        ml_draw_text(&c, f, 24, 122, v, 13, ml_rgb(168, 174, 190));
        row(&c, f, 160, "Processor", ci.model);
        row(&c, f, 188, "Memory", "see maclite-memory for live per-component use");
        row(&c, f, 216, "Graphics", g.present ? g.device : "software");
        ml_draw_text(&c, f, 24, 260, "An independent, ultra-light desktop inspired by macOS.", 12, ml_rgb(150, 156, 172));
        ml_draw_text(&c, f, 24, 280, "Not affiliated with Apple. No Apple assets are used.", 12, ml_rgb(150, 156, 172));
    }
    mica_win_commit(WIN);
}

/* One brightness step through the shared backend. The message says exactly what
 * happened: a verified read-back, a refusal, or a failure with the reason. */
static void bl_step_ui(int delta)
{
    if (!BLS_READY) { bl_probe(&BLS); BLS_READY = true; }
    int applied = -1;
    bl_set_result r = bl_step(&BLS, delta, &applied);
    const bl_device *bd = bl_active(&BLS);
    if (bl_set_ok(r))
        snprintf(BLS_MSG, sizeof BLS_MSG, "%d%% verified by read-back from %s",
                 applied, bd ? bd->name : "?");
    else if (bd && bd->note[0])
        snprintf(BLS_MSG, sizeof BLS_MSG, "%s: %s", bl_set_result_str(r), bd->note);
    else
        snprintf(BLS_MSG, sizeof BLS_MSG, "%s: nothing was changed", bl_set_result_str(r));
    draw();
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_KEY) {
        if (in->key == 0xff1b || in->key == 'q') { if (PANEL < 0) mica_quit(G, 0); else { PANEL = -1; draw(); } }
        return;
    }
    if (in->kind != IN_DOWN && in->kind != IN_CLICK) return;
    int x = in->x, y = in->y;
    if (PANEL >= 0 && y < 44 && x > WIN->w - 80) { PANEL = -1; draw(); return; }
    if (PANEL < 0) {
        for (int i = 0; i < NPANELS; i++) {
            int ry = 64 + i * 52;
            if (y >= ry && y <= ry + 44) { PANEL = i; draw(); return; }
        }
        return;
    }
    if (PANEL == P_PERF || PANEL == P_APPEARANCE) {
        int y0 = 108;
        for (int m = 0; m < 3; m++) {
            if (x >= 24 && x <= 174 && y >= y0 && y <= y0 + 34) {
                mica_set_mode(G, (uint32_t)m);
                G->info.mode = (uint32_t)m;
                draw();
                return;
            }
            y0 += 42;
        }
        return;
    }
    if (PANEL == P_DISPLAYS) {
        if (y >= 200 && y <= 224 && x >= 430 && x <= 464) bl_step_ui(-10);
        else if (y >= 200 && y <= 224 && x >= 472 && x <= 506) bl_step_ui(+10);
    }
}

int main(void)
{
    ml_log_init("mica-settings", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("settings");
    if (!G) return 1;
    WIN = mica_win_new(G, 640, 460, "System Settings", "settings", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 380, 140);
    WIN->on_input = input;
    PANEL = -1;
    draw();
    return mica_run(G);
}
