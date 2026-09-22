#include "ml/raster.h"
#include "ml/cache.h"
#include "ml/log.h"
#include <math.h>

static ml_raster_stats S;
void ml_raster_reset_stats(void) { memset(&S, 0, sizeof S); }
const ml_raster_stats *ml_raster_get_stats(void) { return &S; }

/* ============================ path ===================================== */
static void path_reserve(ml_path *p, size_t extra)
{
    if (p->n + extra <= p->cap) return;
    size_t cap = p->cap ? p->cap : 32;
    while (cap < p->n + extra) cap *= 2;
    p->p = ml_realloc(p->p, cap * sizeof *p->p);
    p->cap = cap;
}
static void sub_reserve(ml_path *p)
{
    if (p->nsub + 1 <= p->capsub) return;
    size_t cap = p->capsub ? p->capsub * 2 : 8;
    p->sub = ml_realloc(p->sub, cap * sizeof *p->sub);
    p->subclosed = ml_realloc(p->subclosed, cap * sizeof *p->subclosed);
    p->capsub = cap;
}
void ml_path_init(ml_path *p) { memset(p, 0, sizeof *p); }
void ml_path_free(ml_path *p) { ml_free(p->p); ml_free(p->sub); ml_free(p->subclosed); memset(p, 0, sizeof *p); }
void ml_path_reset(ml_path *p) { p->n = 0; p->nsub = 0; }
void ml_path_move(ml_path *p, double x, double y)
{
    sub_reserve(p);
    p->sub[p->nsub] = p->n;
    p->subclosed[p->nsub] = false;
    p->nsub++;
    path_reserve(p, 1);
    p->p[p->n].x = x; p->p[p->n].y = y; p->n++;
}
void ml_path_line(ml_path *p, double x, double y)
{
    if (!p->nsub) ml_path_move(p, x, y);
    path_reserve(p, 1);
    p->p[p->n].x = x; p->p[p->n].y = y; p->n++;
}
void ml_path_quad(ml_path *p, double cx, double cy, double x, double y, int seg)
{
    if (!p->nsub || p->n == 0) { ml_path_move(p, cx, cy); return; }
    if (seg <= 0) seg = 8;
    ml_pointd p0 = p->p[p->n - 1];
    path_reserve(p, (size_t)seg);
    for (int i = 1; i <= seg; i++) {
        double t = (double)i / seg, u = 1 - t;
        p->p[p->n].x = u * u * p0.x + 2 * u * t * cx + t * t * x;
        p->p[p->n].y = u * u * p0.y + 2 * u * t * cy + t * t * y;
        p->n++;
    }
}
void ml_path_cubic(ml_path *p, double c1x, double c1y, double c2x, double c2y, double x, double y, int seg)
{
    if (!p->nsub || p->n == 0) { ml_path_move(p, c1x, c1y); return; }
    if (seg <= 0) seg = 12;
    ml_pointd p0 = p->p[p->n - 1];
    path_reserve(p, (size_t)seg);
    for (int i = 1; i <= seg; i++) {
        double t = (double)i / seg, u = 1 - t;
        p->p[p->n].x = u*u*u*p0.x + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x;
        p->p[p->n].y = u*u*u*p0.y + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*y;
        p->n++;
    }
}
void ml_path_close(ml_path *p)
{
    if (!p->nsub) return;
    p->subclosed[p->nsub - 1] = true;
    ml_pointd first = p->p[p->sub[p->nsub - 1]];
    if (p->n && (p->p[p->n - 1].x != first.x || p->p[p->n - 1].y != first.y)) ml_path_line(p, first.x, first.y);
}
void ml_path_rect(ml_path *p, double x, double y, double w, double h)
{
    ml_path_move(p, x, y);
    ml_path_line(p, x + w, y);
    ml_path_line(p, x + w, y + h);
    ml_path_line(p, x, y + h);
    ml_path_close(p);
}
void ml_path_rounded_rect(ml_path *p, double x, double y, double w, double h, double r)
{
    r = ML_CLAMP(r, 0, ML_MIN(w, h) / 2);
    if (r <= 0.01) { ml_path_rect(p, x, y, w, h); return; }
    const double k = 0.5522847498;
    ml_path_move(p, x + r, y);
    ml_path_line(p, x + w - r, y);
    ml_path_cubic(p, x + w - r + r * k, y, x + w, y + r - r * k, x + w, y + r, 8);
    ml_path_line(p, x + w, y + h - r);
    ml_path_cubic(p, x + w, y + h - r + r * k, x + w - r + r * k, y + h, x + w - r, y + h, 8);
    ml_path_line(p, x + r, y + h);
    ml_path_cubic(p, x + r - r * k, y + h, x, y + h - r + r * k, x, y + h - r, 8);
    ml_path_line(p, x, y + r);
    ml_path_cubic(p, x, y + r - r * k, x + r - r * k, y, x + r, y, 8);
    ml_path_close(p);
}

