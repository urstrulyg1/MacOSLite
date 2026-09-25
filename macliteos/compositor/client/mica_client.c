#include "mica_client.h"
#include "ml/ipc.h"
#include "ml/log.h"
#include "ml/util.h"
#include <unistd.h>
#include <sys/mman.h>

static char *sock_path(void)
{
    char *rt = ml_runtime_dir();
    char *p = ml_path_join(rt, "mica-comp.sock");
    ml_free(rt);
    return p;
}

static void handle_msg(void *ud, int fd, uint32_t type, const void *payload, uint32_t len, int fd_recv);

mica_client *mica_connect(const char *name)
{
    char *path = sock_path();
    int fd = mlipc_connect(path);
    ml_free(path);
    if (fd < 0) { ML_WARN("cannot reach compositor (is mica-comp running?)"); return NULL; }
    mica_client *c = ml_zalloc(sizeof *c);
    c->fd = fd;
    snprintf(c->name, sizeof c->name, "%s", name ? name : "client");
    msg_hello h = { .pid = (uint32_t)getpid(), .kind = 0 };
    snprintf(h.name, sizeof h.name, "%s", c->name);
    if (!mlipc_send(fd, MC_HELLO, &h, sizeof h)) { ml_free(c); return NULL; }
    uint32_t len = 0;
    uint8_t buf[4096];
    uint32_t t = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    if (t != MS_WELCOME || len < sizeof(msg_welcome)) { close(fd); ml_free(c); return NULL; }
    memcpy(&c->info, buf, sizeof c->info);
    c->loop = ml_loop_new();
    mlipc_watch(c->loop, fd, handle_msg, c);
    return c;
}

void mica_disconnect(mica_client *c)
{
    if (!c) return;
    ml_loop_destroy(c->loop);
    mlipc_close(c->fd);
    ml_free(c);
}
int mica_run(mica_client *c) { return ml_loop_run(c->loop); }
int mica_run_for(mica_client *c, uint64_t ms) { return ml_loop_run_for(c->loop, ms); }
void mica_quit(mica_client *c, int code) { ml_loop_quit(c->loop, code); }
ml_loop *mica_loop(mica_client *c) { return c->loop; }
ml_source *mica_add_timer(mica_client *c, uint64_t ms, bool repeat, ml_void_fn fn, void *ud)
{
    return ml_loop_add_timer(c->loop, ms, repeat, fn, ud);
}
ml_anim_engine *mica_anims(mica_client *c)
{
    static ml_anim_engine *e;
    (void)c;
    if (!e) e = ml_anim_engine_new(96);
    return e;
}

/* windows are kept in a registry owned by the client instance */
static mica_win **g_wins;
static size_t g_nwins;

static mica_win *win_by_id(uint32_t id)
{
    for (size_t i = 0; i < g_nwins; i++)
        if (g_wins[i] && g_wins[i]->id == id) return g_wins[i];
    return NULL;
}
static void win_register(mica_win *w)
{
    for (size_t i = 0; i < g_nwins; i++)
        if (!g_wins[i]) { g_wins[i] = w; return; }
    g_wins = ml_realloc(g_wins, (g_nwins + 1) * sizeof *g_wins);
    g_wins[g_nwins++] = w;
}
static void win_unregister(mica_win *w)
{
    for (size_t i = 0; i < g_nwins; i++)
        if (g_wins[i] == w) g_wins[i] = NULL;
}

