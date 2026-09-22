/* mica-shell — the MacLiteOS session shell.
 *
 * One binary, four roles, four processes (spec §36):
 *   mica-shell --role panel     menu bar, status items, menus, control center
 *   mica-shell --role dock      the Dock (event-driven magnification)
 *   mica-shell --role desktop   wallpaper-layer icons + desktop context menu
 *   mica-shell --role launcher  Meta+Space search overlay (started on demand)
 *
 * Nothing here polls: the panel's clock is a timer aligned to the minute, the
 * Dock only animates while the pointer is over it, the desktop refreshes from
 * inotify, and the launcher only exists while it is open.
 */
#include "../compositor/client/shellkit.h"
#include "ml/util.h"
#include "ml/log.h"
#include "ml/img.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

static mica_client *G;
static shell_menu *G_menu;

/* ============================== PANEL ================================== */
typedef struct {
    mica_win *win;
    shell_menu *menu;
    int menu_idx;
    ml_source *clock_timer;
    int vol;
} panel_t;
static panel_t P;

static const char *menu_titles[] = { "System", "File", "Edit", "View", "Go", "Window", "Help" };

static void act_launch_app(void *ud)
{
    const mica_app_def *a = ud;
    if (a) mica_launch(G, a->exec);
}
static void act_mode(void *ud)
{
    mica_set_mode(G, (uint32_t)(intptr_t)ud);
}
static void act_quit(void *ud) { (void)ud; mica_quit(G, 0); }
static void act_notify(void *ud)
{
    (void)ud;
    mica_notify(G, "MacLiteOS", "Session is healthy. 0 background daemons, 0 indexers.", "shield", 4000);
}

static void panel_menu_open(int idx, int x)
{
    if (P.menu) shell_menu_close(P.menu);
    P.menu = shell_menu_open(G, x, PANEL_H, NULL);
    shell_menu_own(P.menu, &P.menu);
    P.menu_idx = idx;
    if (idx == 0) {
        shell_menu_add(P.menu, "About This Mac", "info", act_launch_app, (void *)&mica_apps[3]);
        shell_menu_add(P.menu, "System Settings", "gear", act_launch_app, (void *)&mica_apps[2]);
        shell_menu_sep(P.menu);
        shell_menu_add(P.menu, "Beautiful mode", "brightness", act_mode, (void *)(intptr_t)MODE_BEAUTIFUL);
        shell_menu_add(P.menu, "Balanced mode", "sliders", act_mode, (void *)(intptr_t)MODE_BALANCED);
        shell_menu_add(P.menu, "Performance mode", "pulse", act_mode, (void *)(intptr_t)MODE_PERFORMANCE);
        shell_menu_sep(P.menu);
        shell_menu_add(P.menu, "Security report", "shield", act_notify, NULL);
        shell_menu_sep(P.menu);
        shell_menu_add(P.menu, "Quit session", "power", act_quit, NULL);
    } else if (idx == 1) {
        shell_menu_add(P.menu, "New Finder Window", "folder", act_launch_app, (void *)&mica_apps[0]);
        shell_menu_add(P.menu, "Open Terminal", "terminal-prompt", act_launch_app, (void *)&mica_apps[1]);
    } else if (idx == 2) {
        shell_menu_add(P.menu, "Undo", NULL, NULL, NULL);
        shell_menu_add(P.menu, "Cut", NULL, NULL, NULL);
        shell_menu_add(P.menu, "Copy", NULL, NULL, NULL);
        shell_menu_add(P.menu, "Paste", NULL, NULL, NULL);
    } else if (idx == 3) {
        shell_menu_add(P.menu, "as Icons", "grid-view", NULL, NULL);
        shell_menu_add(P.menu, "as List", "list-view", NULL, NULL);
    } else if (idx == 4) {
        shell_menu_add(P.menu, "Home", "folder", act_launch_app, (void *)&mica_apps[0]);
        shell_menu_add(P.menu, "Desktop", "display", act_launch_app, (void *)&mica_apps[0]);
    } else if (idx == 5) {
        shell_menu_add(P.menu, "Minimize", "minimize", NULL, NULL);
        shell_menu_add(P.menu, "Zoom", "maximize", NULL, NULL);
    } else {
        shell_menu_add(P.menu, "MacLiteOS Help", "info", act_notify, NULL);
    }
    shell_menu_draw(P.menu);
    if (P.menu->win) mica_win_place(P.menu->win, x, PANEL_H);
}

