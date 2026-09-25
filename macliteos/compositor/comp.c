/* mica-comp — the MacLiteOS compositor + window manager + session supervisor.
 *
 * Design rules it lives by (spec §10, §13, §36, §40):
 *   - event driven: the loop blocks in epoll; frames are produced only when
 *     damage exists or an animation is live, then the vsync timer is disarmed
 *   - damage tracking: every composite pass renders only the dirty rects
 *   - isolation: clients are separate processes; a dead client only loses its
 *     own windows, and session components are restarted here
 *   - frame pacing: presents are quantised to the refresh interval and the
 *     stats are exported over MS_STATS for maclite-ui-benchmark
 */
#include "proto.h"
#include "ml/common.h"
#include "ml/log.h"
#include "ml/util.h"
#include "ml/event.h"
#include "ml/ipc.h"
#include "ml/surface.h"
#include "ml/region.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"
#include "ml/anim.h"
#include "ml/img.h"
#include "ml/cache.h"
#include "../hardware/hwprobe.h"
#include "../hardware/kms.h"
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <execinfo.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <strings.h>
#include <sys/stat.h>

#define DECO_H 30
#define MAX_CLIENTS 32
#define MAX_NOTIFS 3
#define N_WS 4

typedef struct client {
    int fd;
    pid_t pid;
    char name[48];
    bool alive;
    bool is_tool;
} client_t;

typedef struct win {
    client_t *cl;
    uint32_t id;
    int shm_fd;
    uint32_t *px;
    size_t shm_bytes;
    ml_surface *surf;
    char title[64], appid[32];
    uint32_t flags;
    ml_rect cur;            /* content rect on screen */
    ml_rect tgt;
    int ws;
    uint32_t state;
    bool mapped;
    bool closing;
    bool minimized;
    double opacity;
    double scale;
    ml_anim *geom;          /* rect spring for move/resize/maximize */
    ml_anim *fade;          /* opacity/scale for open/close/minimize */
    ml_region pending;      /* screen damage waiting for the next present */
    bool deco_dirty;
    struct win *z_next;     /* front -> back */
} win_t;

typedef struct notif {
    char title[64], body[160], icon[32];
    uint64_t born_ms, timeout_ms;
    ml_anim *in, *out;
    double xoff;
    bool dying;
} notif_t;

typedef struct child {
    pid_t pid;
    char name[64];
    char arg[32];          /* e.g. --role panel */
    uint64_t restarts;
    uint64_t last_restart;
    bool wanted;
} child_t;

static struct {
    ml_loop *loop;
    int listen_fd;
    client_t clients[MAX_CLIENTS];
    win_t *z_front, *z_back;
    int nwin;
    ml_surface *fb;             /* the screen */
    ml_surface *wallpaper;
    ml_anim_engine *anim;
    uint32_t mode;
    int screen_w, screen_h;
    int cur_ws;
    double ws_offset;           /* slide during workspace switch */
    ml_anim *ws_anim;
    /* pacing */
    ml_source *vsync;
    bool vsync_armed;
    uint64_t last_present_ns;
    uint64_t frames, dropped, presents;
    /* §24: the mode is picked from the GPU once, then *reduced* — never raised —
     * if the frames keep arriving late. The counter is the same one the report
     * uses; there is no timer and no sampling thread. */
    int slow_streak;
    bool mode_pinned;           /* an explicit --mode/MICA_MODE won over the picker */
    bool mode_frozen;           /* scripted/shot run: reduce nothing mid-run */
    uint32_t mode_steps;        /* how many times §24 has reduced the effects */
    uint64_t frame_us_sum, frame_us_worst;
    uint64_t pts[64];
    int pts_n, pts_i;
    uint64_t damage_last;
    ml_region damage;
    /* input */
    int mx, my;
    bool btn[3];
    win_t *drag_win, *resize_win, *hover_win;
    int drag_ox, drag_oy;
    win_t *hover_deco_btn;
    int deco_btn;
    /* notifications */
    notif_t notifs[MAX_NOTIFS];
    /* session */
    child_t children[8];
    bool session;
    bool shutting_down;
    pid_t launched[64];
    int n_launched;
    /* headless script */
    char **script;
    int script_n, script_i;
    char *shot_path;
    mica_gpu_info gpu;
    mica_cpu_info cpu;
    char sock[256];
    /* present backend (v0.2): kms / fbdev / headless. The renderer is unchanged
     * by this: it only decides where a finished damage region is handed off. */
    ml_display disp;
    const char *backend_req;
    bool backend_explicit;
} C;

/* ------------------------------------------------------ console ownership -- */
static void detach_framebuffer_console(void)
{
    const char *root = "/sys/class/vtconsole";
    DIR *d = opendir(root);
    if (!d) {
        ML_INFO("console handoff: %s unavailable; no fbcon detach required", root);
        return;
    }

    struct dirent *e;
    int found = 0, detached = 0;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "vtcon", 5) != 0) continue;

        char name_path[256], bind_path[256], name[128] = {0};
        snprintf(name_path, sizeof name_path, "%s/%s/name", root, e->d_name);
        snprintf(bind_path, sizeof bind_path, "%s/%s/bind", root, e->d_name);

        int fd = open(name_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = read(fd, name, sizeof(name) - 1);
        close(fd);
        if (n <= 0) continue;
        name[n] = 0;
        while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r'))
            name[--n] = 0;

        if (!strcasestr(name, "frame buffer") && !strcasestr(name, "framebuffer"))
            continue;

        found++;
        fd = open(bind_path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) {
            ML_WARN("console handoff: cannot open %s: %s", bind_path, strerror(errno));
            continue;
        }
        if (write(fd, "0\n", 2) == 2) {
            detached++;
            ML_INFO("console handoff: detached %s (%s)", e->d_name, name);
        } else {
            ML_WARN("console handoff: failed to detach %s (%s): %s",
                    e->d_name, name, strerror(errno));
        }
        close(fd);
    }
    closedir(d);

    if (!found)
        ML_INFO("console handoff: no framebuffer console backend registered");
    else
        ML_INFO("console handoff: detached %d/%d framebuffer console backend(s)", detached, found);
}

/* ---------------------------------------------------------------- utils -- */
static void notify(const msg_notify *n);        /* defined below; the frame loop uses these */
static void broadcast_event(uint32_t kind, uint32_t a, uint32_t b, const char *text);

static void damage_add(ml_rect r)
{
    /* clamp to screen: out-of-bounds clips (shadow margins, magnified dock
     * edges) would otherwise make the wallpaper blit sample outside the
     * surface and leave shifted stale pixels (measured: 50x60 px tear). */
    ml_rect scr = ml_rect_make(0, 0, C.screen_w, C.screen_h);
    r = ml_rect_intersect(r, scr);
    if (!ml_rect_empty(r)) ml_region_add(&C.damage, r);
}
static void win_frame(win_t *w, ml_rect *f)
{
    if (w->flags & WIN_F_BORDERLESS) { *f = w->cur; return; }
    *f = ml_rect_make(w->cur.x, w->cur.y - DECO_H, w->cur.w, w->cur.h + DECO_H);
}
static bool win_visible(win_t *w)
{
    return w->mapped && !w->closing && !w->minimized && w->opacity > 0.01
        && (w->ws == C.cur_ws || (w->flags & (WIN_F_TOPMOST | WIN_F_BOTTOM)));
}
static void do_launch(const char *cmdline);
static void respawn(void *ud);
static void respawn_deferred(void *ud);
static void frame_tick(void *ud);
static void on_term(void *ud);
static void request_frame(void)
{
    if (!C.vsync_armed) { ml_timer_arm(C.vsync, 16, true); C.vsync_armed = true; }
}

/* ------------------------------------------------------------ wallpaper -- */
static void build_wallpaper(void)
{
    C.wallpaper = ml_surface_new(C.screen_w, C.screen_h);
    ml_ctx c;
    ml_ctx_init(&c, C.wallpaper, ml_rect_make(0, 0, C.screen_w, C.screen_h));
    ml_color top = ml_rgb(22, 25, 42), mid = ml_rgb(66, 70, 122), low = ml_rgb(196, 116, 92);
    for (int y = 0; y < C.screen_h; y++) {
        double t = (double)y / (C.screen_h - 1);
        ml_color col = t < 0.62 ? ml_color_mix(top, mid, t / 0.62) : ml_color_mix(mid, low, (t - 0.62) / 0.38);
        ml_fill_rect(&c, ml_rect_make(0, y, C.screen_w, 1), col);
    }
    ml_pointd cp = { C.screen_w * 0.5, C.screen_h * 0.72 };
    ml_fill_radial_glow(&c, cp, C.screen_w * 0.75, C.screen_h * 0.42, ml_rgba(255, 172, 122, 64));
    ml_pointd cp2 = { C.screen_w * 0.26, C.screen_h * 0.14 };
    ml_fill_radial_glow(&c, cp2, C.screen_w * 0.7, C.screen_h * 0.5, ml_rgba(92, 106, 198, 30));
}

/* --------------------------------------------------------- decorations -- */
static void draw_deco(win_t *w)
{
    if (w->flags & WIN_F_BORDERLESS) return;
    if (!w->surf) return;
    ml_surface *d = w->surf;   /* decorations are composited inline from fb */
    (void)d;
    w->deco_dirty = false;
}

