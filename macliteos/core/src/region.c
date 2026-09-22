#include "ml/region.h"

static void grow(ml_region *g, size_t need)
{
    if (need <= g->cap) return;
    size_t cap = g->cap ? g->cap : 16;
    while (cap < need) cap *= 2;
    g->r = ml_realloc(g->r, cap * sizeof(ml_rect));
    g->cap = cap;
}

void ml_region_init(ml_region *g) { g->r = NULL; g->n = g->cap = 0; }
void ml_region_free(ml_region *g) { ml_free(g->r); g->r = NULL; g->n = g->cap = 0; }
void ml_region_clear(ml_region *g) { g->n = 0; }
bool ml_region_empty(const ml_region *g) { return g->n == 0; }
size_t ml_region_count(const ml_region *g) { return g->n; }

void ml_region_copy(ml_region *dst, const ml_region *src)
{
    ml_region_clear(dst);
    grow(dst, src->n);
    memcpy(dst->r, src->r, src->n * sizeof(ml_rect));
    dst->n = src->n;
}

int64_t ml_region_area(const ml_region *g)
{
    int64_t a = 0;
    for (size_t i = 0; i < g->n; i++) a += ml_rect_area(g->r[i]);
    return a;
}

ml_rect ml_region_bounds(const ml_region *g)
{
    ml_rect b = { 0, 0, 0, 0 };
    for (size_t i = 0; i < g->n; i++) b = ml_rect_union_bounds(b, g->r[i]);
    return b;
}

bool ml_region_contains(const ml_region *g, int x, int y)
{
    for (size_t i = 0; i < g->n; i++)
        if (ml_rect_contains(g->r[i], x, y)) return true;
    return false;
}
bool ml_region_intersects_rect(const ml_region *g, ml_rect r)
{
    for (size_t i = 0; i < g->n; i++)
        if (ml_rect_intersects(g->r[i], r)) return true;
    return false;
}

void ml_region_add(ml_region *g, ml_rect r)
{
    if (ml_rect_empty(r)) return;
    grow(g, g->n + 1);
    g->r[g->n++] = r;
}

void ml_region_add_region(ml_region *g, const ml_region *o)
{
    if (!o->n) return;
    grow(g, g->n + o->n);
    for (size_t i = 0; i < o->n; i++)
        if (!ml_rect_empty(o->r[i])) g->r[g->n++] = o->r[i];
}

static void sub_one(ml_region *g, size_t idx, ml_rect s)
{
    ml_rect r = g->r[idx];
    if (!ml_rect_intersects(r, s)) return;
    /* remove r, then push up to 4 remainder pieces */
    ml_region_remove_at(g, idx);
    ml_rect pieces[4];
    int n = 0;
    if (r.x < s.x) pieces[n++] = ml_rect_make(r.x, r.y, s.x - r.x, r.h);
    if (ml_rect_right(r) > ml_rect_right(s))
        pieces[n++] = ml_rect_make(ml_rect_right(s), r.y, ml_rect_right(r) - ml_rect_right(s), r.h);
    int x1 = ML_MAX(r.x, s.x), x2 = ML_MIN(ml_rect_right(r), ml_rect_right(s));
    if (r.y < s.y) pieces[n++] = ml_rect_make(x1, r.y, x2 - x1, s.y - r.y);
    if (ml_rect_bottom(r) > ml_rect_bottom(s))
        pieces[n++] = ml_rect_make(x1, ml_rect_bottom(s), x2 - x1, ml_rect_bottom(r) - ml_rect_bottom(s));
    for (int i = 0; i < n; i++) ml_region_add(g, pieces[i]);
}

void ml_region_subtract(ml_region *g, ml_rect s)
{
    if (ml_rect_empty(s)) return;
    for (size_t i = 0; i < g->n; i++) sub_one(g, i, s);
}

void ml_region_subtract_region(ml_region *g, const ml_region *o)
{
    for (size_t i = 0; i < o->n; i++) ml_region_subtract(g, o->r[i]);
}

void ml_region_intersect(ml_region *g, ml_rect clip)
{
    for (size_t i = 0; i < g->n; i++) {
        g->r[i] = ml_rect_intersect(g->r[i], clip);
        if (ml_rect_empty(g->r[i])) ml_region_remove_at(g, i--);
    }
}

void ml_region_translate(ml_region *g, int dx, int dy)
{
    for (size_t i = 0; i < g->n; i++) { g->r[i].x += dx; g->r[i].y += dy; }
}

void ml_region_scale(ml_region *g, int num, int den)
{
    if (den == 0 || num == den) return;
    for (size_t i = 0; i < g->n; i++) {
        ml_rect r = g->r[i];
        int x2 = (ml_rect_right(r) * num + den - 1) / den;
        int y2 = (ml_rect_bottom(r) * num + den - 1) / den;
        r.x = r.x * num / den;
        r.y = r.y * num / den;
        r.w = ML_MAX(1, x2 - r.x);
        r.h = ML_MAX(1, y2 - r.y);
        g->r[i] = r;
    }
}

static bool can_merge(ml_rect a, ml_rect b, ml_rect *out)
{
    if (a.x == b.x && a.w == b.w) {
        if (a.y == ml_rect_bottom(b)) { *out = ml_rect_make(a.x, b.y, a.w, a.h + b.h); return true; }
        if (b.y == ml_rect_bottom(a)) { *out = ml_rect_make(a.x, a.y, a.w, a.h + b.h); return true; }
    }
    if (a.y == b.y && a.h == b.h) {
        if (a.x == ml_rect_right(b)) { *out = ml_rect_make(b.x, a.y, a.w + b.w, a.h); return true; }
        if (b.x == ml_rect_right(a)) { *out = ml_rect_make(a.x, a.y, a.w + b.w, a.h); return true; }
    }
    return false;
}

void ml_region_simplify(ml_region *g)
{
    bool merged = true;
    int guard = 0;
    while (merged && guard++ < 64) {
        merged = false;
        for (size_t i = 0; i < g->n && !merged; i++)
            for (size_t j = i + 1; j < g->n && !merged; j++) {
                ml_rect out;
                if (can_merge(g->r[i], g->r[j], &out)) {
                    g->r[i] = out;
                    ml_region_remove_at(g, j);
                    merged = true;
                } else if (ml_rect_contains_rect(g->r[i], g->r[j])) {
                    /* j adds nothing: painting it would repaint i's pixels */
                    ml_region_remove_at(g, j);
                    merged = true;
                } else if (ml_rect_contains_rect(g->r[j], g->r[i])) {
                    g->r[i] = g->r[j];
                    ml_region_remove_at(g, j);
                    merged = true;
                } else if (ml_rect_intersects(g->r[i], g->r[j])) {
                    /* overlapping clips double-paint everything under them;
                     * one bounding rect repaints a little extra but once. */
                    ml_rect a = g->r[i], b = g->r[j];
                    int x0 = ML_MIN(a.x, b.x), y0 = ML_MIN(a.y, b.y);
                    int x1 = ML_MAX(a.x + a.w, b.x + b.w), y1 = ML_MAX(a.y + a.h, b.y + b.h);
                    g->r[i] = ml_rect_make(x0, y0, x1 - x0, y1 - y0);
                    ml_region_remove_at(g, j);
                    merged = true;
                }
            }
    }
}
