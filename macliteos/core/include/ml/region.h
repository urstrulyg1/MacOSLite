#ifndef ML_REGION_H
#define ML_REGION_H
#include "ml/common.h"

/* Region algebra used for damage tracking.
 * The compositor never redraws the whole screen: clients and animations report
 * damage, we accumulate it into a region, and the renderer clips to it. */
typedef struct {
    ml_rect *r;
    size_t n, cap;
} ml_region;

void ml_region_init(ml_region *g);
void ml_region_free(ml_region *g);
void ml_region_clear(ml_region *g);
void ml_region_copy(ml_region *dst, const ml_region *src);
bool ml_region_empty(const ml_region *g);
size_t ml_region_count(const ml_region *g);
int64_t ml_region_area(const ml_region *g);          /* sum, may double count overlaps */
ml_rect ml_region_bounds(const ml_region *g);
bool ml_region_contains(const ml_region *g, int x, int y);
bool ml_region_intersects_rect(const ml_region *g, ml_rect r);

static inline void ml_region_remove_at(ml_region *g, size_t i)
{
    if (i >= g->n) return;
    memmove(&g->r[i], &g->r[i + 1], (g->n - i - 1) * sizeof(ml_rect));
    g->n--;
}

void ml_region_add(ml_region *g, ml_rect r);
void ml_region_add_region(ml_region *g, const ml_region *o);
void ml_region_subtract(ml_region *g, ml_rect r);
void ml_region_subtract_region(ml_region *g, const ml_region *o);
void ml_region_intersect(ml_region *g, ml_rect clip);
void ml_region_translate(ml_region *g, int dx, int dy);
void ml_region_scale(ml_region *g, int num, int den);
/* Merge rects that share edges: keeps the rect list short so the renderer
 * issues few draws. Bounded work: O(n^2) on a list that is normally < 16. */
void ml_region_simplify(ml_region *g);

#endif