static void paint_deco(ml_ctx *c, win_t *w, ml_rect clip)
{
    if (w->flags & WIN_F_BORDERLESS) return;
    bool focused = (C.z_front == w);
    ml_rect tb = ml_rect_make(w->cur.x, w->cur.y - DECO_H, w->cur.w, DECO_H);
    ml_color bg = focused ? ml_rgba(46, 49, 62, (uint8_t)(250 * w->opacity))
                          : ml_rgba(38, 40, 50, (uint8_t)(235 * w->opacity));
    ml_fill_rounded(c, ml_rect_make(tb.x, tb.y, tb.w, DECO_H + 10), 10, bg);
    ml_fill_rect(c, ml_rect_make(tb.x, tb.y + DECO_H - 1, tb.w, 1), ml_rgba(0, 0, 0, (uint8_t)(80 * w->opacity)));
    /* traffic lights (original colours, slightly desaturated) */
    ml_color dots[3] = { ml_rgb(235, 105, 96), ml_rgb(240, 188, 84), ml_rgb(118, 199, 128) };
    for (int i = 0; i < 3; i++) {
        ml_rect r = ml_rect_make(tb.x + 14 + i * 20, tb.y + 9, 12, 12);
        ml_fill_rounded(c, r, 6, focused ? dots[i] : ml_rgb(90, 93, 104));
    }
    ml_font *f = ml_font_get("mica-sans");
    ml_draw_text_box(c, f, ml_rect_make(tb.x + 84, tb.y, tb.w - 168, DECO_H), w->title, 13,
                     ml_rgba(focused ? 235 : 190, focused ? 239 : 196, 250, (uint8_t)(235 * w->opacity)),
                     ML_ALIGN_CENTER);
    (void)clip;
}

/* ------------------------------------------------------------ compositing */
static void paint_window(ml_ctx *c, win_t *w, ml_rect clip, int dx)
{
    ml_rect frame;
    win_frame(w, &frame);
    frame.x += dx;
    ml_rect cr = ml_rect_intersect(frame, clip);
    if (ml_rect_empty(cr)) return;
    uint8_t op = (uint8_t)(255 * ML_CLAMP(w->opacity, 0, 1));
    if (op == 0) return;

    if (C.mode != MODE_PERFORMANCE && !(w->flags & WIN_F_BORDERLESS))
        ml_draw_shadow(c, ml_rect_make(frame.x, frame.y, frame.w, frame.h), 10, 16, ml_rgba(0, 0, 0, (uint8_t)(110 * op / 255)));

    paint_deco(c, w, cr);

    ml_rect content = ml_rect_make(w->cur.x + dx, w->cur.y, w->cur.w, w->cur.h);
    ml_rect area = ml_rect_intersect(content, clip);
    if (!ml_rect_empty(area) && w->surf) {
        if (w->scale > 0.02 && fabs(w->scale - 1.0) > 0.004) {
            ml_rect scaled = ml_rect_make(
                (int)(content.x + content.w / 2.0 - content.w * w->scale / 2.0),
                (int)(content.y + content.h / 2.0 - content.h * w->scale / 2.0),
                (int)(content.w * w->scale), (int)(content.h * w->scale));
            ml_blit_scaled(c, w->surf, scaled, ml_rect_make(0, 0, w->surf->w, w->surf->h), op);
        } else {
            ml_blit(c, w->surf, ml_rect_make(area.x - content.x, area.y - content.y, area.w, area.h),
                    area.x, area.y, op);
        }
    }
    /* 1px hairline keeps windows legible even in Performance mode */
    ml_stroke_rounded(c, ml_rect_make(frame.x, frame.y, frame.w, frame.h), 10, 1.0,
                      ml_rgba(255, 255, 255, (uint8_t)(26 * op / 255)));
}

static void paint_notifications(ml_ctx *c, ml_rect clip)
{
    int y = 34;
    for (int i = 0; i < MAX_NOTIFS; i++) {
        notif_t *n = &C.notifs[i];
        if (!n->title[0]) continue;
        double slide = n->in ? ml_anim_value(n->in) : (n->out ? 1 - ml_anim_value(n->out) : 1);
        int w = 340, h = 76;
        int x = C.screen_w - w - 12 + (int)((1 - slide) * (w + 20));
        ml_rect r = ml_rect_make(x, y, w, h);
        if (!ml_rect_intersects(r, clip)) { y += h + 10; continue; }
        if (C.mode != MODE_PERFORMANCE)
            ml_draw_shadow(c, r, 14, 14, ml_rgba(0, 0, 0, 90));
        ml_fill_rounded(c, r, 14, ml_rgba(44, 47, 60, 236));
        ml_stroke_rounded(c, r, 14, 1, ml_rgba(255, 255, 255, 34));
        ml_icon_draw(c, n->icon[0] ? n->icon : "bell", ml_rect_make(r.x + 12, r.y + 12, 30, 30), ml_rgba(255, 255, 255, 255));
        ml_font *fb = ml_font_get("mica-sans-bold");
        ml_font *f = ml_font_get("mica-sans");
        ml_draw_text_box(c, fb, ml_rect_make(r.x + 54, r.y + 10, r.w - 66, 20), n->title, 13,
                         ml_rgba(240, 244, 252, 245), ML_ALIGN_LEFT);
        ml_draw_text_box(c, f, ml_rect_make(r.x + 54, r.y + 32, r.w - 66, 36), n->body, 12,
                         ml_rgba(200, 208, 226, 235), ML_ALIGN_LEFT);
        y += h + 10;
    }
}

static void paint_cursor(ml_ctx *c, ml_rect clip)
{
    ml_path p;
    ml_path_init(&p);
    ml_path_parse(&p, "M0,0 L0,16 L4.4,12.4 L7.2,18.4 L9.8,17.2 L7,11.4 L12.4,11 Z");
    ml_draw_path_fill(c, &p, ml_rgb(250, 250, 252), C.mx, C.my, 1.0);
    ml_draw_path_stroke(c, &p, 1.0, ml_rgba(20, 22, 30, 200), C.mx, C.my, 1.0);
    ml_path_free(&p);
    (void)clip;
}

/* Paint a damage region into dst. Used by present() for the live fb and by
 * the scripted `shot` command for tear-free deterministic captures. */
static void present_region(ml_surface *dst, ml_region *dmg)
{
    /* snapshot z-order front->back, then paint reversed (painter's algorithm) */
    win_t *order[64];
    int n = 0;
    for (win_t *w = C.z_front; w && n < 64; w = w->z_next) order[n++] = w;
    for (size_t i = 0; i < ml_region_count(dmg); i++) {
        ml_rect clip = dmg->r[i];
        ml_ctx c;
        ml_ctx_init(&c, dst, clip);
        ml_blit(&c, C.wallpaper, ml_rect_make(clip.x - (int)C.ws_offset, clip.y, clip.w, clip.h),
                clip.x, clip.y, 255);
        for (int k = n - 1; k >= 0; k--) {
            win_t *w = order[k];
            if (!win_visible(w)) continue;
            paint_window(&c, w, clip, (w->flags & (WIN_F_TOPMOST | WIN_F_BOTTOM)) ? 0 : (int)C.ws_offset);
        }
        paint_notifications(&c, clip);
        paint_cursor(&c, clip);
    }
}

static void present(void)
{
    uint64_t t0 = ml_now_ns();
    ml_region dmg;
    ml_region_init(&dmg);
    bool have = ml_region_empty(&C.damage) ? false : (ml_region_copy(&dmg, &C.damage), ml_region_simplify(&dmg), true);
    ml_region_clear(&C.damage);
    bool anims = ml_anim_active_count(C.anim) > 0;
    if (!have && !anims) {
        if (C.vsync_armed) { ml_timer_disarm(C.vsync); C.vsync_armed = false; }
        ml_region_free(&dmg);
        return;
    }
    if (!have) {
        /* animations without explicit damage: they touched the whole window
         * bounds which the tick callbacks added; fallback to screen if empty */
        ml_region_add(&dmg, ml_rect_make(0, 0, C.screen_w, C.screen_h));
        have = true;
    }

    ml_raster_reset_stats();
    present_region(C.fb, &dmg);
    C.damage_last = (uint64_t)ml_region_area(&dmg);
    if (C.disp.kind != ML_DISP_HEADLESS)
        ml_display_commit(&C.disp, C.fb->px, dmg.r, ml_region_count(&dmg));
    if (getenv("ML_FRAMEDBG")) {
        ml_rect bb = ml_region_bounds(&dmg);
        fprintf(stderr, "[frame] dmg=%d,%d %dx%d area=%llu ws_off=%.2f cost=%llu us\n",
                bb.x, bb.y, bb.w, bb.h, (unsigned long long)C.damage_last, C.ws_offset,
                (unsigned long long)((ml_now_ns() - t0) / 1000));
    }
    ml_region_free(&dmg);

    uint64_t now = ml_now_ns();
    if (C.last_present_ns) {
        uint64_t dt = now - C.last_present_ns;
        bool slow = dt > HW_SLOW_FRAME_NS;
        if (slow) C.dropped++;        /* >25ms gap = a dropped frame */
        /* Sustained slowness (not one hiccup, which every machine has) steps the
         * mode down. Responsiveness outranks eye candy (§24); the decision can
         * only ever go one way, so the machine cannot oscillate between modes,
         * and a pinned mode is never touched. The policy itself lives in
         * hw_mode_step() so it can be tested without a running compositor. */
        bool reduced = false;
        uint32_t next = hw_mode_step(C.mode, &C.slow_streak, slow, C.mode_frozen,
                                     &reduced);
        if (reduced) {
            C.mode = next;
            C.mode_steps++;
            ML_INFO("auto-reducing effects to %s (%u step(s) this run)",
                    mica_mode_name(C.mode), C.mode_steps);
            msg_notify n = { 0 };
            snprintf(n.title, sizeof n.title, "Reduced effects");
            snprintf(n.body, sizeof n.body, "%s mode: the GPU was not keeping up",
                     mica_mode_name(C.mode));
            snprintf(n.icon, sizeof n.icon, "speed");
            notify(&n);
            /* the shell's control centre and Settings read the mode from this
             * broadcast, so an automatic step has to travel the same path an
             * explicit MC_SET_MODE does — otherwise the UI would keep showing
             * the old mode after the compositor changed it. */
            broadcast_event(EV_MODE, C.mode, 0, mica_mode_name(C.mode));
            ml_region_add(&C.damage, ml_rect_make(0, 0, C.screen_w, C.screen_h));
            request_frame();
        }
    }
    C.last_present_ns = now;
    C.frames++;
    C.presents++;
    C.pts[C.pts_i] = now;
    C.pts_i = (C.pts_i + 1) % 64;
    if (C.pts_n < 64) C.pts_n++;
    uint64_t us = (ml_now_ns() - t0) / 1000;
    C.frame_us_sum += us;
    if (us > C.frame_us_worst) C.frame_us_worst = us;
    if (!anims && ml_region_empty(&C.damage)) {
        if (C.vsync_armed) { ml_timer_disarm(C.vsync); C.vsync_armed = false; }
    } else {
        request_frame();
    }
    /* tell clients the frame landed (their frame pacing source) */
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (C.clients[i].alive && !C.clients[i].is_tool) {
            msg_frame f = { .id = 0, .seq = (uint32_t)C.frames, .ts_ns = now };
            mlipc_send(C.clients[i].fd, MS_FRAME, &f, sizeof f);
        }
}

