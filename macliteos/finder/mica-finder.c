/* mica-finder — the MacLiteOS file manager.
 *
 * Deliberately unglamorous internals (spec §16, §43, §44):
 *   - directories are read on demand; nothing is cached beyond the current view
 *   - refreshes come from inotify, never from a scan timer
 *   - search is on-demand and depth/record capped; there is no index anywhere
 *   - image thumbnails go through one bounded LRU (8 MB)
 */
#include "../compositor/client/shellkit.h"
#include "ml/util.h"
#include "ml/log.h"
#include "ml/img.h"
#include "ml/cache.h"
#include <sys/inotify.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define SIDEBAR_W 190
#define TOOLBAR_H 40
#define STATUS_H  24
#define GRID_CELL 108

typedef struct {
    char name[128];
    char path[1024];
    bool isdir;
    off_t size;
    time_t mtime;
} entry_t;

typedef struct {
    mica_client *c;
    mica_win *win;
    char cwd[1024];
    entry_t *items;
    int n, cap;
    int view;                 /* 0 grid, 1 list */
    int sel;
    char history[16][1024];
    int hist_n, hist_i;
    char search[96];
    bool searching;
    int inotify_fd;
    ml_source *in_src, *refresh;
    ml_cache *thumbs;
    shell_menu *menu;
    int scroll;
} F;
static F f;

static void thumb_free(void *v) { ml_surface_free(v); }

static const char *fav_names[] = { "Home", "Desktop", "Documents", "Downloads", "Music", "Pictures" };

static void fav_path(int i, char *out, size_t n)
{
    if (i == 0) snprintf(out, n, "%s", ml_home());
    else snprintf(out, n, "%s/%s", ml_home(), fav_names[i]);
}

static int entry_cmp(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->isdir != y->isdir) return x->isdir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void scan_dir(const char *dir)
{
    f.n = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (f.search[0] && !strcasestr(e->d_name, f.search)) continue;
        if (f.n == f.cap) { f.cap = f.cap ? f.cap * 2 : 128; f.items = ml_realloc(f.items, (size_t)f.cap * sizeof *f.items); }
        entry_t *it = &f.items[f.n++];
        snprintf(it->name, sizeof it->name, "%s", e->d_name);
        snprintf(it->path, sizeof it->path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(it->path, &st) == 0) { it->isdir = S_ISDIR(st.st_mode); it->size = st.st_size; it->mtime = st.st_mtime; }
        else { it->isdir = e->d_type == DT_DIR; it->size = 0; it->mtime = 0; }
    }
    closedir(d);
    qsort(f.items, (size_t)f.n, sizeof *f.items, entry_cmp);
}

static ml_surface *thumb_for(const entry_t *it)
{
    if (it->isdir) return NULL;
    if (!ml_str_endswith(it->name, ".png")) return NULL;
    ml_surface *t = ml_cache_get(f.thumbs, it->path);
    if (t) return t;
    ml_surface *img = ml_img_read_png(it->path);
    if (!img) return NULL;
    t = ml_surface_new(64, 64);
    ml_ctx c;
    ml_ctx_init(&c, t, ml_rect_make(0, 0, 64, 64));
    int side = ML_MIN(img->w, img->h);
    ml_blit_scaled(&c, img, ml_rect_make(0, 0, 64, 64),
                   ml_rect_make((img->w - side) / 2, (img->h - side) / 2, side, side), 255);
    ml_surface_free(img);
    ml_cache_put(f.thumbs, it->path, t, 64 * 64 * 4);
    return t;
}

