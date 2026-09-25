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
    if (!src->n) return;               /* memcpy(NULL, NULL, 0) is UB */
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

#define ML_COVER_MAX_IV 64

bool ml_region_covers_rect(const ml_region *g, ml_rect r)
{
    if (!g || ml_rect_empty(r)) return true;
    int right = ml_rect_right(r);
    for (int y = r.y; y < ml_rect_bottom(r); y++) {
        int x0[ML_COVER_MAX_IV], x1[ML_COVER_MAX_IV], n = 0;
        for (size_t i = 0; i < g->n && n < ML_COVER_MAX_IV; i++) {
            ml_rect h = ml_rect_intersect(g->r[i], ml_rect_make(r.x, y, r.w, 1));
            if (ml_rect_empty(h)) continue;
            x0[n] = h.x;
            x1[n] = ml_rect_right(h);
            n++;
        }
        if (!n) return false;
        if (n >= ML_COVER_MAX_IV) continue;   /* too fragmented to decide cheaply */
        for (int i = 1; i < n; i++) {         /* insertion sort by x0 */
            int a = x0[i], b = x1[i], j = i;
            while (j > 0 && x0[j - 1] > a) { x0[j] = x0[j - 1]; x1[j] = x1[j - 1]; j--; }
            x0[j] = a; x1[j] = b;
        }
        int reach = r.x;
        for (int i = 0; i < n; i++) {
            if (x0[i] > reach) return false;  /* a gap on this scanline */
            if (x1[i] > reach) reach = x1[i];
            if (reach >= right) break;
        }
        if (reach < right) return false;
    }
    return true;
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

/* One full sweep: merge every pair that can be merged, drop every rect that is
 * contained in another. Returns the number of rects removed. */
static size_t simplify_pass(ml_region *g)
{
    size_t removed = 0;
    for (size_t i = 0; i < g->n; i++) {
        for (size_t j = i + 1; j < g->n; ) {
            ml_rect out;
            if (can_merge(g->r[i], g->r[j], &out)) {
                g->r[i] = out;
                ml_region_remove_at(g, j);
                removed++;
                continue;               /* j now holds the next rect */
            }
            if (ml_rect_contains_rect(g->r[i], g->r[j])) {
                ml_region_remove_at(g, j);
                removed++;
                continue;
            }
            if (ml_rect_contains_rect(g->r[j], g->r[i])) {
                g->r[i] = g->r[j];
                ml_region_remove_at(g, j);
                removed++;
                continue;
            }
            if (ml_rect_intersects(g->r[i], g->r[j])) {
                /* overlapping clips double-paint everything under them;
                 * one bounding rect repaints a little extra but once. */
                ml_rect a = g->r[i], b = g->r[j];
                int x0 = ML_MIN(a.x, b.x), y0 = ML_MIN(a.y, b.y);
                int x1 = ML_MAX(ml_rect_right(a), ml_rect_right(b));
                int y1 = ML_MAX(ml_rect_bottom(a), ml_rect_bottom(b));
                g->r[i] = ml_rect_make(x0, y0, x1 - x0, y1 - y0);
                ml_region_remove_at(g, j);
                removed++;
                continue;
            }
            j++;
        }
    }
    return removed;
}

void ml_region_simplify(ml_region *g)
{
    if (!g || g->n < 2) return;
    /* Bound the work by the list length: a region that is pairwise disjoint
     * needs at most n-1 merges, and every sweep either removes a rect or ends
     * the loop. The previous implementation merged exactly *one* pair per sweep
     * and gave up after 64 sweeps, so a burst of pointer movement (120 moves
     * -> 240 damage rects) left 176 overlapping rects behind and the cursor was
     * rasterized once per overlapping rect on every frame. */
    size_t guard = g->n + 2;
    while (guard--) {
        if (simplify_pass(g) == 0) break;
        if (g->n < 2) break;
    }
}