bool ml_path_parse(ml_path *p, const char *d)
{
    if (!d) return false;
    const char *s = d;
    double nums[6];
    int nn = 0;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        char cmd = *s++;
        if (cmd >= 'a' && cmd <= 'z') cmd -= 32;
        nn = 0;
        int need = (cmd == 'M' || cmd == 'L') ? 2 : cmd == 'Q' ? 4 : cmd == 'C' ? 6 : cmd == 'R' ? 5 : 0;
        if (cmd == 'Z') { ml_path_close(p); continue; }
        if (need == 0) { ML_WARN("path: unknown command '%c'", cmd); return false; }
        for (int i = 0; i < need; i++) {
            while (*s == ' ' || *s == ',') s++;
            char *end;
            nums[i] = strtod(s, &end);
            if (end == s) { ML_WARN("path: missing coordinate for '%c'", cmd); return false; }
            s = end;
        }
        nn = need;
        switch (cmd) {
        case 'M': ml_path_move(p, nums[0], nums[1]); break;
        case 'L': ml_path_line(p, nums[0], nums[1]); break;
        case 'Q': ml_path_quad(p, nums[0], nums[1], nums[2], nums[3], 8); break;
        case 'C': ml_path_cubic(p, nums[0], nums[1], nums[2], nums[3], nums[4], nums[5], 10); break;
        case 'R': ml_path_rounded_rect(p, nums[0], nums[1], nums[2], nums[3], nums[4]); break;
        default: break;
        }
        (void)nn;
    }
    return p->n > 0;
}

static void path_bbox(const ml_path *p, double *x0, double *y0, double *x1, double *y1)
{
    *x0 = *y0 = 1e18; *x1 = *y1 = -1e18;
    for (size_t i = 0; i < p->n; i++) {
        *x0 = ML_MIN(*x0, p->p[i].x); *x1 = ML_MAX(*x1, p->p[i].x);
        *y0 = ML_MIN(*y0, p->p[i].y); *y1 = ML_MAX(*y1, p->p[i].y);
    }
}

/* ============================ blending ================================= */
static inline uint32_t mul8(uint32_t v, uint32_t a) { return ((v * (a + 1)) >> 8) & 0xFFu; }
/* dst = src*alpha + dst*(1-alpha), alpha 0..255 (already includes opacity) */
static inline uint32_t blend_px(uint32_t s, uint32_t d, uint32_t a)
{
    if (a == 0) return d;
    uint32_t ia = 255 - a;
    uint32_t rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    uint32_t g  = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
    /* Porter-Duff "over" alpha: a + da*(1-a). The old formula multiplied the
     * source alpha in twice, punching translucent holes into any surface a
     * glow or shadow was blended onto (measured: 20% dark veil at glow core). */
    uint32_t oa = a + ((d >> 24) * ia >> 8);
    if (oa > 255) oa = 255;
    return rb | g | (oa << 24);
}
static inline uint32_t pack(ml_color c) { return ml_color_u32(c); }

void ml_ctx_init(ml_ctx *c, ml_surface *dst, ml_rect clip)
{
    c->dst = dst;
    c->clip = ml_rect_intersect(clip, ml_rect_make(0, 0, dst->w, dst->h));
}

/* ============================ primitives =============================== */
void ml_fill_rect(ml_ctx *c, ml_rect r, ml_color col)
{
    r = ml_rect_intersect(r, c->clip);
    if (ml_rect_empty(r) || col.a == 0) return;
    uint32_t sc = pack(col);
    ml_surface *d = c->dst;
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        if (col.a == 255) {
            for (int x = r.x; x < r.x + r.w; x++) row[x] = sc;
        } else {
            for (int x = r.x; x < r.x + r.w; x++) row[x] = blend_px(sc, row[x], col.a);
        }
    }
    S.pixels_blended += (uint64_t)r.w * r.h;
    S.draw_calls++;
    ml_surface_damage(d, r);
}

/* --- cached coverage masks -------------------------------------------- */
typedef struct { uint8_t *cov; int w, h; } mask_t;
static ml_cache *mask_cache;     /* rounded-rect coverage */
static ml_cache *shadow_cache;   /* blurred shadow masks */
static void mask_free(void *v) { mask_t *m = v; ml_free(m->cov); ml_free(m); }

static void caches_init(void)
{
    if (!mask_cache) mask_cache = ml_cache_new(4u * 1024 * 1024, 64, mask_free);
    if (!shadow_cache) shadow_cache = ml_cache_new(6u * 1024 * 1024, 48, mask_free);
}

/* signed distance to a rounded rect centred in (w,h) */
static double sd_rounded(double px, double py, double w, double h, double r)
{
    double qx = fabs(px - w * 0.5) - (w * 0.5 - r);
    double qy = fabs(py - h * 0.5) - (h * 0.5 - r);
    double ax = ML_MAX(qx, 0), ay = ML_MAX(qy, 0);
    return sqrt(ax * ax + ay * ay) + ML_MIN(ML_MAX(qx, qy), 0) - r;
}

