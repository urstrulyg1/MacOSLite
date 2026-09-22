#include "ml/surface.h"
#include "ml/log.h"

ml_surface *ml_surface_new(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    ml_surface *s = ml_zalloc(sizeof *s);
    s->w = w; s->h = h; s->stride = w;
    s->bytes = (uint64_t)w * h * 4;
    s->px = ml_zalloc(s->bytes);
    s->owns_px = true;
    ml_region_init(&s->damage);
    return s;
}
ml_surface *ml_surface_wrap(uint32_t *px, int w, int h, int stride)
{
    ml_surface *s = ml_zalloc(sizeof *s);
    s->w = w; s->h = h; s->stride = stride > 0 ? stride : w;
    s->px = px;
    s->owns_px = false;
    s->bytes = (uint64_t)s->stride * h * 4;
    ml_region_init(&s->damage);
    return s;
}
void ml_surface_free(ml_surface *s)
{
    if (!s) return;
    ml_region_free(&s->damage);
    if (s->owns_px) ml_free(s->px);
    ml_free(s);
}
bool ml_surface_resize(ml_surface *s, int w, int h)
{
    if (!s || w <= 0 || h <= 0) return false;
    if (s->w == w && s->h == h) return true;
    if (!s->owns_px) return false;
    ml_free(s->px);
    s->w = w; s->h = h; s->stride = w;
    s->bytes = (uint64_t)w * h * 4;
    s->px = ml_zalloc(s->bytes);
    ml_region_clear(&s->damage);
    ml_surface_damage_all(s);
    return true;
}
void ml_surface_clear(ml_surface *s, ml_color c)
{
    uint32_t v = ml_color_u32(c);
    for (int y = 0; y < s->h; y++) {
        uint32_t *row = ml_surface_row(s, y);
        for (int x = 0; x < s->w; x++) row[x] = v;
    }
    ml_surface_damage_all(s);
}
void ml_surface_damage(ml_surface *s, ml_rect r)
{
    r = ml_rect_intersect(r, ml_rect_make(0, 0, s->w, s->h));
    if (ml_rect_empty(r)) return;
    ml_region_add(&s->damage, r);
}
void ml_surface_damage_all(ml_surface *s)
{
    ml_region_clear(&s->damage);
    ml_surface_damage(s, ml_rect_make(0, 0, s->w, s->h));
}
bool ml_surface_take_damage(ml_surface *s, ml_region *out)
{
    if (ml_region_empty(&s->damage)) return false;
    ml_region_copy(out, &s->damage);
    ml_region_simplify(out);
    ml_region_clear(&s->damage);
    s->seq++;
    return true;
}