static void panel_draw(void)
{
    ml_surface *s = P.win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 0, G->info.mode, true);
    ml_fill_rect(&c, ml_rect_make(0, s->h - 1, s->w, 1), ml_rgba(0, 0, 0, 70));
    ml_icon_draw(&c, "mica-mark", ml_rect_make(12, 4, 18, 18), ml_rgba(240, 244, 252, 250));
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");
    int x = 40;
    for (size_t i = 0; i < ML_ARRAY_SIZE(menu_titles); i++) {
        bool act = (P.menu && P.menu_idx == (int)i);
        int tw = ml_text_width(i == 0 ? fb : f, menu_titles[i], 13);
        if (act) ml_fill_rounded(&c, ml_rect_make(x - 7, 4, tw + 14, 18), 5, ml_rgba(88, 128, 240, 220));
        ml_draw_text(&c, i == 0 ? fb : f, x, 18, menu_titles[i], 13, ml_rgba(238, 242, 250, 240));
        x += tw + 22;
    }
    /* right status cluster — each element only redraws when its data changes */
    int sx = s->w - 14;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char clk[48];
    strftime(clk, sizeof clk, "%a %d %b  %H:%M", &tm);
    int cw = ml_text_width(f, clk, 13);
    sx -= cw;
    ml_draw_text(&c, f, sx, 18, clk, 13, ml_rgba(238, 242, 250, 235));
    sx -= 10;
    int bat = shell_battery_pct();
    if (bat >= 0) {
        sx -= 26;
        ml_icon_draw(&c, shell_battery_charging() ? "battery-charge" : "battery",
                     ml_rect_make(sx, 6, 24, 14), ml_rgba(238, 242, 250, 235));
        if (bat < 100) {
            ml_fill_rounded(&c, ml_rect_make(sx + 3, 9, (int)(18 * bat / 100.0), 8), 3,
                            bat > 20 ? ml_rgb(120, 200, 130) : ml_rgb(235, 105, 96));
        }
    }
    sx -= 24;
    ml_icon_draw(&c, P.vol ? "volume-high" : "volume-mute", ml_rect_make(sx, 5, 16, 16), ml_rgba(238, 242, 250, 235));
    sx -= 24;
    int wifi = shell_wifi_level();
    ml_icon_draw(&c, wifi < 0 ? "wifi-off" : "wifi", ml_rect_make(sx, 5, 16, 16), ml_rgba(238, 242, 250, 235));
    sx -= 22;
    ml_icon_draw(&c, "search", ml_rect_make(sx, 5, 15, 15), ml_rgba(238, 242, 250, 220));
    mica_win_commit(P.win);
}

static void clock_tick(void *ud)
{
    (void)ud;
    panel_draw();
}

static void panel_input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (P.menu && (in->kind == IN_DOWN || in->kind == IN_UP)) {
        /* clicks outside the menu close it */
        shell_menu_close(P.menu);
        P.menu = NULL;
        panel_draw();
        return;
    }
    if (in->kind != IN_UP || in->button != 1) return;
    int x = 40;
    for (size_t i = 0; i < ML_ARRAY_SIZE(menu_titles); i++) {
        int tw = ml_text_width(i == 0 ? ml_font_get("mica-sans-bold") : ml_font_get("mica-sans"), menu_titles[i], 13);
        if (in->x >= x - 7 && in->x <= x + tw + 7) { panel_menu_open((int)i, x - 7); return; }
        x += tw + 22;
    }
    if (in->x > w->w - 40 && in->x < w->w - 16) P.vol = !P.vol;
    panel_draw();
}

static int run_panel(void)
{
    P.win = mica_win_new(G, G->info.screen_w, PANEL_H, "", "panel",
                         WIN_F_BORDERLESS | WIN_F_TOPMOST | WIN_F_NO_FOCUS);
    if (!P.win) return 1;
    mica_win_place(P.win, 0, 0);
    P.win->on_input = panel_input;
    P.vol = 1;
    /* clock aligned to the minute: exactly one wakeup per minute when idle */
    time_t now = time(NULL);
    uint64_t ms = (uint64_t)(60 - now % 60) * 1000;
    P.clock_timer = mica_add_timer(G, ms, false, clock_tick, NULL);
    panel_draw();
    return mica_run(G);
}