static mask_t *mask_get(int w, int h, double r, double stroke)
{
    caches_init();
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return NULL;
    char key[64];
    snprintf(key, sizeof key, "%dx%d:r%.2f:s%.2f", w, h, r, stroke);
    mask_t *m = ml_cache_get(mask_cache, key);
    if (m) return m;
    m = ml_zalloc(sizeof *m);
    m->w = w; m->h = h;
    m->cov = ml_zalloc((size_t)w * h);
    double ri = ML_CLAMP(r, 0, ML_MIN(w, h) / 2.0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double sd = sd_rounded(x + 0.5, y + 0.5, w, h, ri);
            double d = stroke > 0 ? fabs(sd) - stroke * 0.5 : sd;
            double cov = ML_CLAMP(0.5 - d, 0.0, 1.0);
            m->cov[(size_t)y * w + x] = (uint8_t)(cov * 255.0 + 0.5);
        }
    }
    ml_cache_put(mask_cache, key, m, (size_t)w * h + sizeof *m);
    return m;
}

void ml_blit_coverage(ml_ctx *c, const uint8_t *cov, int cw, int ch, int dx, int dy, ml_color col)
{
    ml_surface *d = c->dst;
    ml_rect area = ml_rect_intersect(ml_rect_make(dx, dy, cw, ch), c->clip);
    if (ml_rect_empty(area) || col.a == 0) return;
    uint32_t sc = pack(col);
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        const uint8_t *crow = cov + (size_t)(y - dy) * cw;
        for (int x = area.x; x < area.x + area.w; x++) {
            uint32_t a = (uint32_t)crow[x - dx] * col.a / 255;
            row[x] = blend_px(sc, row[x], a);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_blit_coverage_scaled(ml_ctx *c, const uint8_t *cov, int cw, int ch, ml_rect dst, ml_color col)
{
    ml_rect area = ml_rect_intersect(dst, c->clip);
    if (ml_rect_empty(area) || col.a == 0) return;
    ml_surface *d = c->dst;
    uint32_t sc = pack(col);
    double sx = (double)cw / dst.w, sy = (double)ch / dst.h;
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        double fy = (y - dst.y + 0.5) * sy - 0.5;
        int y0 = (int)floor(fy);
        double wy = fy - y0;
        int y1 = ML_CLAMP(y0 + 1, 0, ch - 1);
        y0 = ML_CLAMP(y0, 0, ch - 1);
        for (int x = area.x; x < area.x + area.w; x++) {
            double fx = (x - dst.x + 0.5) * sx - 0.5;
            int x0 = (int)floor(fx);
            double wx = fx - x0;
            int x1 = ML_CLAMP(x0 + 1, 0, cw - 1);
            x0 = ML_CLAMP(x0, 0, cw - 1);
            double c00 = cov[(size_t)y0 * cw + x0], c01 = cov[(size_t)y0 * cw + x1];
            double c10 = cov[(size_t)y1 * cw + x0], c11 = cov[(size_t)y1 * cw + x1];
            double v = (c00 * (1 - wx) + c01 * wx) * (1 - wy) + (c10 * (1 - wx) + c11 * wx) * wy;
            uint32_t a = (uint32_t)(v * col.a / 255.0);
            row[x] = blend_px(sc, row[x], ML_CLAMP(a, 0, 255));
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_blit_coverage_src(ml_ctx *c, const uint8_t *cov, int stride, int ch_total,
                          ml_rect src, ml_rect dst, ml_color col)
{
    (void)ch_total;
    ml_rect area = ml_rect_intersect(dst, c->clip);
    if (ml_rect_empty(area) || col.a == 0 || ml_rect_empty(src)) return;
    ml_surface *d = c->dst;
    uint32_t sc = pack(col);
    double sx = (double)src.w / dst.w, sy = (double)src.h / dst.h;
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        double fy = (y - dst.y + 0.5) * sy - 0.5 + src.y;
        int y0 = (int)floor(fy);
        double wy = fy - y0;
        int y1 = ML_CLAMP(y0 + 1, src.y, src.y + src.h - 1);
        y0 = ML_CLAMP(y0, src.y, src.y + src.h - 1);
        const uint8_t *r0 = cov + (size_t)y0 * stride;
        const uint8_t *r1 = cov + (size_t)y1 * stride;
        for (int x = area.x; x < area.x + area.w; x++) {
            double fx = (x - dst.x + 0.5) * sx - 0.5 + src.x;
            int x0 = (int)floor(fx);
            double wx = fx - x0;
            int x1 = ML_CLAMP(x0 + 1, src.x, src.x + src.w - 1);
            x0 = ML_CLAMP(x0, src.x, src.x + src.w - 1);
            double v = (r0[x0] * (1 - wx) + r0[x1] * wx) * (1 - wy)
                     + (r1[x0] * (1 - wx) + r1[x1] * wx) * wy;
            uint32_t a = (uint32_t)ML_CLAMP(v * col.a / 255.0, 0.0, 255.0);
            row[x] = blend_px(sc, row[x], a);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_fill_radial_glow(ml_ctx *c, ml_pointd center, double rx, double ry, ml_color col)
{
    if (rx <= 0 || ry <= 0 || col.a == 0) return;
    ml_rect box = ml_rect_make((int)(center.x - rx), (int)(center.y - ry), (int)(rx * 2), (int)(ry * 2));
    ml_rect area = ml_rect_intersect(box, c->clip);
    if (ml_rect_empty(area)) return;
    ml_surface *d = c->dst;
    uint32_t sc = pack(col);
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        double dy = (y + 0.5 - center.y) / ry;
        for (int x = area.x; x < area.x + area.w; x++) {
            double dx = (x + 0.5 - center.x) / rx;
            double dist = sqrt(dx * dx + dy * dy);
            if (dist >= 1.0) continue;
            double fall = (1 - dist);
            fall = fall * fall * fall;      /* cubic: no visible ellipse rim */
            uint32_t a = (uint32_t)ML_CLAMP(fall * col.a, 0.0, 255.0);
            row[x] = blend_px(sc, row[x], a);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_fill_rounded(ml_ctx *c, ml_rect r, double radius, ml_color col)
{
    ml_rect area = ml_rect_intersect(r, c->clip);
    if (ml_rect_empty(area) || col.a == 0) return;
    if (radius <= 0.5) { ml_fill_rect(c, r, col); return; }
    mask_t *m = mask_get(r.w, r.h, radius, 0);
    if (!m) { ml_fill_rect(c, area, col); return; }
    ml_blit_coverage(c, m->cov, m->w, m->h, r.x, r.y, col);
}

void ml_stroke_rounded(ml_ctx *c, ml_rect r, double radius, double width, ml_color col)
{
    if (width <= 0 || col.a == 0) return;
    mask_t *m = mask_get(r.w, r.h, radius, width);
    if (!m) return;
    ml_blit_coverage(c, m->cov, m->w, m->h, r.x, r.y, col);
}

void ml_fill_gradient(ml_ctx *c, ml_rect r, double radius, ml_color top, ml_color bottom)
{
    ml_rect area = ml_rect_intersect(r, c->clip);
    if (ml_rect_empty(area)) return;
    mask_t *m = radius > 0.5 ? mask_get(r.w, r.h, radius, 0) : NULL;
    ml_surface *d = c->dst;
    double invh = r.h > 1 ? 1.0 / (r.h - 1) : 1.0;
    for (int y = area.y; y < area.y + area.h; y++) {
        double t = ML_CLAMP((y - r.y) * invh, 0, 1);
        ml_color col = ml_color_mix(top, bottom, t);
        uint32_t sc = pack(col);
        uint32_t *row = ml_surface_row(d, y);
        const uint8_t *crow = m ? m->cov + (size_t)(y - r.y) * m->w : NULL;
        for (int x = area.x; x < area.x + area.w; x++) {
            uint32_t a = col.a;
            if (crow) a = (uint32_t)crow[x - r.x] * col.a / 255;
            row[x] = blend_px(sc, row[x], a);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_fill_gradient_diag(ml_ctx *c, ml_rect r, double radius, ml_color a, ml_color b)
{
    ml_rect area = ml_rect_intersect(r, c->clip);
    if (ml_rect_empty(area)) return;
    mask_t *m = radius > 0.5 ? mask_get(r.w, r.h, radius, 0) : NULL;
    ml_surface *d = c->dst;
    double denom = (double)(r.w + r.h);
    if (denom <= 0) denom = 1;
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        const uint8_t *crow = m ? m->cov + (size_t)(y - r.y) * m->w : NULL;
        for (int x = area.x; x < area.x + area.w; x++) {
            double t = ((x - r.x) + (y - r.y)) / denom;
            ml_color col = ml_color_mix(a, b, ML_CLAMP(t, 0, 1));
            uint32_t al = col.a;
            if (crow) al = (uint32_t)crow[x - r.x] * col.a / 255;
            row[x] = blend_px(pack(col), row[x], al);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_blit(ml_ctx *c, const ml_surface *src, ml_rect sr, int dx, int dy, uint8_t opacity)
{
    sr = ml_rect_intersect(sr, ml_rect_make(0, 0, src->w, src->h));
    if (ml_rect_empty(sr)) return;
    ml_rect dstrect = ml_rect_make(dx + sr.x, dy + sr.y, sr.w, sr.h);
    ml_rect area = ml_rect_intersect(dstrect, c->clip);
    if (ml_rect_empty(area)) return;
    ml_surface *d = c->dst;
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *drow = ml_surface_row(d, y);
        const uint32_t *srow = src->px + (size_t)(y - dy) * src->stride;
        for (int x = area.x; x < area.x + area.w; x++) {
            uint32_t sc = srow[x - dx];
            uint32_t a = ((sc >> 24) * opacity) / 255;
            drow[x] = a >= 255 ? sc : blend_px(sc, drow[x], a);
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

void ml_blit_scaled(ml_ctx *c, const ml_surface *src, ml_rect dst, ml_rect sr, uint8_t opacity)
{
    sr = ml_rect_intersect(sr, ml_rect_make(0, 0, src->w, src->h));
    dst = ml_rect_intersect(dst, c->clip);
    if (ml_rect_empty(sr) || ml_rect_empty(dst)) return;
    if (sr.w == dst.w && sr.h == dst.h) { ml_blit(c, src, sr, dst.x - sr.x, dst.y - sr.y, opacity); return; }
    ml_surface *d = c->dst;
    double fx = (double)sr.w / dst.w, fy = (double)sr.h / dst.h;
    for (int y = dst.y; y < dst.y + dst.h; y++) {
        uint32_t *drow = ml_surface_row(d, y);
        double sy = (y - dst.y + 0.5) * fy - 0.5 + sr.y;
        int y0 = (int)floor(sy);
        double wy = sy - y0;
        int y1 = ML_CLAMP(y0 + 1, sr.y, sr.y + sr.h - 1);
        y0 = ML_CLAMP(y0, sr.y, sr.y + sr.h - 1);
        const uint32_t *r0 = src->px + (size_t)y0 * src->stride;
        const uint32_t *r1 = src->px + (size_t)y1 * src->stride;
        for (int x = dst.x; x < dst.x + dst.w; x++) {
            double sx = (x - dst.x + 0.5) * fx - 0.5 + sr.x;
            int x0 = (int)floor(sx);
            double wx = sx - x0;
            int x1 = ML_CLAMP(x0 + 1, sr.x, sr.x + sr.w - 1);
            x0 = ML_CLAMP(x0, sr.x, sr.x + sr.w - 1);
            uint32_t p00 = r0[x0], p01 = r0[x1], p10 = r1[x0], p11 = r1[x1];
            uint32_t out = 0;
            /* per-channel bilinear */
            uint32_t rr = (uint32_t)(((p00 >> 16) & 0xFF) * (1 - wx) * (1 - wy) + ((p01 >> 16) & 0xFF) * wx * (1 - wy)
                                     + ((p10 >> 16) & 0xFF) * (1 - wx) * wy + ((p11 >> 16) & 0xFF) * wx * wy);
            uint32_t gg = (uint32_t)(((p00 >> 8) & 0xFF) * (1 - wx) * (1 - wy) + ((p01 >> 8) & 0xFF) * wx * (1 - wy)
                                     + ((p10 >> 8) & 0xFF) * (1 - wx) * wy + ((p11 >> 8) & 0xFF) * wx * wy);
            uint32_t bb = (uint32_t)(((p00) & 0xFF) * (1 - wx) * (1 - wy) + ((p01) & 0xFF) * wx * (1 - wy)
                                     + ((p10) & 0xFF) * (1 - wx) * wy + ((p11) & 0xFF) * wx * wy);
            uint32_t aa = (uint32_t)(((p00 >> 24) * (1 - wx) * (1 - wy) + (p01 >> 24) * wx * (1 - wy)
                                     + (p10 >> 24) * (1 - wx) * wy + (p11 >> 24) * wx * wy));
            out = (aa << 24) | (ML_CLAMP(rr, 0, 255) << 16) | (ML_CLAMP(gg, 0, 255) << 8) | ML_CLAMP(bb, 0, 255);
            uint32_t a = (aa * opacity) / 255;
            drow[x] = a >= 255 ? out : blend_px(out, drow[x], a);
        }
    }
    S.pixels_blended += (uint64_t)dst.w * dst.h;
    S.draw_calls++;
    ml_surface_damage(d, dst);
}

/* --- path stroke: analytic distance field, AA ramp of 1px -------------- */
static double seg_dist(double px, double py, double ax, double ay, double bx, double by)
{
    double vx = bx - ax, vy = by - ay;
    double wx = px - ax, wy = py - ay;
    double len2 = vx * vx + vy * vy;
    double t = len2 > 1e-12 ? (wx * vx + wy * vy) / len2 : 0;
    t = ML_CLAMP(t, 0, 1);
    double dx = wx - t * vx, dy = wy - t * vy;
    return sqrt(dx * dx + dy * dy);
}

void ml_draw_path_stroke(ml_ctx *c, const ml_path *p, double width, ml_color col,
                         double ox, double oy, double scale)
{
    if (!p->n || col.a == 0 || width <= 0) return;
    double x0, y0, x1, y1;
    path_bbox(p, &x0, &y0, &x1, &y1);
    double hw = width * 0.5;
    ml_rect box = ml_rect_make((int)floor(x0 * scale + ox - hw - 1), (int)floor(y0 * scale + oy - hw - 1),
                               (int)ceil((x1 - x0) * scale + width + 2), (int)ceil((y1 - y0) * scale + width + 2));
    ml_rect area = ml_rect_intersect(box, c->clip);
    if (ml_rect_empty(area)) return;
    ml_surface *d = c->dst;
    uint32_t sc = pack(col);
    double wpx = width * scale;
    for (int y = area.y; y < area.y + area.h; y++) {
        uint32_t *row = ml_surface_row(d, y);
        for (int x = area.x; x < area.x + area.w; x++) {
            double px = (x + 0.5 - ox) / scale, py = (y + 0.5 - oy) / scale;
            double best = 1e18;
            for (size_t s = 0; s < p->nsub; s++) {
                size_t a = p->sub[s];
                size_t b = (s + 1 < p->nsub) ? p->sub[s + 1] : p->n;
                for (size_t i = a + 1; i < b; i++) {
                    double dd = seg_dist(px, py, p->p[i - 1].x, p->p[i - 1].y, p->p[i].x, p->p[i].y);
                    if (dd < best) best = dd;
                }
                if (p->subclosed[s] && b - a >= 3) {
                    double dd = seg_dist(px, py, p->p[b - 1].x, p->p[b - 1].y, p->p[a].x, p->p[a].y);
                    if (dd < best) best = dd;
                }
            }
            double dpx = best * scale;
            double cov = ML_CLAMP(wpx * 0.5 + 0.5 - dpx, 0.0, 1.0);
            if (cov <= 0) continue;
            row[x] = blend_px(sc, row[x], (uint32_t)(cov * col.a));
        }
    }
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

/* --- path fill: 4x vertical supersampling with analytic horizontal AA --- */
void ml_draw_path_fill(ml_ctx *c, const ml_path *p, ml_color col, double ox, double oy, double scale)
{
    if (!p->n || col.a == 0) return;
    double x0, y0, x1, y1;
    path_bbox(p, &x0, &y0, &x1, &y1);
    ml_rect box = ml_rect_make((int)floor(x0 * scale + ox) - 1, (int)floor(y0 * scale + oy) - 1,
                               (int)ceil((x1 - x0) * scale) + 3, (int)ceil((y1 - y0) * scale) + 3);
    ml_rect area = ml_rect_intersect(box, c->clip);
    if (ml_rect_empty(area)) return;
    ml_surface *d = c->dst;
    uint32_t sc = pack(col);
    const int SUB = 4;
    double *xs = ml_alloc(sizeof(double) * (p->n * 2 + 4));
    int *dir = ml_alloc(sizeof(int) * (p->n * 2 + 4));
    float *cover = ml_alloc(sizeof(float) * (size_t)(area.w + 2));
    for (int y = area.y; y < area.y + area.h; y++) {
        memset(cover, 0, sizeof(float) * (size_t)area.w);
        for (int s = 0; s < SUB; s++) {
            double sy = (y + (s + 0.5) / SUB - oy) / scale;
            size_t nx = 0;
            for (size_t si = 0; si < p->nsub; si++) {
                size_t a = p->sub[si];
                size_t b = (si + 1 < p->nsub) ? p->sub[si + 1] : p->n;
                size_t last = p->subclosed[si] ? a : (size_t)-1;
                for (size_t i = a + 1; i < b; i++) {
                    double ya = p->p[i - 1].y, yb = p->p[i].y;
                    if ((ya <= sy && yb > sy) || (yb <= sy && ya > sy)) {
                        double t = (sy - ya) / (yb - ya);
                        xs[nx] = p->p[i - 1].x + t * (p->p[i].x - p->p[i - 1].x);
                        dir[nx] = yb > ya ? 1 : -1;
                        nx++;
                    }
                }
                if (last != (size_t)-1 && b > a) {
                    double ya = p->p[b - 1].y, yb = p->p[last].y;
                    if ((ya <= sy && yb > sy) || (yb <= sy && ya > sy)) {
                        double t = (sy - ya) / (yb - ya);
                        xs[nx] = p->p[b - 1].x + t * (p->p[last].x - p->p[b - 1].x);
                        dir[nx] = yb > ya ? 1 : -1;
                        nx++;
                    }
                }
            }
            if (nx < 2) continue;
            /* sort by x (insertion sort: crossing lists are short) */
            for (size_t i = 1; i < nx; i++) {
                double kx = xs[i]; int kd = dir[i];
                size_t j = i;
                while (j > 0 && xs[j - 1] > kx) { xs[j] = xs[j - 1]; dir[j] = dir[j - 1]; j--; }
                xs[j] = kx; dir[j] = kd;
            }
            int wind = 0;
            for (size_t i = 0; i < nx; i++) {
                int prev = wind;
                wind += dir[i];
                if (prev == 0 && wind != 0) {
                    double sx0 = xs[i];
                    /* find span end: next crossing where winding returns to 0 */
                    double sx1 = (i + 1 < nx) ? xs[i + 1] : sx0;
                    int px0 = (int)floor(sx0 * scale + ox);
                    int px1 = (int)ceil(sx1 * scale + ox);
                    for (int px = px0; px < px1; px++) {
                        double lx = ML_MAX(sx0 * scale + ox, px);
                        double rx = ML_MIN(sx1 * scale + ox, px + 1);
                        if (rx > lx && px >= area.x && px < area.x + area.w)
                            cover[px - area.x] += (float)((rx - lx) / SUB);
                    }
                }
            }
        }
        uint32_t *row = ml_surface_row(d, y);
        for (int x = area.x; x < area.x + area.w; x++) {
            float cv = cover[x - area.x];
            if (cv <= 0) continue;
            uint32_t a = (uint32_t)ML_CLAMP(cv * (float)col.a, 0.0f, 255.0f);
            row[x] = blend_px(sc, row[x], a);
        }
    }
    ml_free(xs); ml_free(dir); ml_free(cover);
    S.pixels_blended += (uint64_t)area.w * area.h;
    S.draw_calls++;
    ml_surface_damage(d, area);
}

/* --- shadows ---------------------------------------------------------- */
static void box_blur_h(uint8_t *m, int w, int h, int r)
{
    if (r <= 0) return;
    uint8_t *tmp = ml_alloc((size_t)w * h);
    int div = r * 2 + 1;
    for (int y = 0; y < h; y++) {
        int sum = 0;
        const uint8_t *row = m + (size_t)y * w;
        for (int x = -r; x <= r; x++) sum += row[ML_CLAMP(x, 0, w - 1)];
        for (int x = 0; x < w; x++) {
            tmp[(size_t)y * w + x] = (uint8_t)(sum / div);
            int add = row[ML_CLAMP(x + r + 1, 0, w - 1)];
            int sub = row[ML_CLAMP(x - r, 0, w - 1)];
            sum += add - sub;
        }
    }
    memcpy(m, tmp, (size_t)w * h);
    ml_free(tmp);
}
static void box_blur_v(uint8_t *m, int w, int h, int r)
{
    if (r <= 0) return;
    uint8_t *tmp = ml_alloc((size_t)w * h);
    int div = r * 2 + 1;
    for (int x = 0; x < w; x++) {
        int sum = 0;
        for (int y = -r; y <= r; y++) sum += m[(size_t)ML_CLAMP(y, 0, h - 1) * w + x];
        for (int y = 0; y < h; y++) {
            tmp[(size_t)y * w + x] = (uint8_t)(sum / div);
            int add = m[(size_t)ML_CLAMP(y + r + 1, 0, h - 1) * w + x];
            int sub = m[(size_t)ML_CLAMP(y - r, 0, h - 1) * w + x];
            sum += add - sub;
        }
    }
    memcpy(m, tmp, (size_t)w * h);
    ml_free(tmp);
}

static mask_t *shadow_get(int w, int h, double radius, double blur)
{
    caches_init();
    if (w <= 0 || h <= 0) return NULL;
    int m = (int)ceil(blur * 2.0) + 2;
    int W = w + m * 2, H = h + m * 2;
    if (W > 8192 || H > 8192) return NULL;
    char key[80];
    snprintf(key, sizeof key, "sh:%dx%d:r%.2f:b%.2f", w, h, radius, blur);
    mask_t *cached = ml_cache_get(shadow_cache, key);
    if (cached) return cached;
    mask_t *out = ml_zalloc(sizeof *out);
    out->w = W; out->h = H;
    out->cov = ml_zalloc((size_t)W * H);
    /* inner rounded rect coverage, offset by m */
    double ri = ML_CLAMP(radius, 0, ML_MIN(w, h) / 2.0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double sd = sd_rounded(x + 0.5, y + 0.5, w, h, ri);
            out->cov[(size_t)(y + m) * W + (x + m)] = (uint8_t)(ML_CLAMP(0.5 - sd, 0, 1) * 255);
        }
    int r = ML_MAX(1, (int)lround(blur * 0.7));
    for (int i = 0; i < 3; i++) { box_blur_h(out->cov, W, H, r); box_blur_v(out->cov, W, H, r); }
    ml_cache_put(shadow_cache, key, out, (size_t)W * H + sizeof *out);
    return out;
}

void ml_draw_shadow(ml_ctx *c, ml_rect r, double radius, double blur, ml_color col)
{
    if (blur <= 0.5 || col.a == 0) return;
    mask_t *m = shadow_get(r.w, r.h, radius, blur);
    if (!m) return;
    int mw = (m->w - r.w) / 2, mh = (m->h - r.h) / 2;
    if (mw <= 0 || mh <= 0) return;
    /* 9-slice from the blurred mask: corners 1:1, edges stretched, centre never
     * drawn because the window itself covers it. */
    int W = m->w, H = m->h;
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(0, 0, mw, mh),
                         ml_rect_make(r.x - mw, r.y - mh, mw, mh), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(mw, 0, r.w, mh),
                         ml_rect_make(r.x, r.y - mh, r.w, mh), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(mw + r.w, 0, mw, mh),
                         ml_rect_make(r.x + r.w, r.y - mh, mw, mh), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(0, mh, mw, r.h),
                         ml_rect_make(r.x - mw, r.y, mw, r.h), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(mw + r.w, mh, mw, r.h),
                         ml_rect_make(r.x + r.w, r.y, mw, r.h), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(0, mh + r.h, mw, mh),
                         ml_rect_make(r.x - mw, r.y + r.h, mw, mh), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(mw, mh + r.h, r.w, mh),
                         ml_rect_make(r.x, r.y + r.h, r.w, mh), col);
    ml_blit_coverage_src(c, m->cov, W, H, ml_rect_make(mw + r.w, mh + r.h, mw, mh),
                         ml_rect_make(r.x + r.w, r.y + r.h, mw, mh), col);
}

void ml_raster_stroke_coverage(uint8_t *cov, int cw, int ch, const ml_path *p,
                               double width, double ox, double oy, double scale)
{
    if (!p->n || cw <= 0 || ch <= 0) return;
    double wpx = width * scale;
    for (int y = 0; y < ch; y++) {
        uint8_t *row = cov + (size_t)y * cw;
        for (int x = 0; x < cw; x++) {
            double px = (x + 0.5 - ox) / scale, py = (y + 0.5 - oy) / scale;
            double best = 1e18;
            for (size_t s = 0; s < p->nsub; s++) {
                size_t a = p->sub[s];
                size_t b = (s + 1 < p->nsub) ? p->sub[s + 1] : p->n;
                for (size_t i = a + 1; i < b; i++) {
                    double dd = seg_dist(px, py, p->p[i - 1].x, p->p[i - 1].y, p->p[i].x, p->p[i].y);
                    if (dd < best) best = dd;
                }
                if (p->subclosed[s] && b - a >= 3) {
                    double dd = seg_dist(px, py, p->p[b - 1].x, p->p[b - 1].y, p->p[a].x, p->p[a].y);
                    if (dd < best) best = dd;
                }
            }
            double dpx = best * scale;
            double c2 = ML_CLAMP(wpx * 0.5 + 0.5 - dpx, 0.0, 1.0);
            row[x] = (uint8_t)(c2 * 255.0 + 0.5);
        }
    }
}

void ml_raster_fill_coverage(uint8_t *cov, int cw, int ch, const ml_path *p,
                             double ox, double oy, double scale)
{
    if (!p->n || cw <= 0 || ch <= 0) return;
    const int SUB = 4;
    double *xs = ml_alloc(sizeof(double) * (p->n * 2 + 4));
    int *dir = ml_alloc(sizeof(int) * (p->n * 2 + 4));
    float *cover = ml_alloc(sizeof(float) * (size_t)cw);
    for (int y = 0; y < ch; y++) {
        memset(cover, 0, sizeof(float) * (size_t)cw);
        for (int s = 0; s < SUB; s++) {
            double sy = (y + (s + 0.5) / SUB - oy) / scale;
            size_t nx = 0;
            for (size_t si = 0; si < p->nsub; si++) {
                size_t a = p->sub[si];
                size_t b = (si + 1 < p->nsub) ? p->sub[si + 1] : p->n;
                bool closed = p->subclosed[si];
                for (size_t i = a + 1; i <= b; i++) {
                    size_t j = (i == b) ? a : i;
                    if (i == b && !closed) break;
                    double ya = p->p[i - 1].y, yb = p->p[j].y;
                    if ((ya <= sy && yb > sy) || (yb <= sy && ya > sy)) {
                        double t = (sy - ya) / (yb - ya);
                        xs[nx] = p->p[i - 1].x + t * (p->p[j].x - p->p[i - 1].x);
                        dir[nx] = yb > ya ? 1 : -1;
                        nx++;
                    }
                }
            }
            if (nx < 2) continue;
            for (size_t i = 1; i < nx; i++) {
                double kx = xs[i]; int kd = dir[i];
                size_t j = i;
                while (j > 0 && xs[j - 1] > kx) { xs[j] = xs[j - 1]; dir[j] = dir[j - 1]; j--; }
                xs[j] = kx; dir[j] = kd;
            }
            int wind = 0;
            for (size_t i = 0; i < nx; i++) {
                int prev = wind;
                wind += dir[i];
                if (prev == 0 && wind != 0 && i + 1 < nx) {
                    double sx0 = xs[i] * scale + ox, sx1 = xs[i + 1] * scale + ox;
                    int px0 = ML_MAX(0, (int)floor(sx0));
                    int px1 = ML_MIN(cw, (int)ceil(sx1));
                    for (int px = px0; px < px1; px++) {
                        double lx = ML_MAX(sx0, px), rx = ML_MIN(sx1, px + 1);
                        if (rx > lx) cover[px] += (float)((rx - lx) / SUB);
                    }
                }
            }
        }
        uint8_t *row = cov + (size_t)y * cw;
        for (int x = 0; x < cw; x++)
            if (cover[x] > 0) row[x] = (uint8_t)ML_CLAMP(cover[x] * 255.0f, 0.0f, 255.0f);
    }
    ml_free(xs); ml_free(dir); ml_free(cover);
}