static void draw(void)
{
    ml_surface *s = f.win->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(30, 32, 40));
    ml_font *fn = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");

    /* sidebar */
    ml_fill_rect(&c, ml_rect_make(0, 0, SIDEBAR_W, s->h), ml_rgb(38, 40, 50));
    ml_fill_rect(&c, ml_rect_make(SIDEBAR_W - 1, 0, 1, s->h), ml_rgba(0, 0, 0, 80));
    ml_draw_text(&c, fn, 16, 26, "FAVOURITES", 10, ml_rgb(150, 158, 176));
    for (size_t i = 0; i < ML_ARRAY_SIZE(fav_names); i++) {
        char p[1024];
        fav_path((int)i, p, sizeof p);
        int y = 36 + (int)i * 28;
        bool cur = strcmp(f.cwd, p) == 0;
        if (cur) ml_fill_rounded(&c, ml_rect_make(8, y, SIDEBAR_W - 16, 24), 6, ml_rgba(88, 128, 240, 110));
        ml_icon_draw(&c, i == 0 ? "user" : "folder", ml_rect_make(16, y + 4, 16, 16), ml_rgb(150, 190, 250));
        ml_draw_text(&c, fn, 40, y + 17, fav_names[i], 13, ml_rgb(226, 231, 242));
    }
    int dy = 36 + (int)ML_ARRAY_SIZE(fav_names) * 28 + 18;
    ml_draw_text(&c, fn, 16, dy, "DEVICES", 10, ml_rgb(150, 158, 176));
    ml_icon_draw(&c, "drive", ml_rect_make(16, dy + 10, 16, 16), ml_rgb(190, 198, 214));
    ml_draw_text(&c, fn, 40, dy + 23, "MacLiteOS Disk", 13, ml_rgb(226, 231, 242));

    /* toolbar */
    ml_fill_rect(&c, ml_rect_make(SIDEBAR_W, 0, s->w - SIDEBAR_W, TOOLBAR_H), ml_rgb(46, 49, 60));
    ml_fill_rect(&c, ml_rect_make(SIDEBAR_W, TOOLBAR_H - 1, s->w - SIDEBAR_W, 1), ml_rgba(0, 0, 0, 80));
    ml_icon_draw(&c, "arrow-left", ml_rect_make(SIDEBAR_W + 12, 11, 18, 18),
                 f.hist_i > 0 ? ml_rgb(226, 231, 242) : ml_rgb(90, 95, 108));
    ml_icon_draw(&c, "arrow-right", ml_rect_make(SIDEBAR_W + 38, 11, 18, 18),
                 f.hist_i + 1 < f.hist_n ? ml_rgb(226, 231, 242) : ml_rgb(90, 95, 108));
    char label[128];
    snprintf(label, sizeof label, "%s%s", f.searching ? "Search: " : "", f.searching ? f.search : ml_path_base(f.cwd));
    ml_draw_text(&c, fb, SIDEBAR_W + 72, 26, label, 14, ml_rgb(236, 240, 250));
    ml_icon_draw(&c, f.view ? "list-view" : "grid-view", ml_rect_make(s->w - 34, 11, 18, 18), ml_rgb(226, 231, 242));
    ml_icon_draw(&c, "search", ml_rect_make(s->w - 62, 11, 18, 18), ml_rgb(226, 231, 242));

    /* content */
    ml_rect area = ml_rect_make(SIDEBAR_W + 1, TOOLBAR_H, s->w - SIDEBAR_W - 1, s->h - TOOLBAR_H - STATUS_H);
    if (f.view == 0) {
        int cols = ML_MAX(1, (area.w - 16) / GRID_CELL);
        for (int i = 0; i < f.n; i++) {
            int cx = area.x + 12 + (i % cols) * GRID_CELL;
            int cy = area.y + 12 + (i / cols) * GRID_CELL - f.scroll;
            if (cy + GRID_CELL < area.y || cy > area.y + area.h) continue;
            if (i == f.sel) ml_fill_rounded(&c, ml_rect_make(cx - 4, cy - 4, GRID_CELL - 8, GRID_CELL - 4), 8, ml_rgba(88, 128, 240, 90));
            ml_surface *t = thumb_for(&f.items[i]);
            if (t) ml_blit(&c, t, ml_rect_make(0, 0, 64, 64), cx + 20, cy + 4, 255);
            else ml_icon_draw(&c, f.items[i].isdir ? "folder" : (ml_str_endswith(f.items[i].name, ".png") ? "photo" : "file"),
                              ml_rect_make(cx + 18, cy, 60, 60), ml_rgb(255, 255, 255));
            ml_draw_text_box(&c, fn, ml_rect_make(cx - 6, cy + 66, GRID_CELL - 4, 16), f.items[i].name, 12,
                             ml_rgb(226, 231, 242), ML_ALIGN_CENTER);
        }
    } else {
        ml_draw_text(&c, fn, area.x + 12, area.y + 18, "Name", 11, ml_rgb(150, 158, 176));
        ml_draw_text(&c, fn, area.x + area.w - 150, area.y + 18, "Size", 11, ml_rgb(150, 158, 176));
        ml_draw_text(&c, fn, area.x + area.w - 70, area.y + 18, "Kind", 11, ml_rgb(150, 158, 176));
        ml_fill_rect(&c, ml_rect_make(area.x + 8, area.y + 24, area.w - 16, 1), ml_rgba(255, 255, 255, 30));
        for (int i = 0; i < f.n; i++) {
            int y = area.y + 26 + i * 24 - f.scroll;
            if (y < area.y + 24 || y > area.y + area.h) continue;
            if (i == f.sel) ml_fill_rounded(&c, ml_rect_make(area.x + 6, y, area.w - 12, 22), 5, ml_rgba(88, 128, 240, 110));
            ml_icon_draw(&c, f.items[i].isdir ? "folder" : "file", ml_rect_make(area.x + 12, y + 3, 16, 16), ml_rgb(190, 210, 250));
            ml_draw_text_box(&c, fn, ml_rect_make(area.x + 34, y + 2, area.w - 260, 20), f.items[i].name, 12,
                             ml_rgb(226, 231, 242), ML_ALIGN_LEFT);
            char sz[32];
            ml_format_bytes((uint64_t)f.items[i].size, sz, sizeof sz);
            ml_draw_text(&c, fn, area.x + area.w - 150, y + 16, f.items[i].isdir ? "--" : sz, 12, ml_rgb(180, 188, 204));
            ml_draw_text(&c, fn, area.x + area.w - 70, y + 16, f.items[i].isdir ? "Folder" : "File", 12, ml_rgb(180, 188, 204));
        }
    }

    /* status bar */
    ml_fill_rect(&c, ml_rect_make(SIDEBAR_W, s->h - STATUS_H, s->w - SIDEBAR_W, STATUS_H), ml_rgb(38, 40, 50));
    char st[96];
    snprintf(st, sizeof st, "%d item%s", f.n, f.n == 1 ? "" : "s");
    ml_draw_text(&c, fn, SIDEBAR_W + 12, s->h - 8, st, 11, ml_rgb(160, 168, 186));
    mica_win_commit(f.win);
}