/* ---------------------------------------------------------------- windows */
static void win_send_configure(win_t *w)
{
    msg_configure m = { .id = w->id, .x = w->tgt.x, .y = w->tgt.y, .w = w->tgt.w, .h = w->tgt.h, .state = w->state };
    if (w->cl) mlipc_send(w->cl->fd, MS_CONFIGURE, &m, sizeof m);
}
static void win_send_state(win_t *w)
{
    msg_win_state m = { .id = w->id, .state = w->state, .focused = C.z_front == w };
    if (w->cl) mlipc_send(w->cl->fd, MS_WIN_STATE, &m, sizeof m);
}
static void z_raise(win_t *w)
{
    if (C.z_front == w) return;
    /* unlink */
    win_t **pp = &C.z_front;
    while (*pp && *pp != w) pp = &(*pp)->z_next;
    if (*pp) *pp = w->z_next;
    else if (C.z_back == w) C.z_back = NULL;
    if (C.z_back == w) { /* recompute tail */
        C.z_back = C.z_front;
        while (C.z_back && C.z_back->z_next) C.z_back = C.z_back->z_next;
    }
    w->z_next = C.z_front;
    C.z_front = w;
    if (!C.z_back) C.z_back = w;
}
static void z_push_bottom(win_t *w)
{
    w->z_next = NULL;
    if (C.z_back) {
        win_t *t = C.z_back;
        /* find tail: z list is front->back via z_next */
        while (t->z_next) t = t->z_next;
        t->z_next = w;
    } else C.z_front = w;
    C.z_back = w;
}

static win_t *win_find(uint32_t id)
{
    for (win_t *w = C.z_front; w; w = w->z_next)
        if (w->id == id) return w;
    return NULL;
}

static void win_damage_all(win_t *w)
{
    ml_rect f;
    win_frame(w, &f);
    damage_add(ml_rect_make(f.x - 24, f.y - 24, f.w + 48, f.h + 48));
    request_frame();
}

static void open_anim_done(ml_anim *a, void *ud) { (void)a; (void)ud; }

static void win_open_anim(win_t *w)
{
    w->opacity = 0;
    w->scale = 0.94;
    w->fade = ml_anim_start(C.anim, 0, 1, 0.22, ML_EASE_OUT_CUBIC);
    ml_anim_set_ud(w->fade, w);
    ml_anim_on_done(w->fade, open_anim_done, NULL);
}

static void win_close(win_t *w)
{
    if (w->closing) return;
    w->closing = true;
    win_damage_all(w);
    w->fade = ml_anim_start(C.anim, 1, 0, 0.16, ML_EASE_IN_OUT_CUBIC);
    ml_anim_set_ud(w->fade, w);
}

static void win_release(win_t *w)
{
    win_damage_all(w);
    win_t **pp = &C.z_front;
    while (*pp && *pp != w) pp = &(*pp)->z_next;
    if (*pp) *pp = w->z_next;
    if (C.z_back == w) C.z_back = w->z_next;
    if (C.z_front == w) C.z_front = w->z_next;
    if (C.drag_win == w) C.drag_win = NULL;
    if (C.resize_win == w) C.resize_win = NULL;
    /* dangling hover pointers caused a use-after-free when a transient menu
     * closed under the cursor (segv in input_move); clear them here too */
    if (C.hover_win == w) { C.hover_win = NULL; C.hover_deco_btn = NULL; }
    if (w->surf) { munmap(w->px, w->shm_bytes); close(w->shm_fd); ml_surface_free(w->surf); }
    ml_region_free(&w->pending);
    ml_free(w);
    C.nwin--;
    msg_event e = { .kind = EV_WIN_CLOSE };
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (C.clients[i].alive && !C.clients[i].is_tool) mlipc_send(C.clients[i].fd, MS_EVENT, &e, sizeof e);
}

/* ------------------------------------------------------------- animation */
static void anim_tick(void *ud)
{
    (void)ud;
    double now = ml_now_s();
    ml_anim_tick(C.anim, now);
    for (win_t *w = C.z_front; w; w = w->z_next) {
        if (w->fade && ml_anim_ud(w->fade) == w) {
            double v = ml_anim_value(w->fade);
            if (w->closing) w->opacity = v;
            else { w->opacity = v; w->scale = 0.94 + 0.06 * v; }
            if (!ml_anim_active(w->fade)) {
                if (w->closing) { win_release(w); continue; }
                if (w->minimized) { w->opacity = 0; w->scale = 1; }
                else { w->opacity = 1; w->scale = 1; }
                w->fade = NULL;
            }
            win_damage_all(w);
        }
        if (w->geom) {
            ml_rect r = ml_anim_rect_value(w->geom);
            if (!ml_rect_eq(r, w->cur)) {
                win_damage_all(w);
                w->cur = r;
                w->tgt = r;
                win_damage_all(w);
            }
            if (!ml_anim_active(w->geom)) {
                w->cur = w->tgt;
                w->geom = NULL;
                win_damage_all(w);
                win_send_configure(w);
            }
        }
    }
    if (C.ws_anim) {
        C.ws_offset = ml_anim_value(C.ws_anim);
        damage_add(ml_rect_make(0, 0, C.screen_w, C.screen_h));
        if (!ml_anim_active(C.ws_anim)) { C.ws_anim = NULL; C.ws_offset = 0; }
    }
    for (int i = 0; i < MAX_NOTIFS; i++) {
        notif_t *n = &C.notifs[i];
        if (!n->title[0]) continue;
        if (n->in && ml_anim_active(n->in)) damage_add(ml_rect_make(C.screen_w - 400, 20, 420, 400));
        if (n->out && ml_anim_active(n->out)) damage_add(ml_rect_make(C.screen_w - 400, 20, 420, 400));
        if (n->out && !ml_anim_active(n->out)) { memset(n, 0, sizeof *n); damage_add(ml_rect_make(C.screen_w - 400, 20, 420, 400)); }
        else if (!n->dying && ml_wall_ms() - n->born_ms > n->timeout_ms) {
            n->dying = true;
            n->out = ml_anim_start(C.anim, 0, 1, 0.22, ML_EASE_IN_OUT_CUBIC);
        }
    }
}

static void frame_tick(void *ud)
{
    (void)ud;
    anim_tick(NULL);
    present();
}

/* DRM page-flip completion. The fd is only readable when an event is queued, so
 * this adds no wakeups to an idle loop (tests/test_idle.c still passes). */
static void flip_ready(void *ud, unsigned int events)
{
    (void)ud; (void)events;
    ml_display_wait(&C.disp, 1);
}

/* ------------------------------------------------------------------ input */
static win_t *win_at(int x, int y, int *local_x, int *local_y, bool *in_deco)
{
    for (win_t *w = C.z_front; w; w = w->z_next) {
        if (!win_visible(w)) continue;
        ml_rect f;
        win_frame(w, &f);
        if (ml_rect_contains(f, x, y)) {
            bool deco = !(w->flags & WIN_F_BORDERLESS) && y < w->cur.y;
            if (in_deco) *in_deco = deco;
            if (local_x) *local_x = x - w->cur.x;
            if (local_y) *local_y = y - w->cur.y;
            return w;
        }
    }
    return NULL;
}

static int deco_button_at(win_t *w, int x, int y)
{
    if (w->flags & WIN_F_BORDERLESS) return -1;
    for (int i = 0; i < 3; i++) {
        ml_rect r = ml_rect_make(w->cur.x + 14 + i * 20, w->cur.y - DECO_H + 9, 12, 12);
        if (ml_rect_contains(r, x, y)) return i;
    }
    return -1;
}

