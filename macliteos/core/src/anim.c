#include "ml/anim.h"
#include "ml/log.h"

struct ml_anim {
    bool in_use;
    bool active;
    bool cancelled;
    bool spring;
    ml_ease ease;
    double t0;          /* start time (s) */
    double dur;         /* seconds (duration anims) */
    double from, to;
    /* spring state */
    double x0, v0;      /* offset from target and velocity at (re)target time */
    double w, zeta, wd; /* natural freq, damping ratio, damped freq */
    double k, c, m;
    /* cached value */
    double value, velocity, progress;
    ml_anim_done_fn done;
    void *done_ud;
    ml_anim_frame_fn frame;
    void *frame_ud;
    void *ud;
    /* compound payloads */
    struct { ml_rect from, to; } rect;
    struct { ml_xform from, to; } xf;
    bool has_rect, has_xf;
    bool settled;
    double last_t;      /* for finite-difference velocity on duration anims */
    ml_rect rect_cur;
    ml_xform xf_cur;
};

struct ml_anim_engine {
    ml_anim *pool;
    size_t cap, used;
    uint64_t started, retargeted;
};

static const double SETTLE_POS = 0.02;    /* sub-pixel at 1x scale */
static const double SETTLE_VEL = 0.05;

double ml_ease_apply(ml_ease e, double t)
{
    t = ML_CLAMP(t, 0.0, 1.0);
    switch (e) {
    case ML_EASE_LINEAR:       return t;
    case ML_EASE_OUT_QUAD:     return 1 - (1 - t) * (1 - t);
    case ML_EASE_OUT_CUBIC:    { double u = 1 - t; return 1 - u * u * u; }
    case ML_EASE_IN_OUT_CUBIC: return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
    case ML_EASE_OUT_QUINT:    { double u = 1 - t; return 1 - u * u * u * u * u; }
    case ML_EASE_IN_OUT_QUINT: return t < 0.5 ? 16 * t * t * t * t * t : 1 - pow(-2 * t + 2, 5) / 2;
    case ML_EASE_OUT_EXPO:     return t >= 1.0 ? 1.0 : 1 - pow(2, -10 * t);
    case ML_EASE_IN_OUT_EXPO:
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        return t < 0.5 ? pow(2, 20 * t - 10) / 2 : (2 - pow(2, -20 * t + 10)) / 2;
    case ML_EASE_OUT_BACK:     { const double c1 = 1.70158, c3 = c1 + 1, u = t - 1;
                                 return 1 + c3 * u * u * u + c1 * u * u; }
    case ML_EASE_IN_OUT_BACK:  { const double c2 = 1.70158 * 1.525;
                                 return t < 0.5
                                     ? (pow(2 * t, 2) * ((c2 + 1) * 2 * t - c2)) / 2
                                     : (pow(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2; }
    case ML_EASE_ELASTIC_OUT:  { const double c4 = (2 * M_PI) / 3;
                                 if (t <= 0) return 0;
                                 if (t >= 1) return 1;
                                 return pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1; }
    default:                   return t;
    }
}

const char *ml_ease_name(ml_ease e)
{
    static const char *n[] = { "linear", "out-quad", "out-cubic", "in-out-cubic", "out-quint",
        "in-out-quint", "out-expo", "in-out-expo", "out-back", "in-out-back", "elastic-out", "spring" };
    return (unsigned)e < ML_ARRAY_SIZE(n) ? n[e] : "?";
}

ml_anim_engine *ml_anim_engine_new(size_t pool_size)
{
    ml_anim_engine *e = ml_zalloc(sizeof *e);
    e->cap = pool_size ? pool_size : 128;
    e->pool = ml_zalloc(e->cap * sizeof *e->pool);
    return e;
}
void ml_anim_engine_destroy(ml_anim_engine *e)
{
    if (!e) return;
    ml_free(e->pool);
    ml_free(e);
}

static ml_anim *alloc_anim(ml_anim_engine *e)
{
    for (size_t i = 0; i < e->cap; i++)
        if (!e->pool[i].in_use) {
            ml_anim *a = &e->pool[i];
            memset(a, 0, sizeof *a);
            a->in_use = true;
            a->active = true;
            e->used++;
            e->started++;
            return a;
        }
    ML_WARN("animation pool exhausted (%zu) — dropping animation", e->cap);
    return NULL;
}

static void solve_spring(ml_anim *a, double k, double c, double m, double x0, double v0)
{
    if (m <= 0) m = 1;
    a->k = k; a->c = c; a->m = m;
    a->w = sqrt(k / m);
    a->zeta = c / (2 * sqrt(k * m));
    a->wd = a->w * sqrt(ML_MAX(0.0, 1 - a->zeta * a->zeta));
    a->x0 = x0;
    a->v0 = v0;
    a->spring = true;
    a->ease = ML_EASE_SPRING;
}

ml_anim *ml_anim_start(ml_anim_engine *e, double from, double to, double dur_s, ml_ease ease)
{
    ml_anim *a = alloc_anim(e);
    if (!a) return NULL;
    a->from = from; a->to = to;
    a->dur = dur_s > 0 ? dur_s : 0.0001;
    a->ease = ease;
    a->t0 = ml_now_s();
    a->value = from;
    a->progress = 0;
    return a;
}

ml_anim *ml_anim_spring(ml_anim_engine *e, double from, double to,
                        double stiffness, double damping, double mass, double velocity)
{
    ml_anim *a = alloc_anim(e);
    if (!a) return NULL;
    a->from = from; a->to = to;
    a->t0 = ml_now_s();
    a->dur = 0;
    a->value = from;
    solve_spring(a, stiffness, damping, mass, from - to, velocity);
    return a;
}

/* Tuned once, reused everywhere: a ~350 ms critically-damped-ish response.
 * Feels like a modern desktop without overshoot jitter on a 2010 GPU. */
ml_anim *ml_anim_spring_default(ml_anim_engine *e, double from, double to, double velocity)
{
    return ml_anim_spring(e, from, to, 260.0, 26.0, 1.0, velocity);
}

double ml_anim_value(const ml_anim *a) { return a ? a->value : 0; }
double ml_anim_velocity(const ml_anim *a) { return a ? a->velocity : 0; }
double ml_anim_progress(const ml_anim *a) { return a ? a->progress : 0; }
bool ml_anim_active(const ml_anim *a) { return a && a->active; }
bool ml_anim_is_spring(const ml_anim *a) { return a && a->spring; }
void *ml_anim_ud(const ml_anim *a) { return a ? a->ud : NULL; }
void ml_anim_set_ud(ml_anim *a, void *ud) { if (a) a->ud = ud; }
void ml_anim_on_done(ml_anim *a, ml_anim_done_fn fn, void *ud) { if (!a) return; a->done = fn; a->done_ud = ud; }
void ml_anim_on_frame(ml_anim *a, ml_anim_frame_fn fn, void *ud) { if (!a) return; a->frame = fn; a->frame_ud = ud; }

static void release(ml_anim_engine *e, ml_anim *a)
{
    a->active = false;
    a->in_use = false;
    if (e->used) e->used--;
}

static void complete(ml_anim_engine *e, ml_anim *a, bool fire_callbacks)
{
    a->value = a->to;
    a->velocity = 0;
    a->progress = 1;
    if (a->has_rect) a->rect.from = a->rect.to;
    if (a->has_xf) a->xf.from = a->xf.to;
    ml_anim_done_fn done = a->done;
    void *dud = a->done_ud;
    a->done = NULL;
    release(e, a);
    if (fire_callbacks && done) done(NULL, dud);
}

void ml_anim_retarget(ml_anim *a, double to)
{
    if (!a || !a->in_use) return;
    if (a->to == to) return;
    double cur = a->value, vel = a->velocity;
    if (a->spring) {
        a->to = to;
        a->t0 = ml_now_s();
        solve_spring(a, a->k, a->c, a->m, cur - to, vel);
    } else {
        /* preserve feel: keep remaining fraction of the original duration */
        double remain = a->dur * (1.0 - a->progress);
        a->from = cur;
        a->to = to;
        a->dur = ML_CLAMP(remain, 0.06, a->dur);
        a->t0 = ml_now_s();
        a->progress = 0;
        a->velocity = (to - cur) / ML_MAX(1e-6, a->dur);
    }
    a->active = true;
    /* keep the same done callback: a retargeted close animation must still
     * destroy the window when it lands. */
}

void ml_anim_stop(ml_anim *a) { if (a && a->in_use) { a->active = false; a->velocity = 0; } }
void ml_anim_cancel(ml_anim *a)
{
    if (!a || !a->in_use) return;
    a->cancelled = true;
    a->done = NULL;
    a->active = false;
}

static ml_anim_engine *g_engine_for_finish; /* set during tick so finish_now works */
void ml_anim_finish_now(ml_anim *a)
{
    if (!a || !a->in_use) return;
    if (!g_engine_for_finish) { a->value = a->to; a->active = false; return; }
    complete(g_engine_for_finish, a, true);
}

static void update(ml_anim *a, double now)
{
    if (!a->active) return;
    double t = now - a->t0;
    if (a->spring) {
        double dt = ML_MAX(0.0, t);
        double off, vel;
        if (a->zeta < 1.0 - 1e-6) {
            double decay = exp(-a->zeta * a->w * dt);
            double cs = cos(a->wd * dt), sn = sin(a->wd * dt);
            double B = (a->v0 + a->zeta * a->w * a->x0) / (a->wd > 1e-9 ? a->wd : 1e-9);
            off = decay * (a->x0 * cs + B * sn);
            vel = -decay * ((a->zeta * a->w) * (a->x0 * cs + B * sn)) + decay * (-a->x0 * a->wd * sn + B * a->wd * cs);
        } else if (fabs(a->zeta - 1.0) <= 1e-6) {
            double decay = exp(-a->w * dt);
            double B = a->v0 + a->w * a->x0;
            off = decay * (a->x0 + B * dt);
            vel = decay * (B - a->w * (a->x0 + B * dt));
        } else {
            double s1 = -a->w * (a->zeta - sqrt(a->zeta * a->zeta - 1));
            double s2 = -a->w * (a->zeta + sqrt(a->zeta * a->zeta - 1));
            double B = (a->v0 - s1 * a->x0) / (s2 - s1);
            double A = a->x0 - B;
            off = A * exp(s1 * dt) + B * exp(s2 * dt);
            vel = A * s1 * exp(s1 * dt) + B * s2 * exp(s2 * dt);
        }
        a->value = a->to + off;
        a->velocity = vel;
        a->last_t = now;
        a->progress = 1.0 - ML_CLAMP(fabs(off) / (fabs(a->from - a->to) > 1e-9 ? fabs(a->from - a->to) : 1.0), 0.0, 1.0);
        if (fabs(off) < SETTLE_POS && fabs(vel) < SETTLE_VEL) a->active = false, a->settled = true;
        if (dt > 10.0) a->active = false;      /* safety: never animate forever */
    } else {
        double p = ML_CLAMP(t / a->dur, 0.0, 1.0);
        double prev = a->value;
        double dt = now - (a->last_t ? a->last_t : a->t0);
        a->last_t = now;
        a->progress = p;
        a->value = a->from + (a->to - a->from) * ml_ease_apply(a->ease, p);
        a->velocity = dt > 1e-6 ? (a->value - prev) / dt : 0;
        if (p >= 1.0) a->active = false, a->settled = true;
    }
    if (a->has_rect) {
        double p = a->progress;
        double x = a->rect.from.x + (a->rect.to.x - a->rect.from.x) * p;
        double y = a->rect.from.y + (a->rect.to.y - a->rect.from.y) * p;
        double w = a->rect.from.w + (a->rect.to.w - a->rect.from.w) * p;
        double h = a->rect.from.h + (a->rect.to.h - a->rect.from.h) * p;
        a->rect_cur = ml_rect_make((int)lround(x), (int)lround(y), (int)lround(w), (int)lround(h));
    }
    if (a->has_xf) {
        double p = a->progress;
        ml_xform *f = &a->xf.from, *t2 = &a->xf.to;
        a->xf_cur.opacity = f->opacity + (t2->opacity - f->opacity) * p;
        a->xf_cur.scale = f->scale + (t2->scale - f->scale) * p;
        a->xf_cur.offset.x = f->offset.x + (t2->offset.x - f->offset.x) * p;
        a->xf_cur.offset.y = f->offset.y + (t2->offset.y - f->offset.y) * p;
    }
}

size_t ml_anim_tick(ml_anim_engine *e, double now_s)
{
    g_engine_for_finish = e;
    size_t active = 0;
    for (size_t i = 0; i < e->cap; i++) {
        ml_anim *a = &e->pool[i];
        if (!a->in_use) continue;
        if (a->active) {
            update(a, now_s);
            if (a->frame && a->in_use) a->frame(a, a->frame_ud);
        }
        if (!a->active && a->in_use) {
            if (a->cancelled) { release(e, a); continue; }
            complete(e, a, true);
            continue;
        }
        if (a->in_use && a->active) active++;
    }
    g_engine_for_finish = NULL;
    return active;
}

size_t ml_anim_active_count(const ml_anim_engine *e)
{
    size_t n = 0;
    for (size_t i = 0; i < e->cap; i++)
        if (e->pool[i].in_use && e->pool[i].active) n++;
    return n;
}
size_t ml_anim_pool_used(const ml_anim_engine *e) { return e->used; }
uint64_t ml_anim_started_total(const ml_anim_engine *e) { return e->started; }
uint64_t ml_anim_retarget_total(const ml_anim_engine *e) { return e->retargeted; }

ml_anim *ml_anim_rect(ml_anim_engine *e, ml_rect from, ml_rect to, double dur_s, ml_ease ease)
{
    ml_anim *a = ml_anim_start(e, 0, 1, dur_s, ease);
    if (!a) return NULL;
    a->has_rect = true;
    a->rect.from = from;
    a->rect.to = to;
    a->rect_cur = from;
    a->value = 0;
    return a;
}
ml_anim *ml_anim_rect_spring(ml_anim_engine *e, ml_rect from, ml_rect to, double velocity)
{
    ml_anim *a = ml_anim_spring_default(e, 0, 1, velocity);
    if (!a) return NULL;
    a->has_rect = true;
    a->rect.from = from;
    a->rect.to = to;
    a->rect_cur = from;
    return a;
}
ml_rect ml_anim_rect_value(ml_anim *a) { return a && a->has_rect ? a->rect_cur : ml_rect_make(0, 0, 0, 0); }
void ml_anim_rect_retarget(ml_anim *a, ml_rect to)
{
    if (!a || !a->has_rect || !a->in_use) return;
    ml_rect cur = a->rect_cur;
    a->rect.from = cur;
    a->rect.to = to;
    if (a->spring) {
        a->t0 = ml_now_s();
        solve_spring(a, a->k, a->c, a->m, a->value - 1.0, a->velocity);
        a->to = 1.0;
    } else {
        double remain = a->dur * (1.0 - a->progress);
        a->from = a->progress;
        a->to = 1.0;
        a->dur = ML_CLAMP(remain, 0.08, a->dur);
        a->t0 = ml_now_s();
    }
    a->active = true;
}

ml_anim *ml_anim_xform(ml_anim_engine *e, ml_xform from, ml_xform to, double dur_s, ml_ease ease)
{
    ml_anim *a = ml_anim_start(e, 0, 1, dur_s, ease);
    if (!a) return NULL;
    a->has_xf = true;
    a->xf.from = from;
    a->xf.to = to;
    a->xf_cur = from;
    return a;
}
ml_xform ml_anim_xform_value(ml_anim *a)
{
    if (!a || !a->has_xf) { ml_xform z = { 1, 1, { 0, 0 } }; return z; }
    return a->xf_cur;
}
