#include "ml/cursor.h"
#include "ml/log.h"
#include <math.h>

/* ---------------------------------------------------------------- sprite -- */

#define CURSOR_PROBE_MARGIN 4     /* scratch surface slack when measuring ink */

static const char *DEFAULT_CURSOR_PATH =
    "M0,0 L0,16 L4.4,12.4 L7.2,18.4 L9.8,17.2 L7,11.4 L12.4,11 Z";

void ml_cursor_sprite_init(ml_cursor_sprite *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
}

void ml_cursor_sprite_default(ml_cursor_sprite *s)
{
    memset(s, 0, sizeof *s);
    ml_cursor_sprite_set(s, DEFAULT_CURSOR_PATH, 1.0,
                         ml_rgb(250, 250, 252), ml_rgba(20, 22, 30, 200),
                         ML_CURSOR_DEFAULT_MARGIN);
}

void ml_cursor_sprite_free(ml_cursor_sprite *s)
{
    if (!s) return;
    ml_path_free(&s->path);
    memset(s, 0, sizeof *s);
}

/* Measure the pixels the sprite can touch.
 *
 * The path is rasterized into a scratch surface filled with a colour the
 * sprite never produces, at a handful of integer placements, and the bounding
 * box of every differing pixel is taken. Taking the union over several
 * placements makes the result independent of where the hotspot happens to land
 * relative to the pixel grid (the anti-aliasing ramp moves by a pixel), so the
 * measured box is conservative rather than lucky.
 */
static bool measure_ink(ml_cursor_sprite *s)
{
    double x0, y0, x1, y1;
    ml_path_bbox(&s->path, &x0, &y0, &x1, &y1);
    int bw = (int)ceil(x1 - x0) + 2 * (int)ceil(s->stroke) + 2 * CURSOR_PROBE_MARGIN + 4;
    int bh = (int)ceil(y1 - y0) + 2 * (int)ceil(s->stroke) + 2 * CURSOR_PROBE_MARGIN + 4;
    if (bw <= 0 || bh <= 0) return false;

    ml_surface *scratch = ml_surface_new(bw, bh);
    if (!scratch) return false;

    /* a colour the sprite cannot produce: pure green with a unique alpha */
    const uint32_t sentinel = ml_color_u32(ml_rgba(1, 254, 3, 77));

    ml_rect ink = ml_rect_make(0, 0, 0, 0);
    const int placements[][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 3} };
    for (size_t p = 0; p < ML_ARRAY_SIZE(placements); p++) {
        int ox = CURSOR_PROBE_MARGIN + placements[p][0];
        int oy = CURSOR_PROBE_MARGIN + placements[p][1];
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
                ml_surface_row(scratch, y)[x] = sentinel;

        ml_ctx c;
        ml_ctx_init(&c, scratch, ml_rect_make(0, 0, bw, bh));
        ml_draw_path_fill(&c, &s->path, s->fill, ox, oy, 1.0);
        ml_draw_path_stroke(&c, &s->path, s->stroke, s->outline, ox, oy, 1.0);

        for (int y = 0; y < bh; y++) {
            for (int x = 0; x < bw; x++) {
                if (ml_surface_row(scratch, y)[x] == sentinel) continue;
                ml_rect px = ml_rect_make(x - ox, y - oy, 1, 1);
                ink = ml_rect_union_bounds(ink, px);
            }
        }
    }
    ml_surface_free(scratch);

    if (ml_rect_empty(ink)) return false;
    s->ink = ink;
    return true;
}

bool ml_cursor_sprite_set(ml_cursor_sprite *s, const char *path_d, double stroke,
                          ml_color fill, ml_color outline, int margin)
{
    if (!s || !path_d) return false;

    /* Build into a temporary first: a sprite that cannot be rasterized must
     * leave the caller's sprite exactly as it was rather than half-updated. */
    ml_cursor_sprite next;
    memset(&next, 0, sizeof next);
    ml_path_init(&next.path);
    if (!ml_path_parse(&next.path, path_d)) {
        ML_ERR("cursor: sprite path is malformed (%s); the pointer cannot be drawn safely", path_d);
        ml_path_free(&next.path);
        return false;
    }
    next.path_ok = true;
    next.stroke = stroke > 0 ? stroke : 1.0;
    next.fill = fill;
    next.outline = outline;
    next.margin = margin >= 0 ? margin : ML_CURSOR_DEFAULT_MARGIN;
    if (!measure_ink(&next)) {
        ML_ERR("cursor: sprite rasterized to nothing; refusing to track damage for it");
        ml_path_free(&next.path);
        return false;
    }
    next.ready = true;
    ml_path_free(&s->path);
    *s = next;
    return true;
}

ml_rect ml_cursor_sprite_ink(const ml_cursor_sprite *s)
{
    if (!s || !s->ready) return ml_rect_make(0, 0, 0, 0);
    return s->ink;
}

/* ---------------------------------------------------------------- cursor -- */

