#ifndef ML_SURFACE_H
#define ML_SURFACE_H
#include "ml/common.h"
#include "ml/region.h"

/* A surface is a CPU-visible RGBA8888 buffer plus its damage region.
 * Client processes own one surface each (shared memory); the compositor owns
 * the framebuffer. Keeping damage on the surface is what allows the
 * "event -> update state -> render changed region only" flow of spec §10. */
typedef struct ml_surface {
    uint32_t id;
    int w, h, stride;          /* stride in pixels */
    uint32_t *px;              /* straight (non premultiplied) RGBA8888 */
    bool owns_px;
    ml_region damage;
    uint32_t seq;              /* bumped on every commit; compositor skips untouched */
    void *backend_tex;         /* GL texture / drm bo handle when accelerated */
    uint64_t bytes;            /* accounted against memory budgets */
} ml_surface;

ml_surface *ml_surface_new(int w, int h);
ml_surface *ml_surface_wrap(uint32_t *px, int w, int h, int stride);
void ml_surface_free(ml_surface *s);
bool ml_surface_resize(ml_surface *s, int w, int h);
void ml_surface_clear(ml_surface *s, ml_color c);
void ml_surface_damage(ml_surface *s, ml_rect r);
void ml_surface_damage_all(ml_surface *s);
bool ml_surface_take_damage(ml_surface *s, ml_region *out);  /* true if non-empty */
static inline uint32_t *ml_surface_row(ml_surface *s, int y) { return s->px + (size_t)y * s->stride; }
#endif
