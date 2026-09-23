#include "shellkit.h"
#include "ml/util.h"
#include "ml/log.h"
#include <dirent.h>

static char *g_keep[2];
static int g_keep_i;
static char *ml_path_join_tmp2(const char *a, const char *b)
{
    g_keep_i = (g_keep_i + 1) % 2;
    free(g_keep[g_keep_i]);
    g_keep[g_keep_i] = ml_path_join(a, b);
    return g_keep[g_keep_i];
}

const mica_app_def mica_apps[] = {
    { "finder",     "Files",            "app-files",        "mica-finder" },
    { "terminal",   "Terminal",         "app-terminal",     "mica-terminal" },
    { "browser",    "Web Browser",      "app-browser",      "maclite-browser" },
    { "vlc",        "VLC Media Player", "app-video",        "maclite-video" },
    { "youtube",    "YouTube",          "app-youtube",      "maclite-browser --app=https://www.youtube.com" },
    { "netflix",    "Netflix",          "app-netflix",      "maclite-browser --app=https://www.netflix.com" },
    { "primevideo", "Prime Video",      "app-primevideo",   "maclite-browser --app=https://www.primevideo.com" },
    { "disneyplus", "Disney+",          "app-disneyplus",   "maclite-browser --app=https://www.disneyplus.com" },
    { "imageview",  "Image Viewer",     "app-image",        "mica-viewer --image" },
    { "pdf",        "PDF Viewer",       "app-pdf",          "mica-viewer --pdf" },
    { "camera",     "Camera",           "app-camera",       "maclite-hardware" },
    { "settings",   "Settings",         "app-settings",     "mica-settings" },
    { "sysinfo",    "System Info",      "app-sysinfo",      "mica-sysinfo" },
    { "player",     "Media Player",     "app-video",        "maclite-video" },
    { "music",      "Music",            "app-music",        "mica-music" },
    { "textedit",   "Text Editor",      "app-textedit",     "mica-textedit" },
    { "diagnostics","Diagnostics",      "app-diagnostics",  "mica-diagnostics --gui" },
};
const size_t mica_apps_n = sizeof mica_apps / sizeof mica_apps[0];

const mica_app_def *mica_app_find(const char *key)
{
    if (!key) return NULL;
    for (size_t i = 0; i < mica_apps_n; i++)
        if (!strcmp(mica_apps[i].id, key) || !strcasecmp(mica_apps[i].name, key)) return &mica_apps[i];
    return NULL;
}

void shell_panel_bg(ml_ctx *c, ml_rect r, double radius, uint32_t mode, bool dark)
{
    if (mode == MODE_PERFORMANCE) {
        ml_fill_rounded(c, r, radius, dark ? ml_rgb(24, 26, 34) : ml_rgb(240, 242, 248));
        return;
    }
    uint8_t alpha = mode == MODE_BEAUTIFUL ? 178 : 225;
    ml_fill_rounded(c, r, radius, dark ? ml_rgba(20, 22, 30, alpha) : ml_rgba(246, 248, 252, alpha));
}
void shell_hairline(ml_ctx *c, ml_rect r, double radius)
{
    ml_stroke_rounded(c, r, radius, 1.0, ml_rgba(255, 255, 255, 40));
}

int shell_battery_pct(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return -1;
    struct dirent *e;
    int pct = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *p = ml_path_join("/sys/class/power_supply", e->d_name);
        char *type = ml_sysfs_str(ml_path_join_tmp2(p, "type"), "");
        if (strcmp(type, "Battery") == 0)
            pct = (int)ml_sysfs_long(ml_path_join_tmp2(p, "capacity"), -1);
        ml_free(type);
        ml_free(p);
        if (pct >= 0) break;
    }
    closedir(d);
    return pct;
}
bool shell_battery_charging(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return false;
    struct dirent *e;
    bool ch = false;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *p = ml_path_join("/sys/class/power_supply", e->d_name);
        char *st = ml_sysfs_str(ml_path_join_tmp2(p, "status"), "");
        if (strstr(st, "Charging")) ch = true;
        ml_free(st); ml_free(p);
    }
    closedir(d);
    return ch;
}
int shell_wifi_level(void)
{
    char *w = ml_read_file("/proc/net/wireless", NULL);
    if (!w) return -1;
    int level = -1;
    char *line = strtok(w, "\n");
    while (line) {
        char *q = strchr(line, ':');
        if (q) {
            /* last numeric column is link quality in dBm on most drivers */
            char *num = strrchr(line, ' ');
            if (num) level = atoi(num);
        }
        line = strtok(NULL, "\n");
    }
    ml_free(w);
    return level;
}
char *shell_wifi_ssid(void)
{
    /* iw/wpa_cli are not guaranteed present; read the AP from proc only when a
     * wireless iface exists. Returns NULL otherwise. Never polls. */
    DIR *d = opendir("/proc/net/wireless");
    (void)d;
    return NULL;
}