static ml_rect damage_at(const ml_cursor *c, int x, int y)
{
    const ml_cursor_sprite *s = &c->sprite;
    int m = s->margin;
    ml_rect r = ml_rect_make(x + s->ink.x - m, y + s->ink.y - m,
                             s->ink.w + 2 * m, s->ink.h + 2 * m);
    /* clamp to the screen: a damage rect that leaves the framebuffer makes the
     * renderer sample outside the surface (the 50x60 px tear class of bug) */
    return ml_rect_intersect(r, ml_rect_make(0, 0, c->screen_w, c->screen_h));
}


void ml_cursor_init(ml_cursor *c, const ml_cursor_sprite *sprite, int screen_w, int screen_h)
{
    memset(c, 0, sizeof *c);
    if (sprite && sprite->ready) {
        c->sprite = *sprite;
        /* the sprite owns a heap path: take a private copy so the cursor can
         * outlive (or be re-pointed at) whatever sprite it was built from */
        ml_path_init(&c->sprite.path);
        ml_path_copy(&c->sprite.path, &sprite->path);
    } else {
        ml_cursor_sprite_default(&c->sprite);
    }
    c->screen_w = screen_w > 0 ? screen_w : 1;
    c->screen_h = screen_h > 0 ? screen_h : 1;
    c->visible = true;
    c->x = c->y = 0;
    c->has_last = true;
    c->last_damage = damage_at(c, 0, 0);
}

void ml_cursor_free(ml_cursor *c)
{
    if (!c) return;
    ml_path_free(&c->sprite.path);
    memset(c, 0, sizeof *c);
}

void ml_cursor_set_screen(ml_cursor *c, int screen_w, int screen_h)
{
    if (!c) return;
    c->screen_w = screen_w > 0 ? screen_w : 1;
    c->screen_h = screen_h > 0 ? screen_h : 1;
    c->x = ML_CLAMP(c->x, 0, c->screen_w - 1);
    c->y = ML_CLAMP(c->y, 0, c->screen_h - 1);
    /* the recorded damage was computed against the old geometry: recompute it
     * against the new one so the next move still invalidates the old position */
    c->last_damage = damage_at(c, c->x, c->y);
    c->has_last = true;
}

void ml_cursor_warp(ml_cursor *c, int x, int y)
{
    if (!c) return;
    c->x = ML_CLAMP(x, 0, c->screen_w - 1);
    c->y = ML_CLAMP(y, 0, c->screen_h - 1);
    c->last_damage = damage_at(c, c->x, c->y);
    c->has_last = true;
}

bool ml_cursor_move(ml_cursor *c, int x, int y, ml_rect *old_dmg, ml_rect *new_dmg)
{
    if (!c || !c->sprite.ready) return false;
    int nx = ML_CLAMP(x, 0, c->screen_w - 1);
    int ny = ML_CLAMP(y, 0, c->screen_h - 1);
    if (nx == c->x && ny == c->y) return false;

    /* The old damage rect is derived from the position the sprite is leaving,
     * which is exactly where the framebuffer still shows it. It is recorded
     * unconditionally: a caller that is handed only the new rect would repaint
     * the background under the new cursor and leave the old one behind. */
    ml_rect prev = damage_at(c, c->x, c->y);
    c->x = nx;
    c->y = ny;
    ml_rect now = damage_at(c, c->x, c->y);
    c->last_damage = now;
    c->has_last = true;
    c->moves++;

    if (old_dmg) *old_dmg = prev;
    if (new_dmg) *new_dmg = now;
    return true;
}

ml_rect ml_cursor_damage(const ml_cursor *c)
{
    if (!c || !c->sprite.ready) return ml_rect_make(0, 0, 0, 0);
    return damage_at(c, c->x, c->y);
}

ml_rect ml_cursor_damage_at(const ml_cursor *c, int x, int y)
{
    if (!c || !c->sprite.ready) return ml_rect_make(0, 0, 0, 0);
    return damage_at(c, ML_CLAMP(x, 0, c->screen_w - 1), ML_CLAMP(y, 0, c->screen_h - 1));
}

bool ml_cursor_damage_covers_ink(const ml_cursor *c, ml_rect r)
{
    if (!c || !c->sprite.ready) return false;
    const ml_cursor_sprite *s = &c->sprite;
    ml_rect ink = ml_rect_make(c->x + s->ink.x, c->y + s->ink.y, s->ink.w, s->ink.h);
    /* Ink that falls outside the screen is never rasterized (every primitive
     * clips to the surface), so the damage rect only has to cover the part of
     * the ink that is actually on screen. */
    ink = ml_rect_intersect(ink, ml_rect_make(0, 0, c->screen_w, c->screen_h));
    if (ml_rect_empty(ink)) return true;
    return ml_rect_contains_rect(r, ink);
}

void ml_cursor_paint(ml_ctx *ctx, const ml_cursor *c, ml_rect clip)
{
    if (!ctx || !c || !c->sprite.ready || !c->visible) return;
    /* cheap reject first: the sprite can only touch its own damage box */
    ml_rect reach = ml_cursor_damage(c);
    if (ml_rect_empty(ml_rect_intersect(reach, clip))) return;
    ml_draw_path_fill(ctx, &c->sprite.path, c->sprite.fill, c->x, c->y, 1.0);
    ml_draw_path_stroke(ctx, &c->sprite.path, c->sprite.stroke, c->sprite.outline,
                        c->x, c->y, 1.0);
}