static void send_input(win_t *w, uint32_t kind, int x, int y, uint32_t button, uint32_t key, uint32_t mods)
{
    if (!w || !w->cl) return;
    msg_input m = { .id = w->id, .kind = kind, .x = x - w->cur.x, .y = y - w->cur.y,
                    .dx = x, .dy = y, .button = button, .key = key, .mods = mods, .time_ns = ml_now_ns() };
    mlipc_send(w->cl->fd, MS_INPUT, &m, sizeof m);
}

static void broadcast_event(uint32_t kind, uint32_t a, uint32_t b, const char *text)
{
    msg_event e = { .kind = kind, .a = a, .b = b };
    if (text) snprintf(e.text, sizeof e.text, "%s", text);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (C.clients[i].alive && !C.clients[i].is_tool) mlipc_send(C.clients[i].fd, MS_EVENT, &e, sizeof e);
}

static void focus_win(win_t *w)
{
    if (!w || (w->flags & WIN_F_NO_FOCUS)) return;
    if (C.z_front == w && (w->state & WS_FOCUSED)) return;
    win_t *prev = C.z_front;
    z_raise(w);
    w->state |= WS_FOCUSED;
    if (prev && prev != w) prev->state &= ~WS_FOCUSED;
    if (prev) win_damage_all(prev);
    win_damage_all(w);
    win_send_state(w);
    if (prev) win_send_state(prev);
    broadcast_event(EV_FOCUS, w->id, 0, w->appid);
}

static void toggle_maximize(win_t *w)
{
    if (!w) return;
    ml_rect from = w->cur, to;
    if (w->state & WS_MAXIMIZED) {
        to = ml_rect_make(120, 120, C.screen_w - 240, C.screen_h - 240);
        w->state &= ~WS_MAXIMIZED;
    } else {
        to = ml_rect_make(0, DECO_H + 2, C.screen_w, C.screen_h - DECO_H - 2 - 84);
        w->state |= WS_MAXIMIZED;
    }
    win_damage_all(w);
    w->tgt = to;
    w->geom = ml_anim_rect(C.anim, from, to, 0.24, ML_EASE_OUT_QUINT);
    request_frame();
}

static void minimize_win(win_t *w)
{
    if (!w || w->minimized) return;
    w->minimized = true;
    w->state |= WS_MINIMIZED;
    win_damage_all(w);
    w->fade = ml_anim_start(C.anim, 1, 0, 0.24, ML_EASE_IN_OUT_CUBIC);
    ml_anim_set_ud(w->fade, w);
    w->closing = false;
    win_send_state(w);
    /* focus next visible window */
    for (win_t *n = C.z_front; n; n = n->z_next)
        if (n != w && win_visible(n) && !(n->flags & WIN_F_NO_FOCUS)) { focus_win(n); break; }
}

static void unminimize_win(win_t *w)
{
    if (!w || !w->minimized) return;
    w->minimized = false;
    w->state &= ~WS_MINIMIZED;
    w->opacity = 0;
    w->fade = ml_anim_start(C.anim, 0, 1, 0.24, ML_EASE_OUT_CUBIC);
    ml_anim_set_ud(w->fade, w);
    win_damage_all(w);
    focus_win(w);
}

static void switch_workspace(int dir)
{
    int next = ML_CLAMP(C.cur_ws + dir, 0, N_WS - 1);
    if (next == C.cur_ws) return;
    C.cur_ws = next;
    damage_add(ml_rect_make(0, 0, C.screen_w, C.screen_h));
    broadcast_event(EV_WORKSPACE, (uint32_t)C.cur_ws, 0, NULL);
    request_frame();
}

static void notify(const msg_notify *n)
{
    int slot = -1;
    for (int i = 0; i < MAX_NOTIFS; i++)
        if (!C.notifs[i].title[0]) { slot = i; break; }
    if (slot < 0) slot = 0;
    notif_t *t = &C.notifs[slot];
    snprintf(t->title, sizeof t->title, "%s", n->title);
    snprintf(t->body, sizeof t->body, "%s", n->body);
    snprintf(t->icon, sizeof t->icon, "%s", n->icon);
    t->born_ms = ml_wall_ms();
    t->timeout_ms = n->timeout_ms ? n->timeout_ms : 5000;
    t->dying = false;
    t->out = NULL;
    t->in = ml_anim_start(C.anim, 0, 1, 0.26, ML_EASE_OUT_QUINT);
    damage_add(ml_rect_make(C.screen_w - 400, 20, 420, 400));
    request_frame();
    broadcast_event(EV_NOTIFY, 0, 0, n->title);
}

/* ------------------------------------------------------- client protocol */
static void client_gone(client_t *cl)
{
    cl->alive = false;
    for (win_t *w = C.z_front; w; w = w->z_next) {
        if (w->cl == cl && !w->closing) { w->cl = NULL; win_close(w); }
    }
    if (cl->fd >= 0) { close(cl->fd); cl->fd = -1; }
}

static void handle_client(void *ud, int fd, uint32_t type, const void *payload, uint32_t len, int fd_recv)
{
    client_t *cl = ud;
    (void)len;
    switch (type) {
    case MC_WIN_NEW: {
        const msg_win_new *m = payload;
        if (fd_recv < 0) break;
        size_t bytes = (size_t)m->w * m->h * 4;
        uint32_t *px = ml_shm_map(fd_recv, bytes);
        if (!px) { close(fd_recv); break; }
        win_t *w = ml_zalloc(sizeof *w);
        w->cl = cl;
        w->id = m->id;
        w->shm_fd = fd_recv;
        w->px = px;
        w->shm_bytes = bytes;
        w->surf = ml_surface_wrap(px, m->w, m->h, m->w);
        w->flags = m->flags;
        snprintf(w->title, sizeof w->title, "%s", m->title);
        snprintf(w->appid, sizeof w->appid, "%s", m->appid);
        w->ws = C.cur_ws;
        w->opacity = 1;
        w->scale = 1;
        ml_region_init(&w->pending);
        bool centered = true;
        static int cascade = 0;
        w->cur = w->tgt = ml_rect_make((C.screen_w - m->w) / 2 + (cascade % 6) * 28 - 84,
                                       (C.screen_h - m->h) / 2 + (cascade % 6) * 24 - 60,
                                       m->w, m->h);
        cascade++;
        (void)centered;
        if (w->flags & WIN_F_BOTTOM) { z_push_bottom(w); }
        else { z_raise(w); focus_win(w); }
        w->mapped = true;
        C.nwin++;
        win_open_anim(w);
        win_damage_all(w);
        msg_win_ok ok = { .id = w->id, .x = w->cur.x, .y = w->cur.y, .w = w->cur.w, .h = w->cur.h, .state = w->state };
        mlipc_send(cl->fd, MS_WIN_OK, &ok, sizeof ok);
        broadcast_event(EV_WIN_OPEN, w->id, 0, w->appid);
        break;
    }
    case MC_WIN_COMMIT: {
        const msg_win_commit *m = payload;
        win_t *w = win_find(m->id);
        if (!w) break;
        for (uint32_t i = 0; i < m->n; i++) {
            ml_rect r = ml_rect_make(m->rects[i * 4], m->rects[i * 4 + 1], m->rects[i * 4 + 2], m->rects[i * 4 + 3]);
            r.x += w->cur.x; r.y += w->cur.y;
            damage_add(r);
        }
        if (!m->n) win_damage_all(w);
        request_frame();
        break;
    }
    case MC_WIN_RESIZE: {
        const msg_win_resize *m = payload;
        win_t *w = win_find(m->id);
        if (!w || fd_recv < 0) { if (fd_recv >= 0) close(fd_recv); break; }
        size_t bytes = (size_t)m->w * m->h * 4;
        uint32_t *px = ml_shm_map(fd_recv, bytes);
        if (!px) { close(fd_recv); break; }
        munmap(w->px, w->shm_bytes);
        close(w->shm_fd);
        w->px = px; w->shm_fd = fd_recv; w->shm_bytes = bytes;
        ml_surface_free(w->surf);
        w->surf = ml_surface_wrap(px, m->w, m->h, m->w);
        win_damage_all(w);
        w->cur.w = w->tgt.w = m->w;
        w->cur.h = w->tgt.h = m->h;
        win_damage_all(w);
        break;
    }
    case MC_WIN_TITLE: {
        const msg_win_title *m = payload;
        win_t *w = win_find(m->id);
        if (!w) break;
        snprintf(w->title, sizeof w->title, "%s", m->title);
        win_damage_all(w);
        break;
    }
    case MC_WIN_ACTION: {
        const msg_win_action *m = payload;
        win_t *w = win_find(m->id);
        if (!w) break;
        switch (m->action) {
        case ACT_CLOSE: win_close(w); break;
        case ACT_MINIMIZE: minimize_win(w); break;
        case ACT_MAXIMIZE: toggle_maximize(w); break;
        case ACT_FULLSCREEN: toggle_maximize(w); break;
        case ACT_RAISE: focus_win(w); break;
        default: break;
        }
        break;
    }
    case MC_WIN_DRAG: {
        const msg_win_drag *m = payload;
        win_t *w = win_find(m->id);
        if (!w) break;
        win_damage_all(w);
        w->cur.x += m->dx; w->cur.y += m->dy;
        w->tgt = w->cur;
        win_damage_all(w);
        break;
    }
    case MC_WIN_PLACE: {
        const msg_win_place *m = payload;
        win_t *w = win_find(m->id);
        if (!w) break;
        win_damage_all(w);
        w->cur.x = w->tgt.x = m->x;
        w->cur.y = w->tgt.y = m->y;
        win_damage_all(w);
        break;
    }
    case MC_FOCUS: {
        const msg_id *m = payload;
        focus_win(win_find(m->id));
        break;
    }
    case MC_WORKSPACE: {
        const msg_ws *m = payload;
        switch_workspace(m->dir);
        break;
    }
    case MC_LAUNCH:
        do_launch(((const msg_launch *)payload)->cmd);
        break;
    case MC_NOTIFY:
        notify(payload);
        break;
    case MC_SET_MODE: {
        const msg_mode *m = payload;
        C.mode = m->mode;
        /* An explicit request is a promise, exactly like --mode/MICA_MODE: the
         * operator (or a script that steps through the modes) asked for this,
         * so the automatic reduction below must leave it alone. */
        C.mode_pinned = true;
        C.mode_frozen = true;
        damage_add(ml_rect_make(0, 0, C.screen_w, C.screen_h));
        request_frame();
        broadcast_event(EV_MODE, C.mode, 0, mica_mode_name(C.mode));
        break;
    }
    case MC_QUERY: {
        msg_stats st = { 0 };
        st.frames = C.frames;
        st.dropped = C.dropped;
        st.presents = C.presents;
        /* fps over a real 1 s sliding window of presents — never a constant */
        uint32_t fps = 0;
        if (C.pts_n > 1) {
            uint64_t qnow = ml_now_ns();
            uint64_t cut = qnow > 1000000000ull ? qnow - 1000000000ull : 0;
            uint32_t inwin = 0;
            for (int i = 0; i < C.pts_n; i++) if (C.pts[i] >= cut) inwin++;
            fps = inwin * 100;
        }
        st.fps_x100 = fps;
        st.frame_us = C.presents ? (uint32_t)(C.frame_us_sum / C.presents) : 0;
        st.worst_us = (uint32_t)C.frame_us_worst;
        st.damage_px_last = (uint32_t)C.damage_last;
        st.mode = C.mode;
        st.mode_steps = C.mode_steps;
        st.mode_pinned = C.mode_pinned ? 1u : 0u;
        st.nwindows = (uint32_t)C.nwin;
        for (int i = 0; i < MAX_CLIENTS; i++) if (C.clients[i].alive) st.nclients++;
        /* 0 headless, 1 drm/kms, 2 fbdev — the real backend, never a constant.
         * accel stays 0 until a GL compositing path lands (docs/gpu.md). */
        st.backend = (uint32_t)C.disp.kind;
        st.accel = 0;
        st.screen_w = C.screen_w; st.screen_h = C.screen_h;
        st.cur_ws = (uint32_t)C.cur_ws; st.n_ws = N_WS;
        snprintf(st.gpu_name, sizeof st.gpu_name, "%s", C.gpu.device[0] ? C.gpu.device : "software");
        mlipc_send(fd, MS_STATS, &st, sizeof st);
        break;
    }
    case MC_PING: {
        uint64_t t = ml_now_ns();
        mlipc_send(fd, MS_PONG, &t, sizeof t);
        break;
    }
    case MC_SHUTDOWN:
        C.shutting_down = true;
        ml_loop_quit(C.loop, 0);
        break;
    case 0:
        client_gone(cl);
        break;
    default:
        break;
    }
}