static void navigate(const char *path, bool push_history)
{
    if (push_history && f.hist_i + 1 < 16) {
        snprintf(f.history[f.hist_i + 1], sizeof f.history[0], "%s", path);
        f.hist_i++;
        f.hist_n = f.hist_i + 1;
    }
    snprintf(f.cwd, sizeof f.cwd, "%s", path);
    f.sel = -1;
    f.scroll = 0;
    scan_dir(f.cwd);
    if (f.inotify_fd >= 0) {
        inotify_rm_watch(f.inotify_fd, 1);
        inotify_add_watch(f.inotify_fd, f.cwd, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    }
    char t[128];
    snprintf(t, sizeof t, "%s - Files", ml_path_base(f.cwd));
    mica_win_set_title(f.win, t);
    draw();
}

static void open_item(int i)
{
    if (i < 0 || i >= f.n) return;
    if (f.items[i].isdir) { navigate(f.items[i].path, true); return; }
    char *cmd = ml_strdupf("mica-viewer \"%s\"", f.items[i].path);
    mica_launch(f.c, cmd);
    ml_free(cmd);
}

static void trash_item(int i)
{
    if (i < 0 || i >= f.n) return;
    char *trash = ml_strdupf("%s/.Trash", ml_home());
    ml_mkdirs(trash, 0700);
    char *dst = ml_path_join(trash, f.items[i].name);
    rename(f.items[i].path, dst);
    ml_free(dst); ml_free(trash);
    scan_dir(f.cwd);
    draw();
}

static void refresh_cb(void *ud) { (void)ud; scan_dir(f.cwd); draw(); }
static void in_cb(void *ud, uint32_t ev)
{
    (void)ud; (void)ev;
    char buf[4096];
    while (read(f.inotify_fd, buf, sizeof buf) > 0) { }
    ml_deferred_schedule(f.refresh);
}

static void menu_open_item(void *ud) { open_item((int)(intptr_t)ud); }
static void menu_trash_item(void *ud) { trash_item((int)(intptr_t)ud); }
static void menu_info_item(void *ud)
{
    int i = (int)(intptr_t)ud;
    if (i < 0 || i >= f.n) return;
    char sz[32];
    ml_format_bytes((uint64_t)f.items[i].size, sz, sizeof sz);
    char body[160];
    snprintf(body, sizeof body, "%s, %s", f.items[i].isdir ? "Folder" : "File", sz);
    mica_notify(f.c, f.items[i].name, body, f.items[i].isdir ? "folder" : "file", 4000);
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    ml_rect area = ml_rect_make(SIDEBAR_W + 1, TOOLBAR_H, f.win->w - SIDEBAR_W - 1, f.win->h - TOOLBAR_H - STATUS_H);
    if (in->kind == IN_KEY) {
        if (f.searching) {
            if (in->key == 0xff1b) { f.searching = false; f.search[0] = 0; scan_dir(f.cwd); draw(); }
            else if (in->key == 0xff0d) { f.searching = false; draw(); }
            else if (in->key == 0xff08) { size_t n = strlen(f.search); if (n) f.search[n - 1] = 0; scan_dir(f.cwd); draw(); }
            else if (in->key >= 32 && in->key < 127) {
                size_t n = strlen(f.search);
                if (n < sizeof f.search - 1) { f.search[n] = (char)in->key; f.search[n + 1] = 0; }
                scan_dir(f.cwd); draw();
            }
            return;
        }
        if (in->key == 0xff54) { if (f.sel + 1 < f.n) f.sel++; draw(); }
        else if (in->key == 0xff52) { if (f.sel > 0) f.sel--; draw(); }
        else if (in->key == 0xff0d) open_item(f.sel);
        else if (in->key == 0xff08 && f.hist_i > 0) { f.hist_i--; navigate(f.history[f.hist_i], false); }
        return;
    }
    if (in->kind == IN_SCROLL) { f.scroll = ML_MAX(0, f.scroll - in->dy / 4); draw(); return; }
    if (in->kind != IN_UP && in->kind != IN_DOWN) { /* allow double click detection below */ }
    if (in->kind == IN_UP && in->button == 1) {
        /* toolbar */
        if (in->y < TOOLBAR_H) {
            if (in->x >= SIDEBAR_W + 8 && in->x <= SIDEBAR_W + 34 && f.hist_i > 0) { f.hist_i--; navigate(f.history[f.hist_i], false); }
            else if (in->x >= SIDEBAR_W + 34 && in->x <= SIDEBAR_W + 60 && f.hist_i + 1 < f.hist_n) { f.hist_i++; navigate(f.history[f.hist_i], false); }
            else if (in->x >= f.win->w - 70 && in->x <= f.win->w - 46) { f.searching = true; f.search[0] = 0; draw(); }
            else if (in->x >= f.win->w - 40) { f.view = !f.view; draw(); }
            return;
        }
        if (in->x < SIDEBAR_W) {
            for (size_t i = 0; i < ML_ARRAY_SIZE(fav_names); i++) {
                int y = 36 + (int)i * 28;
                if (in->y >= y && in->y <= y + 24) { char p[1024]; fav_path((int)i, p, sizeof p); navigate(p, true); return; }
            }
            return;
        }
        /* selection + open on click in grid / list */
        if (ml_rect_contains(area, in->x, in->y)) {
            int idx = -1;
            if (f.view == 0) {
                int cols = ML_MAX(1, (area.w - 16) / GRID_CELL);
                int rx = (in->x - area.x - 12) / GRID_CELL;
                int ry = (in->y - area.y - 12 + f.scroll) / GRID_CELL;
                idx = ry * cols + rx;
            } else {
                idx = (in->y - area.y - 26 + f.scroll) / 24;
            }
            if (idx >= 0 && idx < f.n) {
                static uint64_t last_click; static int last_idx = -1;
                uint64_t now = ml_now_ns();
                bool dbl = (idx == last_idx && now - last_click < 400000000ull);
                last_click = now; last_idx = idx;
                f.sel = idx;
                if (dbl) open_item(idx);
                draw();
            }
        }
    } else if (in->kind == IN_UP && in->button == 3 && ml_rect_contains(area, in->x, in->y)) {
        int idx = f.sel;
        if (f.menu) shell_menu_close(f.menu);
        f.menu = shell_menu_open(f.c, in->x, in->y, NULL);
        shell_menu_add(f.menu, "Open", "play", menu_open_item, (void *)(intptr_t)idx);
        shell_menu_add(f.menu, "Get Info", "info", menu_info_item, (void *)(intptr_t)idx);
        shell_menu_sep(f.menu);
        shell_menu_add(f.menu, "Move to Trash", "trash", menu_trash_item, (void *)(intptr_t)idx);
        shell_menu_draw(f.menu);
        mica_win_place(f.menu->win, in->x, in->y);
    }
}

int main(int argc, char **argv)
{
    ml_log_init("mica-finder", ML_LOG_WARN, getenv("MICA_LOG"));
    f.c = mica_connect("finder");
    if (!f.c) return 1;
    f.win = mica_win_new(f.c, 920, 580, "Files", "finder", 0);
    if (!f.win) return 1;
    f.win->on_input = input;
    f.thumbs = ml_cache_new(8u * 1024 * 1024, 64, thumb_free);
    f.inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    f.refresh = ml_loop_add_deferred(mica_loop(f.c), refresh_cb, NULL);
    if (f.inotify_fd >= 0)
        f.in_src = ml_loop_add_fd(mica_loop(f.c), f.inotify_fd, EPOLLIN, (ml_fd_fn)in_cb, NULL);
    char start[1024];
    if (argc > 1) snprintf(start, sizeof start, "%s", argv[1]);
    else snprintf(start, sizeof start, "%s", ml_home());
    snprintf(f.history[0], sizeof f.history[0], "%s", start);
    f.hist_n = 1; f.hist_i = 0;
    navigate(start, false);
    return mica_run(f.c);
}