mica_win *mica_win_new(mica_client *c, int w, int h, const char *title, const char *appid, uint32_t flags)
{
    size_t bytes = (size_t)w * h * 4;
    int shm = ml_shm_create(bytes);
    if (shm < 0) return NULL;
    uint32_t *px = ml_shm_map(shm, bytes);
    if (!px) { close(shm); return NULL; }
    mica_win *win = ml_zalloc(sizeof *win);
    win->c = c;
    win->w = w; win->h = h;
    win->flags = flags;
    win->surf = ml_surface_wrap(px, w, h, w);
    win->surf->backend_tex = (void *)(intptr_t)shm;   /* keep fd for resize/re-send */
    ml_surface_damage_all(win->surf);
    msg_win_new m = { 0 };
    static uint32_t next_id_hint = 1000;
    m.id = ++next_id_hint;
    m.w = w; m.h = h; m.flags = flags;
    snprintf(m.title, sizeof m.title, "%s", title ? title : "");
    snprintf(m.appid, sizeof m.appid, "%s", appid ? appid : c->name);
    win->id = m.id;
    if (!mlipc_send_fd(c->fd, MC_WIN_NEW, &m, sizeof m, shm)) {
        munmap(px, bytes); close(shm); ml_free(win);
        return NULL;
    }
    win_register(win);
    return win;
}

void mica_win_destroy(mica_win *win)
{
    if (!win) return;
    msg_id m = { win->id };
    mlipc_send(win->c->fd, MC_WIN_ACTION, &(msg_win_action){ .id = win->id, .action = ACT_CLOSE }, sizeof(msg_win_action));
    (void)m;
    munmap(win->surf->px, (size_t)win->w * win->h * 4);
    close((int)(intptr_t)win->surf->backend_tex);
    ml_surface_free(win->surf);
    win_unregister(win);
    ml_free(win);
}

void mica_win_commit_rects(mica_win *win, const ml_region *r)
{
    msg_win_commit m = { 0 };
    m.id = win->id;
    m.seq = win->surf->seq;
    size_t n = ML_MIN(r ? ml_region_count(r) : 0, 32u);
    m.n = (uint32_t)n;
    for (size_t i = 0; i < n; i++) {
        m.rects[i * 4 + 0] = r->r[i].x;
        m.rects[i * 4 + 1] = r->r[i].y;
        m.rects[i * 4 + 2] = r->r[i].w;
        m.rects[i * 4 + 3] = r->r[i].h;
    }
    mlipc_send(win->c->fd, MC_WIN_COMMIT, &m, sizeof m);
}
void mica_win_commit(mica_win *win)
{
    ml_region r;
    ml_region_init(&r);
    if (ml_surface_take_damage(win->surf, &r)) mica_win_commit_rects(win, &r);
    ml_region_free(&r);
}

void mica_win_set_title(mica_win *win, const char *title)
{
    msg_win_title m = { .id = win->id };
    snprintf(m.title, sizeof m.title, "%s", title ? title : "");
    mlipc_send(win->c->fd, MC_WIN_TITLE, &m, sizeof m);
}
void mica_win_action(mica_win *win, uint32_t action, int a, int b)
{
    msg_win_action m = { .id = win->id, .action = action, .a = a, .b = b };
    mlipc_send(win->c->fd, MC_WIN_ACTION, &m, sizeof m);
}
void mica_win_drag(mica_win *win, int dx, int dy)
{
    msg_win_drag m = { .id = win->id, .dx = dx, .dy = dy };
    mlipc_send(win->c->fd, MC_WIN_DRAG, &m, sizeof m);
}
void mica_win_place(mica_win *win, int x, int y)
{
    msg_win_place m = { .id = win->id, .x = x, .y = y };
    mlipc_send(win->c->fd, MC_WIN_PLACE, &m, sizeof m);
}
void mica_win_focus(mica_win *win)
{
    msg_id m = { win->id };
    mlipc_send(win->c->fd, MC_FOCUS, &m, sizeof m);
}
void mica_win_resize(mica_win *win, int w, int h)
{
    if (!win || w <= 0 || h <= 0) return;
    if (w == win->w && h == win->h) return;
    size_t bytes = (size_t)w * (size_t)h * 4;
    if (w > INT32_MAX || h > INT32_MAX || bytes / 4 != (size_t)w * (size_t)h) return;

    int shm = ml_shm_create(bytes);
    if (shm < 0) return;
    uint32_t *px = ml_shm_map(shm, bytes);
    if (!px) { close(shm); return; }

    msg_win_resize m = { .id = win->id, .w = w, .h = h };
    /* Do not destroy the old surface until the compositor has accepted the
     * new fd. If the IPC send fails, the client keeps a coherent old surface
     * instead of rendering into a buffer the compositor no longer knows. */
    if (!mlipc_send_fd(win->c->fd, MC_WIN_RESIZE, &m, sizeof m, shm)) {
        munmap(px, bytes);
        close(shm);
        return;
    }

    int old_shm = (int)(intptr_t)win->surf->backend_tex;
    size_t old_bytes = (size_t)win->w * (size_t)win->h * 4;
    munmap(win->surf->px, old_bytes);
    if (old_shm >= 0) close(old_shm);

    win->surf->px = px;
    win->surf->backend_tex = (void *)(intptr_t)shm;
    win->w = w; win->h = h;
    win->surf->w = w; win->surf->h = h; win->surf->stride = w;
    win->surf->bytes = bytes;
    ml_surface_damage_all(win->surf);
}