static void accept_client(void *ud, uint32_t events)
{
    (void)ud; (void)events;
    int fd = accept(C.listen_fd, NULL, NULL);
    if (fd < 0) return;
    uint8_t buf[512];
    uint32_t len = 0;
    uint32_t t = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    if (t != MC_HELLO || len < sizeof(msg_hello)) { close(fd); return; }
    const msg_hello *h = (const msg_hello *)buf;
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!C.clients[i].alive) { slot = i; break; }
    if (slot < 0) { close(fd); return; }
    client_t *cl = &C.clients[slot];
    memset(cl, 0, sizeof *cl);
    cl->fd = fd;
    cl->pid = (pid_t)h->pid;
    cl->alive = true;
    cl->is_tool = strncmp(h->name, "tool:", 5) == 0;
    snprintf(cl->name, sizeof cl->name, "%s", h->name);
    msg_welcome w = { .version = MICA_PROTO_VERSION, .screen_w = C.screen_w, .screen_h = C.screen_h,
                      .scale = 1, .mode = C.mode, .nworkspaces = N_WS, .cur_ws = (uint32_t)C.cur_ws };
    snprintf(w.name, sizeof w.name, "mica-comp");
    mlipc_send(fd, MS_WELCOME, &w, sizeof w);
    mlipc_watch(C.loop, fd, handle_client, cl);
    ML_INFO("client connected: %s (pid %d)", cl->name, cl->pid);
}

/* --------------------------------------------------------- input source -- */
static void input_move(int x, int y)
{
    int oldx = C.mx, oldy = C.my;
    C.mx = ML_CLAMP(x, 0, C.screen_w - 1);
    C.my = ML_CLAMP(y, 0, C.screen_h - 1);
    damage_add(ml_rect_make(oldx - 4, oldy - 4, 24, 28));
    damage_add(ml_rect_make(C.mx - 4, C.my - 4, 24, 28));
    if (C.drag_win) {
        win_damage_all(C.drag_win);
        C.drag_win->cur.x += C.mx - oldx;
        C.drag_win->cur.y += C.my - oldy;
        C.drag_win->tgt = C.drag_win->cur;
        win_damage_all(C.drag_win);
    } else if (C.resize_win) {
        win_t *w = C.resize_win;
        win_damage_all(w);
        w->cur.w = ML_CLAMP(w->cur.w + (C.mx - oldx), 240, C.screen_w);
        w->cur.h = ML_CLAMP(w->cur.h + (C.my - oldy), 160, C.screen_h);
        w->tgt = w->cur;
        win_send_configure(w);
        win_damage_all(w);
    } else {
        win_t *w = win_at(C.mx, C.my, NULL, NULL, NULL);
        if (w != C.hover_win) {
            if (C.hover_win) send_input(C.hover_win, IN_LEAVE, C.mx, C.my, 0, 0, 0);
            if (w) send_input(w, IN_ENTER, C.mx, C.my, 0, 0, 0);
            C.hover_win = w;
        }
        if (w) send_input(w, IN_MOVE, C.mx, C.my, 0, 0, 0);
    }
    request_frame();
}

static pid_t launcher_pid;
static void launch_launcher(void)
{
    char *exe = ml_find_in_path("mica-shell");
    if (!exe) {
        char *self = realpath("/proc/self/exe", NULL);
        char *dir = ml_path_dir(self ? self : "");
        exe = ml_path_join(dir, "mica-shell");
        ml_free(self); ml_free(dir);
        if (!ml_file_exists(exe)) { ml_free(exe); return; }
    }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl(exe, exe, "--role", "launcher", (char *)NULL);
        _exit(127);
    }
    if (C.n_launched < 64) C.launched[C.n_launched++] = pid;
    launcher_pid = pid;
    ml_free(exe);
}
static void input_key(uint32_t key, uint32_t mods)
{
    /* global hotkeys (spec §37, §33): Meta+Space launcher, Meta+arrows spaces */
    if (mods & ML_MOD_META) {
        if (key == ' ') {
            if (launcher_pid > 0 && kill(launcher_pid, 0) == 0) { kill(launcher_pid, SIGTERM); launcher_pid = 0; }
            else launch_launcher();
            return;
        }
        if (key == 0xff53) { switch_workspace(-1); return; }
        if (key == 0xff51) { switch_workspace(1); return; }
    }
    win_t *w = C.z_front;
    for (; w; w = w->z_next)
        if (win_visible(w) && !(w->flags & WIN_F_NO_FOCUS)) break;
    if (w) send_input(w, IN_KEY, C.mx, C.my, 0, key, mods);
}

static void input_button(int btn, bool down)
{
    if (btn < 0 || btn >= 3) return;
    C.btn[btn] = down;
    if (down) {
        bool in_deco = false;
        win_t *w = win_at(C.mx, C.my, NULL, NULL, &in_deco);
        if (!w) return;
        if (in_deco) {
            int b = deco_button_at(w, C.mx, C.my);
            if (b == 0) { win_close(w); return; }
            if (b == 1) { minimize_win(w); return; }
            if (b == 2) { toggle_maximize(w); return; }
            focus_win(w);
            C.drag_win = w;
            C.drag_ox = C.mx - w->cur.x;
            C.drag_oy = C.my - w->cur.y;
            return;
        }
        focus_win(w);
        send_input(w, IN_DOWN, C.mx, C.my, (uint32_t)btn + 1, 0, 0);
    } else {
        win_t *w = C.drag_win ? C.drag_win : C.resize_win ? C.resize_win
                                                      : win_at(C.mx, C.my, NULL, NULL, NULL);
        if (w) send_input(w, IN_UP, C.mx, C.my, (uint32_t)btn + 1, 0, 0);
        C.drag_win = NULL;
        C.resize_win = NULL;
    }
}

