#ifndef ML_CURSOR_H
#define ML_CURSOR_H
#include "ml/common.h"
#include "ml/region.h"
#include "ml/surface.h"
#include "ml/raster.h"

/* The pointer sprite and its damage bookkeeping.
 *
 * Why this is a module and not a #define in the compositor
 * -------------------------------------------------------
 * A damage-only compositor has exactly one hard rule for the pointer:
 *
 *     damage(new position) ∪ damage(old position)  ⊇  ink(sprite)
 *
 * If the damage bounds ever fail to contain the pixels the sprite actually
 * writes, the compositor repaints the *background* under the old position but
 * never the cursor pixels that were left there — and the user sees a trail of
 * stale cursors behind the live one. That is the whole bug class.
 *
 * The bounds used to be a hand-written constant (x-4, y-4, 24, 28) that had no
 * relationship whatsoever to the sprite path that was rasterized two functions
 * away. It happened to be big enough for one specific arrow; nothing enforced
 * it, so any change to the sprite, its hotspot, its outline width, a scale
 * factor or a screen-edge clamp silently reintroduced the trails.
 *
 * This module makes the relationship structural instead of accidental:
 *
 *   1. the sprite (path, hotspot, outline, colours) is parsed and rasterized
 *      *once*, into a scratch surface, at init time;
 *   2. the ink bounding box is *measured* from that rasterization — it is the
 *      set of pixels the sprite can possibly touch, not a guess;
 *   3. the damage rect returned for any position is that measured box plus a
 *      safety margin, clamped to the screen. The invariant therefore holds by
 *      construction for every sprite, every hotspot and every scale;
 *   4. movement returns *both* the old and the new damage rect, and refuses to
 *      report success unless the old one was actually recorded — so a
 *      previous position can never be skipped;
 *   5. painting uses the cached path: no parsing and no allocation in the
 *      render hot path (spec §10).
 *
 * Everything here is pure geometry plus one raster call, so it is unit
 * testable without a compositor, a display or an event loop.
 */

#define ML_CURSOR_DEFAULT_MARGIN 2   /* px of slack around the measured ink */

/* ---------------------------------------------------------------- sprite -- */
typedef struct {
    ml_path path;            /* parsed once; reused by every paint */
    bool path_ok;
    ml_rect ink;             /* measured ink box, relative to the hotspot */
    double stroke;           /* outline width used when rasterizing */
    ml_color fill, outline;
    int margin;              /* slack added around `ink` in damage rects */
    bool ready;
} ml_cursor_sprite;

/* Zero-initialize a sprite. Every sprite must pass through here (or through
 * ml_cursor_sprite_default / ml_cursor_sprite_set on a zeroed struct) before it
 * is used or freed: the struct owns a heap path and the free path assumes it. */
void ml_cursor_sprite_init(ml_cursor_sprite *s);
/* The stock MacLiteOS arrow. Callers may substitute their own path with
 * ml_cursor_sprite_set(); the damage maths is identical either way. */
void ml_cursor_sprite_default(ml_cursor_sprite *s);
void ml_cursor_sprite_free(ml_cursor_sprite *s);

/* Adopt a custom sprite. `margin` is extra slack around the measured ink
 * (ML_CURSOR_DEFAULT_MARGIN is a sane default). Returns false if the path
 * could not be parsed or rasterized, in which case the sprite stays unusable
 * and must not be painted. */
bool ml_cursor_sprite_set(ml_cursor_sprite *s, const char *path_d, double stroke,
                          ml_color fill, ml_color outline, int margin);

/* Measured ink box (sprite space, hotspot at 0,0) — never empty once ready. */
ml_rect ml_cursor_sprite_ink(const ml_cursor_sprite *s);

/* ---------------------------------------------------------------- cursor -- */
typedef struct {
    ml_cursor_sprite sprite;
    int x, y;                /* hotspot in screen space (always in bounds) */
    int screen_w, screen_h;
    bool visible;
    ml_rect last_damage;     /* damage rect of the last recorded position */
    bool has_last;           /* false until the first move/warp is recorded */
    uint64_t moves;          /* how many times the sprite actually moved */
} ml_cursor;

void ml_cursor_init(ml_cursor *c, const ml_cursor_sprite *sprite, int screen_w, int screen_h);
void ml_cursor_free(ml_cursor *c);

/* Screen geometry changed (mode set / panel resize): re-clamp the hotspot and
 * drop the recorded damage so the next move invalidates both the old and the
 * new position against the new bounds. */
void ml_cursor_set_screen(ml_cursor *c, int screen_w, int screen_h);

/* Place the sprite without producing damage. Only valid before the first
 * frame, or together with a full-screen invalidation by the caller. */
void ml_cursor_warp(ml_cursor *c, int x, int y);

/* Move the sprite. On success (the hotspot really changed) *old_dmg and
 * *new_dmg are both valid rects that fully contain the sprite ink at the
 * previous and the new position respectively, clamped to the screen. On
 * failure (no movement, or a degenerate screen) nothing is written. */
bool ml_cursor_move(ml_cursor *c, int x, int y, ml_rect *old_dmg, ml_rect *new_dmg);

/* Damage rect for the current position: guaranteed to contain the ink and to
 * be clamped to the screen. */
ml_rect ml_cursor_damage(const ml_cursor *c);
/* Same, for an arbitrary position. Used to invalidate where the sprite used to
 * be when the compositor only remembers that position (a screen resize, or the
 * present() guard that repairs an under-reported damage region). */
ml_rect ml_cursor_damage_at(const ml_cursor *c, int x, int y);

/* True when `r` fully covers the sprite ink at the current position. This is
 * the invariant the regression tests assert on; keeping it here means the
 * compositor cannot quietly stop satisfying it. */
bool ml_cursor_damage_covers_ink(const ml_cursor *c, ml_rect r);

/* Paint the sprite into `ctx`, clipped to `clip`. No allocation, no path
 * parsing: the sprite was prepared at init time. */
void ml_cursor_paint(ml_ctx *ctx, const ml_cursor *c, ml_rect clip);

#endif /* ML_CURSOR_H */