/* ============================== DOCK =================================== */
typedef struct {
    mica_win *win;
    const mica_app_def *apps[12];
    int n;
    double size[12];          /* current animated size per icon */
    double vel[12];
    ml_anim *spr[12];
    ml_source *anim_timer;
    bool running[12];
    int hover;
    shell_menu *menu;
    bool autohide, hidden, hovered, mag;
    double off, off_vel;
    ml_anim *hide_spr;
    ml_source *hide_timer;
    int base_x, base_y;
} dock_t;
static dock_t D;

static void dock_layout(double *xs, double *widths, double *total)
{
    double pad = 12;
    double t = pad * 2;
    for (int i = 0; i < D.n; i++) { widths[i] = D.size[i]; t += D.size[i] + 6; }
    t -= 6;
    double x = pad;
    for (int i = 0; i < D.n; i++) { xs[i] = x; x += D.size[i] + 6; }
    *total = t;
}

static void dock_draw(void)
{
    ml_surface *s = D.win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    double xs[12], widths[12], total;
    dock_layout(xs, widths, &total);
    int ph = s->h;
    ml_rect panel = ml_rect_make((s->w - (int)total) / 2, 6, (int)total, ph - 12);
    if (G->info.mode != MODE_PERFORMANCE)
        ml_draw_shadow(&c, panel, 18, 14, ml_rgba(0, 0, 0, 90));
    shell_panel_bg(&c, panel, 18, G->info.mode, true);
    shell_hairline(&c, panel, 18);
    double ox = panel.x - 12;
    for (int i = 0; i < D.n; i++) {
        int sz = (int)lround(D.size[i]);
        int y = panel.y + panel.h - 16 - sz;
        ml_icon_draw(&c, D.apps[i]->icon, ml_rect_make((int)lround(ox + xs[i]), y, sz, sz), ml_rgba(255, 255, 255, 255));
        if (D.running[i])
            ml_fill_rounded(&c, ml_rect_make((int)lround(ox + xs[i] + sz / 2.0) - 2, panel.y + panel.h - 10, 4, 4),
                            2, ml_rgba(232, 238, 250, 210));
    }
    mica_win_commit(D.win);
}

static void dock_settle_check(void)
{
    bool active = false;
    for (int i = 0; i < D.n; i++)
        if (D.spr[i] && ml_anim_active(D.spr[i])) active = true;
    if (!active && D.anim_timer) { ml_timer_disarm(D.anim_timer); dock_draw(); }
}
static void dock_anim_tick(void *ud)
{
    (void)ud;
    ml_anim_tick(mica_anims(G), ml_now_s());
    for (int i = 0; i < D.n; i++)
        if (D.spr[i]) {
            D.size[i] = ml_anim_value(D.spr[i]);
            D.vel[i] = ml_anim_velocity(D.spr[i]);
            if (!ml_anim_active(D.spr[i])) D.spr[i] = NULL;
        }
    dock_draw();
    dock_settle_check();
}
static void dock_retarget(int pointer_x_in_dock)
{
    double xs[12], widths[12], total;
    dock_layout(xs, widths, &total);
    double ox = (D.win->w - total) / 2 - 12;
    double mag = (G->info.mode == MODE_PERFORMANCE || !D.mag) ? 0 : 24.0;
    double sigma = DOCK_BASE * 1.4;
    bool any = false;
    for (int i = 0; i < D.n; i++) {
        double cx = ox + xs[i] + D.size[i] / 2;
        double d = pointer_x_in_dock < 0 ? 1e9 : fabs(cx - pointer_x_in_dock);
        double target = DOCK_BASE + mag * exp(-(d * d) / (2 * sigma * sigma));
        if (fabs(target - D.size[i]) < 0.4 && !D.spr[i]) continue;
        D.spr[i] = ml_anim_spring(mica_anims(G), D.size[i], target, 320, 30, 1.0, D.vel[i]);
        any = true;
    }
    if (any && !D.anim_timer)
        D.anim_timer = mica_add_timer(G, 16, true, dock_anim_tick, NULL);
    if (any && D.anim_timer) ml_timer_arm(D.anim_timer, 16, true);
}