static void input_scroll(int dx, int dy)
{
    win_t *w = win_at(C.mx, C.my, NULL, NULL, NULL);
    if (!w || !w->cl) return;
    msg_input m = {
        .id = w->id, .kind = IN_SCROLL,
        .x = C.mx - w->cur.x, .y = C.my - w->cur.y,
        .dx = dx, .dy = dy, .time_ns = ml_now_ns()
    };
    mlipc_send(w->cl->fd, MS_INPUT, &m, sizeof m);
}

/* ------------------------------------------------------------------ evdev input
 *
 * The compositor previously had only scripted input. The kernel was correctly
 * enumerating the Apple keyboard and USB mouse, but nothing opened /dev/input,
 * so the GUI could render while all physical input was ignored. Keep input
 * inside the same epoll loop as rendering: no polling thread and no busy loop.
 * Devices are rescanned periodically so USB/Bluetooth receivers can be
 * unplugged/replugged while the installer is running.
 */
typedef struct {
    int fd;
    char path[64];
    char name[128];
    ml_source *src;
    bool pointer;
    bool keyboard;
    bool sync_dropped;
    uint32_t mods;
    bool caps;
} input_dev_t;

#define MAX_INPUT_DEVICES 32
static input_dev_t INPUT_DEVS[MAX_INPUT_DEVICES];

static bool bit_test(const unsigned long *bits, int bit)
{
    return (bits[bit / (int)(sizeof(unsigned long) * 8)] >>
            (bit % (int)(sizeof(unsigned long) * 8))) & 1UL;
}

static bool input_has_cap(int fd, int type, int code)
{
    unsigned long bits[(KEY_MAX + 1 + sizeof(unsigned long) * 8 - 1) /
                       (sizeof(unsigned long) * 8)];
    memset(bits, 0, sizeof bits);
    int max = type == EV_KEY ? KEY_MAX : type == EV_REL ? REL_MAX : EV_MAX;
    if (ioctl(fd, EVIOCGBIT(type, (max + 1) * (int)sizeof(unsigned long)), bits) < 0)
        return false;
    return bit_test(bits, code);
}

static void input_resync_state(input_dev_t *d)
{
    if (!d || d->fd < 0) return;

    d->mods = 0;
    d->caps = false;

    unsigned long key_bits[(KEY_MAX + 1 + sizeof(unsigned long) * 8 - 1) /
                           (sizeof(unsigned long) * 8)];
    memset(key_bits, 0, sizeof key_bits);
    if (ioctl(d->fd, EVIOCGKEY(sizeof key_bits), key_bits) >= 0) {
        d->mods = 0;
        if (bit_test(key_bits, KEY_LEFTSHIFT) || bit_test(key_bits, KEY_RIGHTSHIFT))
            d->mods |= ML_MOD_SHIFT;
        if (bit_test(key_bits, KEY_LEFTCTRL) || bit_test(key_bits, KEY_RIGHTCTRL))
            d->mods |= ML_MOD_CTRL;
        if (bit_test(key_bits, KEY_LEFTMETA) || bit_test(key_bits, KEY_RIGHTMETA))
            d->mods |= ML_MOD_META;
    }

    unsigned long led_bits[(LED_MAX + 1 + sizeof(unsigned long) * 8 - 1) /
                           (sizeof(unsigned long) * 8)];
    memset(led_bits, 0, sizeof led_bits);
    if (ioctl(d->fd, EVIOCGLED(sizeof led_bits), led_bits) >= 0)
        d->caps = bit_test(led_bits, LED_CAPSL);
}

static void input_close_device(int idx)
{
    if (idx < 0 || idx >= MAX_INPUT_DEVICES || INPUT_DEVS[idx].fd < 0) return;
    int fd = INPUT_DEVS[idx].fd;
    /* If a mouse/receiver disappears while a button is held, synthesize
     * releases so a window cannot remain permanently stuck in drag/resize
     * state after USB hot-unplug. */
    if (INPUT_DEVS[idx].pointer) {
        for (int b = 0; b < 3; b++)
            if (C.btn[b]) input_button(b, false);
        C.drag_win = NULL;
        C.resize_win = NULL;
    }
    if (INPUT_DEVS[idx].src) {
        ml_source_destroy(INPUT_DEVS[idx].src);
        INPUT_DEVS[idx].src = NULL;
    }
    close(fd);
    memset(&INPUT_DEVS[idx], 0, sizeof INPUT_DEVS[idx]);
    INPUT_DEVS[idx].fd = -1;
}

static uint32_t linux_key_to_mica(int code, uint32_t mods)
{
    bool shift = (mods & ML_MOD_SHIFT) != 0;
    if (code >= KEY_A && code <= KEY_Z) {
        char ch = (char)('a' + code - KEY_A);
        return (uint32_t)(shift ? ch - 'a' + 'A' : ch);
    }
    if (code >= KEY_1 && code <= KEY_0) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        int n = code - KEY_1;
        return (uint32_t)(shift ? shifted[n] : normal[n]);
    }
    switch (code) {
    case KEY_SPACE: return ' ';
    case KEY_ENTER: case KEY_KPENTER: return 0xff0d;
    case KEY_ESC: return 0xff1b;
    case KEY_BACKSPACE: return 0xff08;
    case KEY_TAB: return 0xff09;
    case KEY_DELETE: return 0xffff;
    case KEY_INSERT: return 0xff63;
    case KEY_HOME: return 0xff50;
    case KEY_END: return 0xff57;
    case KEY_PAGEUP: return 0xff55;
    case KEY_PAGEDOWN: return 0xff56;
    case KEY_UP: return 0xff52;
    case KEY_DOWN: return 0xff54;
    case KEY_LEFT: return 0xff51;
    case KEY_RIGHT: return 0xff53;
    case KEY_F1: return 0xffbe; case KEY_F2: return 0xffbf;
    case KEY_F3: return 0xffc0; case KEY_F4: return 0xffc1;
    case KEY_F5: return 0xffc2; case KEY_F6: return 0xffc3;
    case KEY_F7: return 0xffc4; case KEY_F8: return 0xffc5;
    case KEY_F9: return 0xffc6; case KEY_F10: return 0xffc7;
    case KEY_F11: return 0xffc8; case KEY_F12: return 0xffc9;
    case KEY_MINUS: return shift ? '_' : '-';
    case KEY_EQUAL: return shift ? '+' : '=';
    case KEY_LEFTBRACE: return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_SEMICOLON: return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE: return shift ? '~' : '`';
    case KEY_BACKSLASH: return shift ? '|' : '\\';
    case KEY_COMMA: return shift ? '<' : ',';
    case KEY_DOT: return shift ? '>' : '.';
    case KEY_SLASH: return shift ? '?' : '/';
    case KEY_KPASTERISK: return '*';
    case KEY_KPMINUS: return '-';
    case KEY_KPPLUS: return '+';
    case KEY_KPSLASH: return '/';
    case KEY_KP0: return '0'; case KEY_KP1: return '1'; case KEY_KP2: return '2';
    case KEY_KP3: return '3'; case KEY_KP4: return '4'; case KEY_KP5: return '5';
    case KEY_KP6: return '6'; case KEY_KP7: return '7'; case KEY_KP8: return '8';
    case KEY_KP9: return '9'; case KEY_KPDOT: return '.';
    default: return 0;
    }
}

static void input_key_event(input_dev_t *d, int code, int value)
{
    bool down = value != 0;
    switch (code) {
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
        if (down) d->mods |= ML_MOD_SHIFT; else d->mods &= ~ML_MOD_SHIFT;
        return;
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
        if (down) d->mods |= ML_MOD_CTRL; else d->mods &= ~ML_MOD_CTRL;
        return;
    case KEY_LEFTMETA: case KEY_RIGHTMETA:
        if (down) d->mods |= ML_MOD_META; else d->mods &= ~ML_MOD_META;
        return;
    case KEY_LEFTALT: case KEY_RIGHTALT:
        return;
    case KEY_CAPSLOCK:
        if (value == 1) d->caps = !d->caps;
        return;
    default:
        break;
    }
    if (!down) return;
    uint32_t mods = d->mods;
    uint32_t key = linux_key_to_mica(code, mods);
    if (d->caps && code >= KEY_A && code <= KEY_Z) {
        bool upper = !(mods & ML_MOD_SHIFT);
        key = (uint32_t)(upper ? ('A' + code - KEY_A) : ('a' + code - KEY_A));
    }
    if (key) input_key(key, mods);
}