void mica_launch(mica_client *c, const char *cmdline)
{
    msg_launch m = { 0 };
    snprintf(m.cmd, sizeof m.cmd, "%s", cmdline ? cmdline : "");
    mlipc_send(c->fd, MC_LAUNCH, &m, sizeof m);
}
void mica_notify(mica_client *c, const char *title, const char *body, const char *icon, uint32_t timeout_ms)
{
    msg_notify m = { .timeout_ms = timeout_ms };
    snprintf(m.title, sizeof m.title, "%s", title ? title : "");
    snprintf(m.body, sizeof m.body, "%s", body ? body : "");
    snprintf(m.icon, sizeof m.icon, "%s", icon ? icon : "bell");
    mlipc_send(c->fd, MC_NOTIFY, &m, sizeof m);
}
void mica_workspace(mica_client *c, int dir)
{
    msg_ws m = { .dir = dir };
    mlipc_send(c->fd, MC_WORKSPACE, &m, sizeof m);
}
void mica_set_mode(mica_client *c, uint32_t mode)
{
    msg_mode m = { .mode = mode };
    mlipc_send(c->fd, MC_SET_MODE, &m, sizeof m);
}
void mica_ping(mica_client *c) { mlipc_send(c->fd, MC_PING, NULL, 0); }

static void handle_msg(void *ud, int fd, uint32_t type, const void *payload, uint32_t len, int fd_recv)
{
    mica_client *c = ud;
    (void)fd; (void)fd_recv;
    switch (type) {
    case MS_INPUT: {
        const msg_input *m = payload;
        mica_win *w = win_by_id(m->id);
        if (w && w->on_input) w->on_input(w, m);
        break;
    }
    case MS_CONFIGURE: {
        const msg_configure *m = payload;
        mica_win *w = win_by_id(m->id);
        if (!w) break;
        if ((m->w != w->w || m->h != w->h) && m->w > 0 && m->h > 0 && !(w->flags & WIN_F_BORDERLESS)) {
            /* the compositor decided our content size (maximize/fullscreen):
             * grow the buffer first, then acknowledge */
            mica_win_resize(w, m->w, m->h);
        }
        w->state = m->state;
        if (w->on_configure) w->on_configure(w, m);
        break;
    }
    case MS_WIN_OK: {
        const msg_win_ok *m = payload;
        mica_win *w = win_by_id(m->id);
        if (w) { w->mapped = true; w->state = m->state; }
        break;
    }
    case MS_WIN_STATE: {
        const msg_win_state *m = payload;
        mica_win *w = win_by_id(m->id);
        if (w) { w->state = m->state; w->focused = m->focused != 0; if (w->on_state) w->on_state(w); }
        break;
    }
    case MS_EVENT:
        if (c->on_event) c->on_event(c, payload);
        break;
    case MS_PONG:
        if (c->on_pong && len >= 8) {
            uint64_t t;
            memcpy(&t, payload, 8);
            c->on_pong(c, t);
        }
        break;
    case 0:
        /* compositor died: clients must not orphan the session */
        ML_WARN("%s: compositor connection lost", c->name);
        mica_quit(c, 2);
        break;
    default:
        break;
    }
    (void)len;
}