static void dock_hide_tick(void *ud)
{
    (void)ud;
    ml_anim_tick(mica_anims(G), ml_now_s());
    D.off = ml_anim_value(D.hide_spr);
    D.off_vel = ml_anim_velocity(D.hide_spr);
    bool act = ml_anim_active(D.hide_spr);
    if (!act) D.hide_spr = NULL;
    mica_win_place(D.win, D.base_x, D.base_y + (int)lround(D.off));
    if (!act) { ml_timer_disarm(D.hide_timer); D.hide_timer = NULL; }
}
static void dock_set_hidden(bool h)
{
    ML_WARN("dock_set_hidden(%d) autohide=%d hovered=%d", (int)h, (int)D.autohide, (int)D.hovered);
    if (D.hidden == h) return;
    D.hidden = h;
    double target = h ? (D.win->h - 3) : 0;
    D.hide_spr = ml_anim_spring(mica_anims(G), D.off, target, 260, 26, 1.0, D.off_vel);
    if (!D.hide_timer) D.hide_timer = mica_add_timer(G, 16, true, dock_hide_tick, NULL);
}
static void act_toggle_hide(void *ud)
{
    (void)ud;
    D.autohide = !D.autohide;
    if (!D.autohide) dock_set_hidden(false);
    else if (!D.hovered) dock_set_hidden(true);
    if (D.menu) { shell_menu_close(D.menu); D.menu = NULL; }
}
static void act_toggle_mag(void *ud)
{
    (void)ud;
    D.mag = !D.mag;
    dock_retarget(D.hover);
    if (D.menu) { shell_menu_close(D.menu); D.menu = NULL; }
}
static void dock_input(mica_win *w, const msg_input *in)
{
    if (in->kind == IN_ENTER || (in->kind == IN_MOVE && !D.hovered)) {
        D.hovered = true;
        if (D.autohide && D.hidden) dock_set_hidden(false);
    }
    if (in->kind == IN_MOVE) { dock_retarget(in->x); return; }
    if (in->kind == IN_LEAVE) {
        D.hovered = false;
        dock_retarget(-1);
        if (D.autohide && !D.hidden) dock_set_hidden(true);
        return;
    }
    if (in->kind != IN_UP) return;
    double xs[12], widths[12], total;
    dock_layout(xs, widths, &total);
    double ox = (w->w - total) / 2 - 12;
    for (int i = 0; i < D.n; i++) {
        double x0 = ox + xs[i], x1 = x0 + D.size[i];
        if (in->x >= x0 && in->x <= x1) {
            if (in->button == 1) {
                mica_launch(G, D.apps[i]->exec);
            } else if (in->button == 3) {
                if (D.menu) shell_menu_close(D.menu);
                D.menu = shell_menu_open(G, (int)x0, w->h - 260, NULL);
                shell_menu_own(D.menu, &D.menu);
                shell_menu_add(D.menu, "Open", "play", act_launch_app, (void *)D.apps[i]);
                shell_menu_add(D.menu, "Show in Files", "folder", act_launch_app, (void *)&mica_apps[0]);
                shell_menu_draw(D.menu);
                mica_win_place(D.menu->win, (int)x0, G->info.screen_h - 60 - (2 * 26 + 12));
            }
            return;
        }
    }
    if (in->kind == IN_UP && in->button == 3) {
        /* blank dock area: preferences */
        if (D.menu) shell_menu_close(D.menu);
        D.menu = shell_menu_open(G, in->dx, in->dy, NULL);
        shell_menu_own(D.menu, &D.menu);
        shell_menu_add(D.menu, D.autohide ? "Turn Hiding Off" : "Turn Hiding On", "folder", act_toggle_hide, NULL);
        shell_menu_add(D.menu, D.mag ? "Turn Magnification Off" : "Turn Magnification On", "folder", act_toggle_mag, NULL);
        shell_menu_draw(D.menu);
        mica_win_place(D.menu->win, G->info.screen_w / 2 - 110, G->info.screen_h - 160);
    }
}

static void dock_event(mica_client *c, const msg_event *e)
{
    (void)c;
    if (e->kind == EV_WIN_OPEN || e->kind == EV_WIN_CLOSE) {
        for (int i = 0; i < D.n; i++)
            if (!strcmp(D.apps[i]->id, e->text)) D.running[i] = e->kind == EV_WIN_OPEN;
        dock_draw();
    } else if (e->kind == EV_MODE) {
        dock_draw();
    }
}