static void input_event_fd(void *ud, uint32_t events)
{
    input_dev_t *d = ud;
    if (!d || d->fd < 0) return;
    int idx = (int)(d - INPUT_DEVS);
    if (events & (EPOLLERR | EPOLLHUP)) {
        input_close_device(idx);
        return;
    }

    struct input_event ev[32];
    int pending_dx = 0, pending_dy = 0, pending_wheel = 0, pending_hwheel = 0;
    for (;;) {
        ssize_t n = read(d->fd, ev, sizeof ev);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            input_close_device(idx);
            return;
        }
        if (n == 0) {
            input_close_device(idx);
            return;
        }
        size_t count = (size_t)n / sizeof(struct input_event);
        for (size_t i = 0; i < count; i++) {
            struct input_event *e = &ev[i];
            if (e->type == EV_SYN && e->code == SYN_DROPPED) {
                /* evdev requires ignoring the remainder of this packet until
                 * SYN_REPORT after an overrun, then resynchronizing state. */
                pending_dx = pending_dy = pending_wheel = pending_hwheel = 0;
                d->sync_dropped = true;
                continue;
            }
            if (d->sync_dropped) {
                if (e->type == EV_SYN && e->code == SYN_REPORT) {
                    d->sync_dropped = false;
                    input_resync_state(d);
                }
                continue;
            }
            if (e->type == EV_REL) {
                if (e->code == REL_X) pending_dx += e->value;
                else if (e->code == REL_Y) pending_dy += e->value;
                else if (e->code == REL_WHEEL) pending_wheel += e->value;
                else if (e->code == REL_HWHEEL) pending_hwheel += e->value;
                continue;
            }
            if (e->type == EV_KEY) {
                if (e->code == BTN_LEFT || e->code == BTN_RIGHT || e->code == BTN_MIDDLE) {
                    if (e->value == 0 || e->value == 1)
                        input_button(e->code - BTN_LEFT, e->value == 1);
                } else if (d->keyboard && (e->value == 0 || e->value == 1 || e->value == 2)) {
                    input_key_event(d, e->code, e->value);
                }
                continue;
            }
            if (e->type == EV_SYN && e->code == SYN_REPORT) {
                if (pending_dx || pending_dy)
                    input_move(C.mx + pending_dx, C.my + pending_dy);
                if (pending_wheel || pending_hwheel)
                    input_scroll(pending_hwheel, pending_wheel);
                pending_dx = pending_dy = pending_wheel = pending_hwheel = 0;
            }
        }
    }
}

static void input_scan(void *ud)
{
    (void)ud;
    DIR *dir = opendir("/dev/input");
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        if (access(path, R_OK) != 0) continue;

        bool known = false;
        for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
            if (INPUT_DEVS[i].fd >= 0 && !strcmp(INPUT_DEVS[i].path, path)) {
                known = true;
                break;
            }
        }
        if (known) continue;

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        bool pointer = input_has_cap(fd, EV_REL, REL_X) || input_has_cap(fd, EV_REL, REL_Y) ||
                       input_has_cap(fd, EV_KEY, BTN_LEFT) || input_has_cap(fd, EV_KEY, BTN_RIGHT);
        bool keyboard = input_has_cap(fd, EV_KEY, KEY_A) || input_has_cap(fd, EV_KEY, KEY_ENTER) ||
                        input_has_cap(fd, EV_KEY, KEY_SPACE);
        if (!pointer && !keyboard) {
            close(fd);
            continue;
        }

        int slot = -1;
        for (int i = 0; i < MAX_INPUT_DEVICES; i++)
            if (INPUT_DEVS[i].fd < 0) { slot = i; break; }
        if (slot < 0) {
            close(fd);
            continue;
        }

        input_dev_t *d = &INPUT_DEVS[slot];
        memset(d, 0, sizeof *d);
        d->fd = fd;
        d->pointer = pointer;
        d->keyboard = keyboard;
        snprintf(d->path, sizeof d->path, "%s", path);
        if (ioctl(fd, EVIOCGNAME(sizeof d->name), d->name) < 0)
            snprintf(d->name, sizeof d->name, "%s", path);
        input_resync_state(d);
        d->src = ml_loop_add_fd(C.loop, fd, EPOLLIN | EPOLLERR | EPOLLHUP, input_event_fd, d);
        ML_INFO("input device: %s (%s)%s%s", d->path, d->name,
                pointer ? " pointer" : "", keyboard ? " keyboard" : "");
    }
    closedir(dir);
}

static void input_init(void)
{
    for (int i = 0; i < MAX_INPUT_DEVICES; i++) INPUT_DEVS[i].fd = -1;
    input_scan(NULL);
    ml_loop_add_timer(C.loop, 500, true, input_scan, NULL);
}

/* children launched by name (dock/menu/launcher) must find our binaries even
 * when the session runs from a build dir that is not in PATH */
static char child_path[1024];
static void init_child_path(void)
{
    char *self = realpath("/proc/self/exe", NULL);
    char *dir = ml_path_dir(self ? self : "");
    snprintf(child_path, sizeof child_path, "PATH=%s:%s", dir,
             getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    ml_free(self); ml_free(dir);
}
static void do_launch(const char *cmdline)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (child_path[0]) putenv(child_path);
        execl("/bin/sh", "sh", "-c", cmdline, (char *)NULL);
        _exit(127);
    }
    if (C.n_launched < 64) C.launched[C.n_launched++] = pid;
    broadcast_event(EV_LAUNCH, (uint32_t)pid, 0, cmdline);
}

/* headless scripted input: deterministic, used by tests and benchmarks */
static void script_step(void);
static void script_run_line(const char *line)
{
    char cmd[32], a[256], b[256];
    a[0] = b[0] = 0;
    int n = sscanf(line, "%31s %255s %255s", cmd, a, b);
    if (n < 1) return;
    if (!strcmp(cmd, "wait")) {
        /* handled by caller via timer */
    } else if (!strcmp(cmd, "move")) {
        input_move(atoi(a), atoi(b));
    } else if (!strcmp(cmd, "click")) {
        if (n >= 3) input_move(atoi(a), atoi(b));
        input_button(0, true);
        input_button(0, false);
    } else if (!strcmp(cmd, "key")) {
        uint32_t k = 0, mods = 0;
        char buf[64];
        snprintf(buf, sizeof buf, "%s", a);
        char *toks[4];
        int nt = 0;
        for (char *t = strtok(buf, "+"); t && nt < 4; t = strtok(NULL, "+")) toks[nt++] = t;
        for (int i = 0; i + 1 < nt; i++) {
            if (!strcmp(toks[i], "meta")) mods |= ML_MOD_META;
            else if (!strcmp(toks[i], "ctrl")) mods |= ML_MOD_CTRL;
            else if (!strcmp(toks[i], "shift")) mods |= ML_MOD_SHIFT;
        }
        char *last = nt ? toks[nt - 1] : (char *)"";
        if (!strcmp(last, "return")) k = 0xff0d;
        else if (!strcmp(last, "escape")) k = 0xff1b;
        else if (!strcmp(last, "up")) k = 0xff52;
        else if (!strcmp(last, "down")) k = 0xff54;
        else if (!strcmp(last, "left")) k = 0xff51;
        else if (!strcmp(last, "right")) k = 0xff53;
        else if (!strcmp(last, "space")) k = ' ';
        else if (!strcmp(last, "backspace")) k = 0xff08;
        else if (last[0]) k = (uint32_t)(unsigned char)last[0];
        if (k) input_key(k, mods);
    } else if (!strcmp(cmd, "rclick")) {
        if (n >= 3) input_move(atoi(a), atoi(b));
        input_button(1, true);
        input_button(1, false);
    } else if (!strcmp(cmd, "down")) {
        input_button(0, true);
    } else if (!strcmp(cmd, "up")) {
        input_button(0, false);
    } else if (!strcmp(cmd, "shot")) {
        /* deterministic capture: re-render the full screen instead of dumping
         * the live fb, so screenshots never tear against an in-flight frame */
        ml_surface *snap = ml_surface_new(C.screen_w, C.screen_h);
        ml_region full;
        ml_region_init(&full);
        ml_region_add(&full, ml_rect_make(0, 0, C.screen_w, C.screen_h));
        present_region(snap, &full);
        ml_img_write(a, snap);
        ml_surface_free(snap);
        ml_region_free(&full);
        ML_INFO("wrote %s", a);
    } else if (!strcmp(cmd, "notify")) {
        msg_notify n = { .timeout_ms = 6000 };
        char *arg = ml_strdup(line + strlen("notify") + 1);
        char *bar = strchr(arg, '|');
        if (bar) { *bar = 0; snprintf(n.body, sizeof n.body, "%s", bar + 1); }
        snprintf(n.title, sizeof n.title, "%s", arg);
        snprintf(n.icon, sizeof n.icon, "shield");
        notify(&n);
        ml_free(arg);
    } else if (!strcmp(cmd, "ws")) {
        switch_workspace(atoi(a));
    } else if (!strcmp(cmd, "mode")) {
        C.mode = !strcmp(a, "beautiful") ? MODE_BEAUTIFUL : !strcmp(a, "balanced") ? MODE_BALANCED : MODE_PERFORMANCE;
        damage_add(ml_rect_make(0, 0, C.screen_w, C.screen_h));
        request_frame();
    } else if (!strcmp(cmd, "launch")) {
        do_launch(line + strlen("launch") + 1);
    } else if (!strcmp(cmd, "quit")) {
        C.shutting_down = true;
        ml_loop_quit(C.loop, 0);
    }
}

static void script_advance(void *ud)
{
    (void)ud;
    script_step();
}

static void script_step(void)
{
    while (C.script_i < C.script_n) {
        const char *line = C.script[C.script_i++];
        char cmd[32], a[256];
        a[0] = 0;
        if (sscanf(line, "%31s %255s", cmd, a) >= 1 && !strcmp(cmd, "wait")) {
            ml_loop_add_timer(C.loop, (uint64_t)ML_MAX(1, atoi(a)), false, script_advance, NULL);
            return;
        }
        script_run_line(line);
    }
}

/* ------------------------------------------------------------- session -- */
static void child_died(pid_t pid, int status)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(C.children); i++) {
        child_t *ch = &C.children[i];
        if (ch->pid != pid) continue;
        ML_WARN("session component %s exited (status %d)", ch->name, status);
        ch->pid = 0;
        if (!ch->wanted || C.shutting_down) return;
        uint64_t now = ml_wall_ms();
        if (now - ch->last_restart < 1500) {
            if (++ch->restarts > 5) { ML_ERR("%s keeps crashing; giving up", ch->name); ch->wanted = false; return; }
        } else ch->restarts = 0;
        ch->last_restart = now;
        char *name = ml_strdup(ch->name);
        ml_loop_add_timer(C.loop, 400, false, respawn_deferred, name);
        return;
    }
}

