/* mica-terminal — a small, fast terminal.
 *
 * Own VT100 subset emulator (CSI/SGR/scroll), a 512-row scrollback ring and a
 * pty child. Redraws only damaged rows. The cursor blink timer exists only
 * while the window is focused, so an idle terminal costs nothing (spec §13).
 */
#include "../compositor/client/shellkit.h"
#include "ml/util.h"
#include "ml/log.h"
#include <pty.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>

#define FONT_PX 13
#define SCROLLBACK 512

typedef struct { uint32_t cp; uint8_t fg, bg, attr; } cell_t;

typedef struct {
    mica_client *c;
    mica_win *win;
    int pty, child;
    cell_t *grid;
    int cols, rows, cell_w, line_h;
    int cx, cy;
    uint8_t fg, bg, attr;
    enum { ST_GROUND, ST_ESC, ST_CSI } state;
    int params[8], nparam;
    cell_t *sb[SCROLLBACK];
    int sb_head, sb_n;
    int scroll_off;
    bool dirty_rows[512];
    bool focused, blink_on;
    ml_source *pty_src, *blink;
    bool full_redraw;
} T;
static T t;

static const uint32_t pal[16] = {
    0xff22242e, 0xffe06c75, 0xff98c379, 0xffe5c07b, 0xff61afef, 0xffc678dd, 0xff56b6c2, 0xffabb2bf,
    0xff5c6370, 0xffe06c75, 0xff98c379, 0xffe5c07b, 0xff61afef, 0xffc678dd, 0xff56b6c2, 0xfff2f4fa,
};

static void mark(int row) { if (row >= 0 && row < t.rows) t.dirty_rows[row] = true; }
static void mark_all(void) { t.full_redraw = true; }

static void cell_clear(cell_t *c)
{
    c->cp = ' '; c->fg = t.fg; c->bg = 0; c->attr = 0;
}

static void scroll_up(void)
{
    if (t.sb[t.sb_head]) { ml_free(t.sb[t.sb_head]); }
    t.sb[t.sb_head] = ml_alloc(sizeof(cell_t) * (size_t)t.cols);
    memcpy(t.sb[t.sb_head], t.grid, sizeof(cell_t) * (size_t)t.cols);
    t.sb_head = (t.sb_head + 1) % SCROLLBACK;
    if (t.sb_n < SCROLLBACK) t.sb_n++;
    memmove(t.grid, t.grid + t.cols, sizeof(cell_t) * (size_t)t.cols * (size_t)(t.rows - 1));
    for (int x = 0; x < t.cols; x++) cell_clear(&t.grid[(size_t)(t.rows - 1) * t.cols + x]);
    mark_all();
}

static void cursor_nl(void)
{
    t.cx = 0;
    t.cy++;
    if (t.cy >= t.rows) { t.cy = t.rows - 1; scroll_up(); }
    mark(t.cy);
}

static void sgr(void)
{
    for (int i = 0; i < ML_MAX(1, t.nparam); i++) {
        int p = t.params[i];
        if (p == 0) { t.fg = 15; t.bg = 0; t.attr = 0; }
        else if (p == 1) t.attr |= 1;
        else if (p >= 30 && p <= 37) t.fg = (uint8_t)(p - 30);
        else if (p >= 90 && p <= 97) t.fg = (uint8_t)(p - 90 + 8);
        else if (p == 39) t.fg = 15;
        else if (p >= 40 && p <= 47) t.bg = (uint8_t)(p - 40);
        else if (p >= 100 && p <= 107) t.bg = (uint8_t)(p - 100 + 8);
        else if (p == 49) t.bg = 0;
    }
}

static void put_char(uint32_t cp)
{
    if (t.cx >= t.cols) { t.cx = 0; t.cy++; if (t.cy >= t.rows) { t.cy = t.rows - 1; scroll_up(); } }
    cell_t *c = &t.grid[(size_t)t.cy * t.cols + t.cx];
    c->cp = cp; c->fg = t.fg; c->bg = t.bg; c->attr = t.attr;
    mark(t.cy);
    t.cx++;
}