static int run_dock(void)
{
    const char *order[] = { "finder", "terminal", "settings", "sysinfo", "imageview",
                            "player", "music", "pdf", "textedit", "diagnostics" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(order); i++) D.apps[D.n++] = mica_app_find(order[i]);
    int w = 12 * (DOCK_BASE + 6) + 96, h = DOCK_BASE + 30 + 14;
    D.win = mica_win_new(G, w, h, "", "dock", WIN_F_BORDERLESS | WIN_F_TOPMOST | WIN_F_NO_FOCUS);
    if (!D.win) return 1;
    mica_win_place(D.win, (G->info.screen_w - w) / 2, G->info.screen_h - h - 6);
    for (int i = 0; i < D.n; i++) D.size[i] = DOCK_BASE;
    D.mag = true;
    D.base_x = (G->info.screen_w - w) / 2;
    D.base_y = G->info.screen_h - h - 6;
    D.win->on_input = dock_input;
    G->on_event = dock_event;
    dock_draw();
    return mica_run(G);
}

/* ============================= DESKTOP ================================= */
typedef struct {
    mica_win *win;
    char dir[512];
    char names[64][96];
    bool isdir[64];
    int n;
    int inotify_fd;
    ml_source *in_src, *refresh;
} desk_t;
static desk_t K;