/* ------------------------------- menus ---------------------------------- */
#define ROW_H 26
#define MENU_W 210

shell_menu *shell_menu_open(mica_client *c, int x, int y, void *ud)
{
    shell_menu *m = ml_zalloc(sizeof *m);
    m->c = c;
    m->ud = ud;
    m->x = x; m->y = y;
    m->hover = -1;
    return m;
}
void shell_menu_add(shell_menu *m, const char *label, const char *icon, void (*action)(void *ud), void *ud)
{
    if (m->n >= 24) return;
    m->items[m->n].label = label;
    m->items[m->n].icon = icon;
    m->items[m->n].action = action;
    m->items[m->n].ud = ud;
    m->n++;
}
void shell_menu_sep(shell_menu *m)
{
    if (m->n >= 24) return;
    m->items[m->n].separator = true;
    m->n++;
}
void shell_menu_close(shell_menu *m)
{
    if (!m) return;
    if (m->win) mica_win_destroy(m->win);
    ml_free(m);
}
static void menu_win_input(mica_win *w, const msg_input *in)
{
    shell_menu *m = w->ud;
    if (m) shell_menu_input(m, in);
}
void shell_menu_own(shell_menu *m, shell_menu **slot) { if (m) m->owner = slot; }

void shell_menu_draw(shell_menu *m)
{
    if (!m->win) {
        int h = 8 + m->n * ROW_H + 4;
        m->win = mica_win_new(m->c, MENU_W, h, "", "menu", WIN_F_BORDERLESS | WIN_F_TOPMOST | WIN_F_TRANSIENT);
        if (!m->win) return;
        m->win->on_input = menu_win_input;
        m->win->ud = m;
    }
    ml_surface *s = m->win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 10, m->c->info.mode, true);
    shell_hairline(&c, ml_rect_make(0, 0, s->w, s->h), 10);
    ml_font *f = ml_font_get("mica-sans");
    for (int i = 0; i < m->n; i++) {
        int y = 6 + i * ROW_H;
        if (m->items[i].separator) {
            ml_fill_rect(&c, ml_rect_make(10, y + ROW_H / 2, s->w - 20, 1), ml_rgba(255, 255, 255, 40));
            continue;
        }
        if (i == m->hover)
            ml_fill_rounded(&c, ml_rect_make(5, y, s->w - 10, ROW_H - 2), 6, ml_rgba(88, 128, 240, 220));
        int x = 14;
        if (m->items[i].icon) {
            ml_icon_draw(&c, m->items[i].icon, ml_rect_make(x, y + 5, 16, 16), ml_rgba(235, 240, 250, 235));
            x += 24;
        }
        ml_draw_text(&c, f, x, y + 18, m->items[i].label, 13, ml_rgba(235, 240, 250, 240));
        if (m->items[i].checked)
            ml_draw_text(&c, f, s->w - 24, y + 18, "✓", 13, ml_rgba(160, 200, 255, 250));
    }
    mica_win_commit(m->win);
}
void shell_menu_input(shell_menu *m, const msg_input *in)
{
    if (in->kind == IN_MOVE) {
        int row = (in->y - 6) / ROW_H;
        int nh = (row >= 0 && row < m->n && !m->items[row].separator) ? row : -1;
        if (nh != m->hover) { m->hover = nh; shell_menu_draw(m); }
    } else if (in->kind == IN_UP && in->button == 1) {
        int row = (in->y - 6) / ROW_H;
        void (*act)(void *) = NULL;
        void *ud = NULL;
        if (row >= 0 && row < m->n && !m->items[row].separator) {
            act = m->items[row].action;
            ud = m->items[row].ud;
        }
        if (m->owner) *m->owner = NULL;
        shell_menu_close(m);
        if (act) act(ud);
    }
}