static void csi_dispatch(char final)
{
    int p0 = t.nparam > 0 ? t.params[0] : 0;
    switch (final) {
    case 'A': t.cy = ML_MAX(0, t.cy - ML_MAX(1, p0)); break;
    case 'B': t.cy = ML_MIN(t.rows - 1, t.cy + ML_MAX(1, p0)); break;
    case 'C': t.cx = ML_MIN(t.cols - 1, t.cx + ML_MAX(1, p0)); break;
    case 'D': t.cx = ML_MAX(0, t.cx - ML_MAX(1, p0)); break;
    case 'H': case 'f':
        t.cy = ML_CLAMP((t.nparam > 0 ? t.params[0] : 1) - 1, 0, t.rows - 1);
        t.cx = ML_CLAMP((t.nparam > 1 ? t.params[1] : 1) - 1, 0, t.cols - 1);
        break;
    case 'J':
        if (p0 == 2) { for (int i = 0; i < t.cols * t.rows; i++) cell_clear(&t.grid[i]); mark_all(); }
        else {
            for (int i = t.cy * t.cols + t.cx; i < t.cols * t.rows; i++) cell_clear(&t.grid[i]);
            for (int r = t.cy; r < t.rows; r++) mark(r);
        }
        break;
    case 'K':
        for (int x = (p0 == 1 ? 0 : t.cx); x < (p0 == 1 ? t.cx + 1 : t.cols); x++)
            cell_clear(&t.grid[(size_t)t.cy * t.cols + x]);
        mark(t.cy);
        break;
    case 'm': sgr(); break;
    case 'r': break;   /* scroll region: ignored, we scroll the whole screen */
    default: break;
    }
}

static void feed(const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)buf[i];
        switch (t.state) {
        case ST_GROUND:
            if (ch == 0x1b) { t.state = ST_ESC; break; }
            if (ch == '\n') { cursor_nl(); break; }
            if (ch == '\r') { t.cx = 0; mark(t.cy); break; }
            if (ch == '\b') { if (t.cx) t.cx--; break; }
            if (ch == '\t') { t.cx = ML_MIN(t.cols - 1, (t.cx + 8) & ~7); break; }
            if (ch == 7) break;
            if (ch < 32) break;
            if (ch >= 0x80) {   /* naive UTF-8 continuation: push into pending */
                put_char(ch); break;
            }
            put_char(ch);
            break;
        case ST_ESC:
            if (ch == '[') { t.state = ST_CSI; t.nparam = 0; memset(t.params, 0, sizeof t.params); }
            else if (ch == 'M') { if (t.cy) t.cy--; else { /* reverse index */ } mark(t.cy); t.state = ST_GROUND; }
            else t.state = ST_GROUND;
            break;
        case ST_CSI:
            if (ch >= '0' && ch <= '9') { t.params[ML_MIN(t.nparam, 7)] = t.params[ML_MIN(t.nparam, 7)] * 10 + (ch - '0'); continue; }
            if (ch == ';') { t.nparam++; continue; }
            if (ch >= 0x40 && ch <= 0x7e) { if (t.nparam || ch == 'm') t.nparam++; csi_dispatch((char)ch); t.state = ST_GROUND; }
            else t.state = ST_GROUND;
            break;
        }
    }
}

static void draw(void)
{
    ml_surface *s = t.win->surf;
    ml_ctx c;
    ml_rect clip = t.full_redraw ? ml_rect_make(0, 0, s->w, s->h) : ml_rect_make(0, 0, s->w, s->h);
    ml_ctx_init(&c, s, clip);
    if (t.full_redraw) ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(28, 30, 38));
    ml_font *mono = ml_font_get("mica-mono");
    for (int y = 0; y < t.rows; y++) {
        bool row_dirty = t.full_redraw || t.dirty_rows[y];
        if (!row_dirty) continue;
        t.dirty_rows[y] = false;
        int py = y * t.line_h;
        for (int x = 0; x < t.cols; x++) {
            cell_t *cell = &t.grid[(size_t)y * t.cols + x];
            if (cell->bg) {
                uint32_t bgc = pal[cell->bg & 15];
                ml_fill_rect(&c, ml_rect_make(x * t.cell_w, py, t.cell_w, t.line_h), ml_color_from_u32(bgc));
            }
            if (cell->cp && cell->cp != ' ') {
                uint32_t fgc = pal[(cell->attr & 1) ? (cell->fg | 8) : cell->fg];
                char buf[8];
                int n = 0;
                uint32_t cp = cell->cp;
                if (cp < 0x80) buf[n++] = (char)cp;
                else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 63)); }
                else { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 63)); buf[n++] = (char)(0x80 | (cp & 63)); }
                buf[n] = 0;
                ml_draw_text(&c, mono, x * t.cell_w + 1, py + t.line_h - 4, buf, FONT_PX, ml_color_from_u32(fgc));
            }
        }
    }
    if (t.focused && t.blink_on)
        ml_fill_rect(&c, ml_rect_make(t.cx * t.cell_w, t.cy * t.line_h, t.cell_w - 1, t.line_h - 2), ml_rgb(226, 232, 244));
    t.full_redraw = false;
    mica_win_commit(t.win);
}