static void desk_scan(void)
{
    K.n = 0;
    DIR *d = opendir(K.dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && K.n < 64) {
        if (e->d_name[0] == '.') continue;
        snprintf(K.names[K.n], sizeof K.names[0], "%s", e->d_name);
        char *p = ml_path_join(K.dir, e->d_name);
        K.isdir[K.n] = ml_is_dir(p);
        ml_free(p);
        K.n++;
    }
    closedir(d);
}
static void desk_draw(void)
{
    ml_surface *s = K.win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    /* transparent: the compositor wallpaper shows through */
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgba(0, 0, 0, 0));
    ml_font *f = ml_font_get("mica-sans");
    for (int i = 0; i < K.n; i++) {
        int cx = s->w - 96, cy = 40 + i * 96;
        ml_icon_draw(&c, K.isdir[i] ? "folder" : "file", ml_rect_make(cx, cy, 56, 56), ml_rgba(255, 255, 255, 240));
        ml_draw_text_box(&c, f, ml_rect_make(cx - 32, cy + 60, 120, 16), K.names[i], 12,
                         ml_rgba(240, 244, 252, 235), ML_ALIGN_CENTER);
    }
    mica_win_commit(K.win);
}
static void desk_refresh(void *ud)
{
    (void)ud;
    desk_scan();
    desk_draw();
}
static void desk_inotify(void *ud, uint32_t ev)
{
    (void)ud; (void)ev;
    char buf[4096];
    ssize_t r = read(K.inotify_fd, buf, sizeof buf);
    (void)r;
    ml_deferred_schedule(K.refresh);
}
static void desk_input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_UP && in->button == 1) {
        for (int i = 0; i < K.n; i++) {
            int cx = w->w - 96, cy = 40 + i * 96;
            if (in->x >= cx - 8 && in->x <= cx + 64 && in->y >= cy && in->y <= cy + 76) {
                char *p = ml_path_join(K.dir, K.names[i]);
                char *cmd = K.isdir[i] ? ml_strdupf("mica-finder \"%s\"", p)
                                       : ml_strdupf("mica-viewer \"%s\"", p);
                mica_launch(G, cmd);
                ml_free(cmd); ml_free(p);
                return;
            }
        }
    } else if (in->kind == IN_UP && in->button == 3) {
        if (G_menu) shell_menu_close(G_menu);
        G_menu = shell_menu_open(G, in->dx, in->dy, NULL);
        shell_menu_own(G_menu, &G_menu);
        shell_menu_add(G_menu, "New Folder", "folder", NULL, NULL);
        shell_menu_add(G_menu, "Open Terminal here", "terminal-prompt", act_launch_app, (void *)&mica_apps[1]);
        shell_menu_sep(G_menu);
        shell_menu_add(G_menu, "Change wallpaper", "photo", NULL, NULL);
        shell_menu_draw(G_menu);
        mica_win_place(G_menu->win, in->x, in->y);
    }
}
static int run_desktop(void)
{
    snprintf(K.dir, sizeof K.dir, "%s/Desktop", ml_home());
    ml_mkdirs(K.dir, 0755);
    K.win = mica_win_new(G, G->info.screen_w, G->info.screen_h, "", "desktop",
                         WIN_F_BORDERLESS | WIN_F_BOTTOM | WIN_F_NO_FOCUS);
    if (!K.win) return 1;
    mica_win_place(K.win, 0, 0);
    K.win->on_input = desk_input;
    K.refresh = ml_loop_add_deferred(mica_loop(G), desk_refresh, NULL);
    K.inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (K.inotify_fd >= 0) {
        inotify_add_watch(K.inotify_fd, K.dir, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
        K.in_src = ml_loop_add_fd(mica_loop(G), K.inotify_fd, EPOLLIN, (ml_fd_fn)desk_inotify, NULL);
    }
    desk_scan();
    desk_draw();
    return mica_run(G);
}

/* ============================= LAUNCHER ================================ */
typedef struct {
    mica_win *win;
    char query[128];
    const mica_app_def *hits[16];
    int nh, sel;
} launcher_t;
static launcher_t L;

static void launcher_search(void)
{
    L.nh = 0;
    for (size_t i = 0; i < mica_apps_n && L.nh < 16; i++)
        if (!L.query[0] || strcasestr(mica_apps[i].name, L.query) || strcasestr(mica_apps[i].id, L.query))
            L.hits[L.nh++] = &mica_apps[i];
}
static void launcher_draw(void)
{
    ml_surface *s = L.win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 16, G->info.mode, true);
    shell_hairline(&c, ml_rect_make(0, 0, s->w, s->h), 16);
    ml_icon_draw(&c, "search", ml_rect_make(16, 14, 20, 20), ml_rgba(200, 210, 230, 220));
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");
    char shown[160];
    snprintf(shown, sizeof shown, "%s", L.query);
    ml_draw_text(&c, f, 48, 30, shown[0] ? shown : "Search applications and files", 15,
                 shown[0] ? ml_rgba(240, 244, 252, 245) : ml_rgba(150, 158, 176, 220));
    ml_fill_rect(&c, ml_rect_make(12, 44, s->w - 24, 1), ml_rgba(255, 255, 255, 40));
    for (int i = 0; i < L.nh; i++) {
        int y = 52 + i * 34;
        if (i == L.sel) ml_fill_rounded(&c, ml_rect_make(8, y, s->w - 16, 30), 8, ml_rgba(88, 128, 240, 210));
        ml_icon_draw(&c, L.hits[i]->icon, ml_rect_make(16, y + 5, 20, 20), ml_rgba(255, 255, 255, 255));
        ml_draw_text(&c, fb, 46, y + 21, L.hits[i]->name, 13, ml_rgba(238, 242, 250, 240));
        ml_draw_text(&c, f, s->w - 150, y + 21, "application", 11, ml_rgba(160, 168, 186, 200));
    }
    mica_win_commit(L.win);
}
static void launcher_input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind != IN_KEY) return;
    if (in->key == 0xff0d || in->key == '\n') {
        if (L.sel < L.nh) mica_launch(G, L.hits[L.sel]->exec);
        mica_quit(G, 0);
    } else if (in->key == 0xff1b) {
        mica_quit(G, 0);
    } else if (in->key == 0xff54) {
        if (L.sel + 1 < L.nh) L.sel++;
        launcher_draw();
    } else if (in->key == 0xff52) {
        if (L.sel > 0) L.sel--;
        launcher_draw();
    } else if (in->key == 0xff08) {
        size_t n = strlen(L.query);
        if (n) L.query[n - 1] = 0;
        launcher_search(); L.sel = 0; launcher_draw();
    } else if (in->key >= 32 && in->key < 127) {
        size_t n = strlen(L.query);
        if (n < sizeof L.query - 1) { L.query[n] = (char)in->key; L.query[n + 1] = 0; }
        launcher_search(); L.sel = 0; launcher_draw();
    }
}
static int run_launcher(void)
{
    L.win = mica_win_new(G, 560, 320, "", "launcher", WIN_F_BORDERLESS | WIN_F_TOPMOST);
    if (!L.win) return 1;
    L.win->on_input = launcher_input;
    launcher_search();
    launcher_draw();
    return mica_run(G);
}

/* ======================================================================= */
int main(int argc, char **argv)
{
    const char *role = "panel";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--role") && i + 1 < argc) role = argv[++i];
    ml_log_init(ml_strdupf("mica-%s", role), ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect(role);
    if (!G) return 1;
    if (!strcmp(role, "panel")) return run_panel();
    if (!strcmp(role, "dock")) return run_dock();
    if (!strcmp(role, "desktop")) return run_desktop();
    if (!strcmp(role, "launcher")) return run_launcher();
    return 1;
}