static void respawn_deferred(void *ud)
{
    char *name = ud;
    respawn(name);
    ml_free(name);
}

static void respawn(void *ud)
{
    char *name = ud;
    char *exe = ml_find_in_path(name);
    if (!exe) {
        char *self = realpath("/proc/self/exe", NULL);
        char *dir = ml_path_dir(self ? self : "");
        exe = ml_path_join(dir, name);
        ml_free(self); ml_free(dir);
        if (!ml_file_exists(exe)) { ML_ERR("cannot find %s to restart", name); ml_free(exe); return; }
    }
    const char *arg = NULL;
    for (size_t i = 0; i < ML_ARRAY_SIZE(C.children); i++)
        if (C.children[i].wanted && strcmp(C.children[i].name, name) == 0 && C.children[i].arg[0])
            arg = C.children[i].arg;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (arg) execl(exe, exe, "--role", arg, (char *)NULL);
        else execl(exe, exe, (char *)NULL);
        _exit(127);
    }
    for (size_t i = 0; i < ML_ARRAY_SIZE(C.children); i++)
        if (C.children[i].wanted && strcmp(C.children[i].name, name) == 0) C.children[i].pid = pid;
    ML_INFO("started %s (pid %d)", name, pid);
    ml_free(exe);
}

static void on_sigchld(void *ud)
{
    (void)ud;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) child_died(pid, status);
}

static void on_term(void *ud)
{
    (void)ud;
    C.shutting_down = true;
    ml_loop_quit(C.loop, 0);
}

/* ------------------------------------------------------------------ main -- */
static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [--backend auto|kms|fbdev|headless] [--headless|--drm] [-W w] [-H h]\n"
        "          [--mode beautiful|balanced|performance] [--session] [--script f] [--shot f]\n"
        "  --backend picks the present path; auto uses KMS when a /dev/dri card exists,\n"
        "  then fbdev, then headless. The chosen backend is logged and exported in MS_STATS.\n", p);
}

static void crash_sig(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    char msg[160];
    int n = snprintf(msg, sizeof msg, "\nFATAL: compositor caught signal %d at addr %p\n", sig, si ? si->si_addr : NULL);
    ssize_t r = write(2, msg, (size_t)n);
    (void)r;
    void *bt[32];
    int bn = backtrace(bt, 32);
    backtrace_symbols_fd(bt, bn, 2);
    _exit(128 + sig);
}
static void install_crash_handler(void)
{
    struct sigaction sa = { 0 };
    sa.sa_sigaction = crash_sig;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

int main(int argc, char **argv)
{
    install_crash_handler();
    init_child_path();
    ml_log_init("mica-comp", ML_LOG_INFO, getenv("MICA_LOG"));
    C.screen_w = 1440; C.screen_h = 900;
    C.mode = MODE_BEAUTIFUL;
    const char *script_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-W") && i + 1 < argc) C.screen_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-H") && i + 1 < argc) C.screen_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            C.mode = !strcmp(m, "balanced") ? MODE_BALANCED : !strcmp(m, "performance") ? MODE_PERFORMANCE : MODE_BEAUTIFUL;
            C.mode_pinned = true;
        } else if (!strcmp(argv[i], "--backend") && i + 1 < argc) { C.backend_req = argv[++i]; C.backend_explicit = true; }
        else if (!strcmp(argv[i], "--headless")) { C.backend_req = "headless"; C.backend_explicit = true; }
        else if (!strcmp(argv[i], "--drm")) { C.backend_req = "kms"; C.backend_explicit = true; }
        else if (!strcmp(argv[i], "--session")) C.session = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script_file = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) C.shot_path = ml_strdup(argv[++i]);
        else if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
    }

    mica_gpu_probe(&C.gpu);
    mica_cpu_probe(&C.cpu);
    /* A real display decides the resolution: never scale the iMac panel. */
    {
        int w = C.screen_w, h = C.screen_h;
        ml_display_open(&C.disp, C.backend_req, &w, &h);
        if (C.disp.kind != ML_DISP_HEADLESS) { C.screen_w = w; C.screen_h = h; }
        ML_INFO("present backend: %s (%s)", ml_disp_kind_name(C.disp.kind), C.disp.note);
        if (C.disp.kind == ML_DISP_KMS || C.disp.kind == ML_DISP_FBDEV)
            detach_framebuffer_console();
        /* An explicit --backend/--drm is a requirement. Degrading it silently
         * would let a user (or a boot script) believe the panel is driven by KMS
         * while every frame actually lands in memory: refuse and say why. */
        ml_present_kind want;
        if (C.backend_explicit && ml_present_parse(C.backend_req, &want) &&
            (int)want != (int)C.disp.kind) {
            ML_ERR("--backend %s requested but %s is running (%s)", C.backend_req,
                   ml_disp_kind_name(C.disp.kind), C.disp.note);
            ML_ERR("use --backend auto to accept the best available path");
            return 3;                        /* 3 = requested capability unavailable */
        }
    }
    /* The mode comes from what was really detected (§23/§24) unless the
     * operator pinned one with --mode/MICA_MODE — a pin is a promise and wins
     * over the picker. A scripted session still gets the machine's mode (the
     * pick is deterministic for a given machine) but is *frozen*: nothing may
     * change its rendering halfway through a run, so the automatic reduction
     * below is disabled for it and for one-shot screenshots. */
    if (getenv("MICA_MODE")) C.mode_pinned = true;
    if (!C.mode_pinned) {
        uint32_t pick = mica_pick_mode(&C.gpu, &C.cpu);
        ML_INFO("gpu: %s driver=%s kms=%d accel=%s -> suggested mode: %s",
                C.gpu.device[0] ? C.gpu.device : "(none)", C.gpu.driver, C.gpu.has_kms,
                mica_gpu_accel_name(&C.gpu), mica_mode_name(pick));
        C.mode = pick;
    }
    C.mode_frozen = C.mode_pinned || script_file || C.shot_path != NULL;

    C.loop = ml_loop_new();
    C.anim = ml_anim_engine_new(256);
    C.fb = ml_surface_new(C.screen_w, C.screen_h);
    build_wallpaper();
    ml_region_init(&C.damage);
    C.vsync = ml_loop_add_timer(C.loop, 0, false, frame_tick, NULL);

    char *rt = ml_runtime_dir();
    snprintf(C.sock, sizeof C.sock, "%s/mica-comp.sock", rt);
    ml_free(rt);
    C.listen_fd = mlipc_server_open(C.sock);
    if (C.listen_fd < 0) { ML_ERR("cannot bind %s", C.sock); return 1; }
    ml_loop_add_fd(C.loop, C.listen_fd, EPOLLIN, accept_client, NULL);
    if (C.disp.kind == ML_DISP_KMS)
        ml_loop_add_fd(C.loop, ml_display_fd(&C.disp), EPOLLIN, flip_ready, NULL);
    ml_loop_add_signal(C.loop, SIGCHLD, on_sigchld, NULL);
    ml_loop_add_signal(C.loop, SIGTERM, on_term, NULL);
    ml_loop_add_signal(C.loop, SIGINT, on_term, NULL);

    /* Consume real keyboard/mouse events from evdev in the compositor epoll
     * loop before the installer client is started. */
    input_init();

    damage_add(ml_rect_make(0, 0, C.screen_w, C.screen_h));
    request_frame();

    if (C.session) {
        static const struct { const char *exe, *role; } comps[] = {
            { "mica-shell", "desktop" }, { "mica-shell", "panel" }, { "mica-shell", "dock" },
        };
        for (size_t i = 0; i < ML_ARRAY_SIZE(comps); i++) {
            child_t *ch = &C.children[i];
            memset(ch, 0, sizeof *ch);
            snprintf(ch->name, sizeof ch->name, "%s", comps[i].exe);
            snprintf(ch->arg, sizeof ch->arg, "%s", comps[i].role);
            ch->wanted = true;
            respawn((void *)comps[i].exe);
        }
    }

    if (script_file) {
        char *txt = ml_read_file(script_file, NULL);
        if (txt) {
            int n = 0;
            C.script = ml_str_split(txt, '\n', &n);
            C.script_n = n;
            ml_free(txt);
            ml_loop_add_idle(C.loop, script_advance, NULL);
        } else ML_WARN("script %s unreadable", script_file);
    }

    ML_INFO("mica-comp ready: %dx%d mode=%s backend=%s socket=%s", C.screen_w, C.screen_h,
            mica_mode_name(C.mode), ml_disp_kind_name(C.disp.kind), C.sock);
    int rc = ml_loop_run(C.loop);

    C.shutting_down = true;
    for (size_t i = 0; i < ML_ARRAY_SIZE(C.children); i++)
        if (C.children[i].pid) kill(C.children[i].pid, SIGTERM);
    for (int i = 0; i < C.n_launched; i++)
        if (C.launched[i] > 0) kill(C.launched[i], SIGTERM);
    if (C.shot_path) ml_img_write(C.shot_path, C.fb);
    ml_display_close(&C.disp);
    unlink(C.sock);
    ML_INFO("compositor shutdown: %llu frames, %llu dropped", (unsigned long long)C.frames, (unsigned long long)C.dropped);
    return rc;
}