static void blink_tick(void *ud)
{
    (void)ud;
    t.blink_on = !t.blink_on;
    draw();
}

static void on_state(mica_win *w)
{
    (void)w;
    if (t.win->focused && !t.blink) {
        t.blink_on = true;
        t.blink = mica_add_timer(t.c, 530, true, blink_tick, NULL);
    } else if (!t.win->focused && t.blink) {
        ml_source_destroy(t.blink);
        t.blink = NULL;
        t.blink_on = false;
        draw();
    }
}

static void on_configure(mica_win *w, const msg_configure *m)
{
    (void)m;
    int cols = ML_MAX(20, w->w / t.cell_w);
    int rows = ML_MAX(6, w->h / t.line_h);
    if (cols == t.cols && rows == t.rows) return;
    cell_t *ng = ml_zalloc(sizeof(cell_t) * (size_t)cols * rows);
    for (int y = 0; y < ML_MIN(rows, t.rows); y++)
        memcpy(ng + (size_t)y * cols, t.grid + (size_t)y * t.cols, sizeof(cell_t) * (size_t)ML_MIN(cols, t.cols));
    ml_free(t.grid);
    t.grid = ng;
    t.cols = cols; t.rows = rows;
    t.cx = ML_MIN(t.cx, cols - 1); t.cy = ML_MIN(t.cy, rows - 1);
    struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols, 0, 0 };
    ioctl(t.pty, TIOCSWINSZ, &ws);
    mark_all();
    draw();
}

static void pty_readable(void *ud, uint32_t ev)
{
    (void)ud; (void)ev;
    char buf[8192];
    ssize_t r = read(t.pty, buf, sizeof buf);
    if (r <= 0) { mica_quit(t.c, 0); return; }
    feed(buf, (size_t)r);
    draw();
}

static void key_to_pty(const msg_input *in)
{
    char seq[8];
    int n = 0;
    switch (in->key) {
    case 0xff0d: seq[n++] = '\r'; break;
    case 0xff08: seq[n++] = 0x7f; break;
    case 0xff1b: seq[n++] = 0x1b; break;
    case 0xff52: seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'A'; break;
    case 0xff54: seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'B'; break;
    case 0xff53: seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'D'; break;
    case 0xff51: seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'C'; break;
    case 0xff89: case '\t': seq[n++] = '\t'; break;
    default:
        if (in->key >= 32 && in->key < 127) seq[n++] = (char)in->key;
        else return;
    }
    ssize_t w = write(t.pty, seq, (size_t)n);
    (void)w;
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_KEY) key_to_pty(in);
    else if (in->kind == IN_SCROLL) {
        t.scroll_off = ML_CLAMP(t.scroll_off - in->dy / 24, 0, t.sb_n);
        mark_all();
        draw();
    }
}

int main(void)
{
    ml_log_init("mica-terminal", ML_LOG_WARN, getenv("MICA_LOG"));
    t.c = mica_connect("terminal");
    if (!t.c) return 1;
    t.win = mica_win_new(t.c, 760, 460, "Terminal", "terminal", 0);
    if (!t.win) return 1;
    t.win->on_input = input;
    t.win->on_configure = on_configure;
    t.win->on_state = on_state;
    ml_font *mono = ml_font_get("mica-mono");
    t.cell_w = ML_MAX(6, ml_text_width(mono, "M", FONT_PX) + 1);
    t.line_h = ml_font_line_height(mono, FONT_PX) + 4;
    t.cols = t.win->w / t.cell_w;
    t.rows = t.win->h / t.line_h;
    t.grid = ml_zalloc(sizeof(cell_t) * (size_t)t.cols * t.rows);
    t.fg = 15;
    struct winsize ws = { .ws_row = (unsigned short)t.rows, .ws_col = (unsigned short)t.cols, 0, 0 };
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) sh = "/bin/sh";
    pid_t pid = forkpty(&t.pty, NULL, NULL, &ws);
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execl(sh, sh, (char *)NULL);
        _exit(127);
    }
    t.child = pid;
    t.pty_src = ml_loop_add_fd(mica_loop(t.c), t.pty, EPOLLIN, pty_readable, NULL);
    mark_all();
    draw();
    int rc = mica_run(t.c);
    kill(t.child, SIGHUP);
    return rc;
}
