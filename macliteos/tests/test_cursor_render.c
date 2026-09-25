/* Cursor rendering regression — pixel level.
 *
 * The damage-geometry test (test_cursor_damage.c) proves the compositor asks
 * for the right rectangles. This one proves the *pixels* are right: it runs a
 * miniature compositor that works exactly like the real one (damage region,
 * per-rect scene repaint, one pointer composite per frame) and, after every
 * pointer movement, compares the framebuffer byte for byte against a full
 * repaint of the same scene.
 *
 * A single comparison catches every failure mode at once:
 *   - a cursor left behind at a previous position  -> mismatch
 *   - two cursors on screen                        -> mismatch
 *   - the pointer drawn more than once             -> mismatch
 *   - a previous location only partly restored     -> mismatch
 *   - a damage region that misses part of the ink  -> mismatch
 *
 * It also asserts the positive form (exactly one connected cursor-shaped blob,
 * sitting where the pointer is) so a failure names the symptom.
 */
#include "ml/common.h"
#include "ml/region.h"
#include "ml/cursor.h"
#include "ml/raster.h"
#include "ml/surface.h"
#include "ml/img.h"
#include "ml/log.h"
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do {                                                \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define SIM_W 480
#define SIM_H 360

/* ------------------------------------------------------------ the scene --- */
/* A stand-in for wallpaper + windows + notifications: an opaque base plus a
 * few translucent panels, so the test also catches damage that fails to
 * restore content the pointer was sitting on top of. */
typedef struct {
    ml_surface *scene;      /* what the desktop looks like without a pointer */
    ml_surface *fb;         /* the damage-only framebuffer under test */
    ml_surface *scanout;    /* the KMS/fbdev buffer, as ml_display_commit() leaves it */
    ml_region damage;
    ml_cursor cur;
    int frames;
    int cursor_paints;      /* how many times the pointer was rasterized/frame */
    int commits;
} sim_t;

static void scene_paint(ml_surface *s)
{
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    /* opaque gradient base */
    for (int y = 0; y < s->h; y++) {
        double t = (double)y / (s->h - 1);
        ml_color col = ml_color_mix(ml_rgb(20, 24, 40), ml_rgb(150, 96, 120), t);
        ml_fill_rect(&c, ml_rect_make(0, y, s->w, 1), col);
    }
    /* a translucent panel, like the menu bar */
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, 26), ml_rgba(16, 18, 26, 190));
    /* two windows with shadows, like the shell's clients */
    ml_draw_shadow(&c, ml_rect_make(40, 80, 220, 150), 10, 16, ml_rgba(0, 0, 0, 110));
    ml_fill_rounded(&c, ml_rect_make(40, 80, 220, 150), 10, ml_rgba(38, 41, 54, 240));
    ml_draw_shadow(&c, ml_rect_make(180, 190, 250, 130), 10, 16, ml_rgba(0, 0, 0, 110));
    ml_fill_rounded(&c, ml_rect_make(180, 190, 250, 130), 10, ml_rgba(48, 44, 60, 235));
    /* a notification strip, top right */
    ml_fill_rounded(&c, ml_rect_make(s->w - 150, 34, 140, 70), 12, ml_rgba(44, 47, 60, 236));
    /* a dock strip along the bottom */
    ml_fill_rounded(&c, ml_rect_make(s->w / 2 - 150, s->h - 60, 300, 52), 14, ml_rgba(38, 42, 56, 200));
    ml_surface_make_opaque(s);
}

static void sim_init(sim_t *s)
{
    memset(s, 0, sizeof *s);
    s->scene = ml_surface_new(SIM_W, SIM_H);
    scene_paint(s->scene);
    s->fb = ml_surface_new(SIM_W, SIM_H);
    /* the scanout buffer starts as whatever the firmware left there, so a bug
     * that forgets to restore a pixel cannot hide behind a blank buffer */
    s->scanout = ml_surface_new(SIM_W, SIM_H);
    for (int y = 0; y < SIM_H; y++)
        for (int x = 0; x < SIM_W; x++)
            ml_surface_row(s->scanout, y)[x] = ml_color_u32(ml_rgb(220, 210, 190));
    ml_region_init(&s->damage);
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor_init(&s->cur, &sprite, SIM_W, SIM_H);
    ml_cursor_sprite_free(&sprite);
}

static void sim_free(sim_t *s)
{
    ml_surface_free(s->scene);
    ml_surface_free(s->fb);
    ml_surface_free(s->scanout);
    ml_region_free(&s->damage);
    ml_cursor_free(&s->cur);
}

static void sim_commit(sim_t *s);

/* Where the framebuffer currently shows the pointer, as comp.c tracks it. */
static int g_sim_fb_x, g_sim_fb_y;
static bool g_sim_fb_has_cursor;

/* The compositor's present(): repaint every damage rect, then composite the
 * pointer exactly once over the part of its own damage box that is dirty. */
static void sim_present(sim_t *s)
{
    ml_region dmg;
    ml_region_init(&dmg);
    ml_region_copy(&dmg, &s->damage);
    ml_region_simplify(&dmg);
    ml_region_clear(&s->damage);
    if (ml_region_empty(&dmg)) { ml_region_free(&dmg); return; }
    s->frames++;
    s->cursor_paints = 0;

    for (size_t i = 0; i < ml_region_count(&dmg); i++) {
        ml_rect clip = dmg.r[i];
        ml_ctx c;
        ml_ctx_init(&c, s->fb, clip);
        ml_blit_scrolled(&c, s->scene, 0, 0, 255);
    }
    ml_rect reach = ml_cursor_damage(&s->cur);
    ml_rect area = ml_rect_make(0, 0, 0, 0);
    for (size_t i = 0; i < ml_region_count(&dmg); i++) {
        ml_rect hit = ml_rect_intersect(reach, dmg.r[i]);
        if (!ml_rect_empty(hit)) area = ml_rect_union_bounds(area, hit);
    }
    if (!ml_rect_empty(area)) {
        ml_ctx c;
        ml_ctx_init(&c, s->fb, area);
        ml_cursor_paint(&c, &s->cur, area);
        s->cursor_paints = 1;
    }
    ml_region_free(&dmg);
    g_sim_fb_has_cursor = true;
    g_sim_fb_x = s->cur.x;
    g_sim_fb_y = s->cur.y;
    sim_commit(s);
}

static int surfaces_equal(ml_surface *a, ml_surface *b);

/* hardware/kms.c ml_display_commit(): the whole compositor surface is copied
 * into the scanout buffer, for KMS (Radeon) and fbdev (EFI/Safe Graphics)
 * alike. It is deliberately damage-independent so that a pixel the compositor
 * did not repaint this frame still ends up current on the display. */
static void sim_commit(sim_t *s)
{
    if (!surfaces_equal(s->fb, s->scanout)) s->commits++;
    for (int y = 0; y < SIM_H; y++)
        memcpy(ml_surface_row(s->scanout, y), ml_surface_row(s->fb, y),
               (size_t)SIM_W * sizeof(uint32_t));
    CHECK(surfaces_equal(s->fb, s->scanout),
          "scanout buffer diverged from the compositor framebuffer");
}

/* Move the pointer, invalidating exactly what ml_cursor_move() reports. */
static void sim_move(sim_t *s, int x, int y)
{
    ml_rect old_dmg, new_dmg;
    if (ml_cursor_move(&s->cur, x, y, &old_dmg, &new_dmg)) {
        ml_region_add(&s->damage, old_dmg);
        ml_region_add(&s->damage, new_dmg);
    }
}

/* A plain move. g_sim_fb_* keeps pointing at where the framebuffer *shows*
 * the pointer, which is the position of the last present, not this move. */
static void sim_move_to(sim_t *s, int x, int y)
{
    sim_move(s, x, y);
}

/* Which half of the pointer's motion a caller may forget to invalidate. */
enum { GUARD_OLD_MISSING = 1, GUARD_NEW_MISSING = 2, GUARD_BOTH_MISSING = 3 };

/* present() with the compositor's restore guard. `drop` hands the guard the
 * damage region a buggy caller would have built, by removing every rect that
 * overlaps one of the two pointer rects. The guard must put it back, exactly as
 * compositor/comp.c does, and the frame must still come out correct. */
static void sim_present_ex(sim_t *s, int drop)
{
    ml_region dmg;
    ml_region_init(&dmg);
    ml_region_copy(&dmg, &s->damage);
    ml_region_simplify(&dmg);
    ml_region_clear(&s->damage);

    ml_rect boxes[2];
    int nboxes = 0;
    if (g_sim_fb_has_cursor)
        boxes[nboxes++] = ml_cursor_damage_at(&s->cur, g_sim_fb_x, g_sim_fb_y);
    boxes[nboxes++] = ml_cursor_damage(&s->cur);

    /* the buggy caller's region: forget one (or both) of the pointer rects */
    for (int i = 0; i < nboxes; i++) {
        if (!(drop & (i == 0 ? GUARD_OLD_MISSING : GUARD_NEW_MISSING))) continue;
        ml_region kept;
        ml_region_init(&kept);
        for (size_t k = 0; k < ml_region_count(&dmg); k++)
            if (!ml_rect_intersects(dmg.r[k], boxes[i]))
                ml_region_add(&kept, dmg.r[k]);
        ml_region_free(&dmg);
        dmg = kept;
    }

    /* the guard, mirroring present() in compositor/comp.c */
    for (int i = 0; i < nboxes; i++)
        if (!ml_region_covers_rect(&dmg, boxes[i]))
            ml_region_add(&dmg, boxes[i]);

    /* then the present itself */
    s->frames++;
    s->cursor_paints = 0;
    for (size_t i = 0; i < ml_region_count(&dmg); i++) {
        ml_rect clip = dmg.r[i];
        ml_ctx c;
        ml_ctx_init(&c, s->fb, clip);
        ml_blit_scrolled(&c, s->scene, 0, 0, 255);
    }
    ml_rect reach = ml_cursor_damage(&s->cur);
    ml_rect area = ml_rect_make(0, 0, 0, 0);
    for (size_t i = 0; i < ml_region_count(&dmg); i++) {
        ml_rect hit = ml_rect_intersect(reach, dmg.r[i]);
        if (!ml_rect_empty(hit)) area = ml_rect_union_bounds(area, hit);
    }
    if (!ml_rect_empty(area)) {
        ml_ctx c;
        ml_ctx_init(&c, s->fb, area);
        ml_cursor_paint(&c, &s->cur, area);
        s->cursor_paints = 1;
    }
    ml_region_free(&dmg);
    g_sim_fb_has_cursor = true;
    g_sim_fb_x = s->cur.x;
    g_sim_fb_y = s->cur.y;
    sim_commit(s);
}

static void sim_present(sim_t *s);

/* Reference: a full repaint of the scene, with and without the pointer. */
static void sim_reference(ml_surface *out, const sim_t *s, bool with_cursor)
{
    ml_ctx c;
    ml_ctx_init(&c, out, ml_rect_make(0, 0, out->w, out->h));
    ml_blit_scrolled(&c, s->scene, 0, 0, 255);
    if (with_cursor)
        ml_cursor_paint(&c, &s->cur, ml_rect_make(0, 0, out->w, out->h));
}

static int surfaces_equal(ml_surface *a, ml_surface *b)
{
    if (a->w != b->w || a->h != b->h) return 0;
    for (int y = 0; y < a->h; y++) {
        const uint32_t *ra = ml_surface_row(a, y), *rb = ml_surface_row(b, y);
        if (memcmp(ra, rb, (size_t)a->w * sizeof(uint32_t)) != 0) return 0;
    }
    return 1;
}

/* --------------------------------------------------------- verification --- */

/* Count connected blobs of pixels that differ from the scene-only image and
 * report the one that contains the pointer hotspot. */
static int cursor_blobs(ml_surface *fb, ml_surface *scene_only, ml_rect *found)
{
    static const int dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    int W = fb->w, H = fb->h;
    unsigned char *mark = calloc((size_t)W * H, 1);
    int blobs = 0;
    ml_rect best = ml_rect_make(0, 0, 0, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t idx = (size_t)y * W + x;
            if (mark[idx]) continue;
            if (ml_surface_row(fb, y)[x] == ml_surface_row(scene_only, y)[x]) continue;
            /* flood fill this blob */
            int minx = x, maxx = x, miny = y, maxy = y, n = 0;
            int *stack = malloc(sizeof(int) * (size_t)W * H);
            int sp = 0;
            mark[idx] = 1;
            stack[sp++] = (int)idx;
            while (sp) {
                int cur = stack[--sp];
                int cx = cur % W, cy = cur / W;
                n++;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
                for (int k = 0; k < 8; k++) {
                    int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    size_t nidx = (size_t)ny * W + nx;
                    if (mark[nidx]) continue;
                    if (ml_surface_row(fb, ny)[nx] == ml_surface_row(scene_only, ny)[nx]) continue;
                    mark[nidx] = 1;
                    stack[sp++] = (int)nidx;
                }
            }
            free(stack);
            blobs++;
            /* every differing pixel belongs to the pointer, so the largest
             * blob is the sprite - even when the screen edge clips most of it */
            if (n > 0 && (ml_rect_empty(best) || n > best.w * best.h))
                best = ml_rect_make(minx, miny, maxx - minx + 1, maxy - miny + 1);
        }
    }
    free(mark);
    if (found) *found = best;
    return blobs;
}

/* The one assertion that matters, run after every frame. */
static void verify_frame(sim_t *s, const char *what)
{
    ml_surface *full = ml_surface_new(SIM_W, SIM_H);
    ml_surface *scene_only = ml_surface_new(SIM_W, SIM_H);
    sim_reference(full, s, true);
    sim_reference(scene_only, s, false);

    if (!surfaces_equal(s->fb, full)) {
        ml_rect blob;
        int blobs = cursor_blobs(s->fb, scene_only, &blob);
        CHECK(0, "%s: framebuffer differs from a full repaint (%d cursor blob(s), "
                 "largest at %d,%d %dx%d; pointer at %d,%d)",
              what, blobs, blob.x, blob.y, blob.w, blob.h, s->cur.x, s->cur.y);
        if (getenv("ML_CURSOR_TEST_DUMP")) {
            char path[256];
            snprintf(path, sizeof path, "/tmp/cursor_fail_%s.png", what);
            ml_img_write(path, s->fb);
        }
    } else {
        checks++;
    }
    /* the pointer must be rasterized at most once per frame */
    CHECK(s->cursor_paints <= 1, "%s: the pointer was composited %d times in one frame",
          what, s->cursor_paints);
    /* and exactly one cursor-shaped blob must sit on the pointer */
    ml_rect blob;
    int blobs = cursor_blobs(s->fb, scene_only, &blob);
    CHECK(blobs == 1, "%s: expected exactly one cursor on screen, found %d "
                      "(pointer at %d,%d)", what, blobs, s->cur.x, s->cur.y);
    CHECK(!ml_rect_empty(blob) && ml_rect_contains(blob, s->cur.x, s->cur.y),
          "%s: the single cursor blob must contain the pointer hotspot "
          "(blob %d,%d %dx%d, pointer %d,%d)",
          what, blob.x, blob.y, blob.w, blob.h, s->cur.x, s->cur.y);
    ml_surface_free(full);
    ml_surface_free(scene_only);
}

/* ------------------------------------------------------------- scenarios --- */

static void test_slow_movement(void)
{
    sim_t s;
    sim_init(&s);
    /* first frame: the compositor starts with a full-screen invalidation */
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    verify_frame(&s, "initial");

    const int path[][2] = {
        { 20, 20 }, { 60, 40 }, { 120, 70 }, { 200, 110 }, { 300, 150 },
        { 380, 200 }, { 420, 260 }, { 460, 320 }, { 240, 300 }, { 100, 250 },
        { 40, 180 }, { 220, 60 }, { 330, 90 }, { 450, 40 },
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(path); i++) {
        sim_move(&s, path[i][0], path[i][1]);
        sim_present(&s);
        verify_frame(&s, "slow move");
    }
    CHECK(s.frames == ML_ARRAY_SIZE(path) + 1, "one frame per move");
    sim_free(&s);
}

static void test_rapid_movement(void)
{
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);

    /* Many moves between two presents: exactly what a fast physical mouse or a
     * burst of coalesced evdev events produces. */
    for (int i = 0; i < 150; i++) {
        int x = 10 + (i * 11) % (SIM_W - 20);
        int y = 10 + (i * 7) % (SIM_H - 20);
        sim_move(&s, x, y);
        if (i % 25 == 24) { sim_present(&s); verify_frame(&s, "rapid burst"); }
    }
    sim_present(&s);
    verify_frame(&s, "rapid final");
    sim_free(&s);
}

static void test_edges_and_corners(void)
{
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);

    const int pts[][2] = {
        { 0, 0 }, { SIM_W - 1, 0 }, { 0, SIM_H - 1 }, { SIM_W - 1, SIM_H - 1 },
        { 0, SIM_H / 2 }, { SIM_W - 1, SIM_H / 2 }, { SIM_W / 2, 0 }, { SIM_W / 2, SIM_H - 1 },
        { 1, 1 }, { SIM_W - 2, SIM_H - 2 }, { 2, SIM_H - 3 }, { SIM_W - 3, 2 },
        /* out of range: must clamp, not escape */
        { -40, -40 }, { SIM_W + 40, SIM_H + 40 }, { -1, 100 }, { 100, -1 },
        { SIM_W, 100 }, { 100, SIM_H },
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(pts); i++) {
        sim_move(&s, pts[i][0], pts[i][1]);
        sim_present(&s);
        verify_frame(&s, "edge/corner");
        CHECK(s.cur.x >= 0 && s.cur.x < SIM_W && s.cur.y >= 0 && s.cur.y < SIM_H,
              "edge/corner: pointer must stay on screen, got (%d,%d)", s.cur.x, s.cur.y);
    }
    sim_free(&s);
}

static void test_overlapping_ui(void)
{
    /* Park the pointer on top of every piece of UI in the scene in turn and
     * walk it across them: the pointer must be restored from the scene, not
     * from whatever the framebuffer happened to hold. */
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);

    const ml_rect hotspots[] = {
        ml_rect_make(0, 0, SIM_W, 26),            /* menu bar */
        ml_rect_make(40, 80, 220, 150),           /* window 1 */
        ml_rect_make(180, 190, 250, 130),         /* window 2 (overlapping) */
        ml_rect_make(SIM_W - 150, 34, 140, 70),   /* notification */
        ml_rect_make(SIM_W / 2 - 150, SIM_H - 60, 300, 52), /* dock */
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(hotspots); i++) {
        ml_rect r = hotspots[i];
        for (int step = 0; step <= 6; step++) {
            int x = r.x + (r.w * step) / 6;
            int y = r.y + r.h / 2;
            sim_move(&s, x, y);
            sim_present(&s);
            verify_frame(&s, "overlapping UI");
        }
    }
    sim_free(&s);
}

static void test_repeated_redraws_are_idempotent(void)
{
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    sim_move(&s, 200, 150);
    sim_present(&s);
    verify_frame(&s, "before repeats");

    /* Repainting the same damage over and over must not change a pixel: the
     * scene base is opaque, so every repaint starts from the same bytes. */
    for (int i = 0; i < 6; i++) {
        sim_present(&s);
        verify_frame(&s, "repeat");
    }
    /* and a pointer that does not move must not smear either */
    for (int i = 0; i < 4; i++) {
        ml_region_add(&s.damage, ml_rect_make(60, 60, 200, 120));
        sim_present(&s);
        verify_frame(&s, "idle pointer repaint");
    }
    sim_free(&s);
}

static void test_scene_change_under_pointer(void)
{
    /* A window moves while the pointer sits on it: the pointer must be
     * re-composited from the new scene, not left as a ghost of the old one. */
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    sim_move(&s, 150, 150);
    sim_present(&s);
    verify_frame(&s, "scene: before");

    for (int i = 1; i <= 8; i++) {
        /* shift the whole scene, as a workspace slide or a wallpaper change */
        ml_ctx c;
        ml_ctx_init(&c, s.scene, ml_rect_make(0, 0, SIM_W, SIM_H));
        ml_fill_rect(&c, ml_rect_make(0, 0, SIM_W, SIM_H),
                     ml_rgba(20 + i * 8, 24 + i * 4, 40 + i * 6, 255));
        ml_surface_make_opaque(s.scene);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        verify_frame(&s, "scene change");
        sim_move(&s, 150 + i * 20, 150 - i * 10);
        sim_present(&s);
        verify_frame(&s, "scene change + move");
    }
    sim_free(&s);
}

static void test_damage_region_covers_pointer(void)
{
    /* The compositor-side guard: whatever else a frame repaints, it must
     * repaint every pixel the framebuffer's pointer currently occupies. */
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    int on_x = s.cur.x, on_y = s.cur.y;
    for (int i = 0; i < 60; i++) {
        int nx = (i * 29) % SIM_W, ny = (i * 17) % SIM_H;
        sim_move(&s, nx, ny);
        if (nx == on_x && ny == on_y) { sim_present(&s); on_x = s.cur.x; on_y = s.cur.y;
                                        verify_frame(&s, "guard"); continue; }
        ml_region dmg;
        ml_region_init(&dmg);
        ml_region_copy(&dmg, &s.damage);
        ml_region_simplify(&dmg);
        ml_rect onscreen = ml_cursor_damage_at(&s.cur, on_x, on_y);
        CHECK(ml_region_covers_rect(&dmg, onscreen),
              "frame %d: damage must cover the on-screen pointer at (%d,%d)",
              i, on_x, on_y);
        ml_region_free(&dmg);
        sim_present(&s);
        on_x = s.cur.x;
        on_y = s.cur.y;
        verify_frame(&s, "guard");
    }
    sim_free(&s);
}

static void test_cursor_painted_once_per_frame(void)
{
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    CHECK(s.cursor_paints == 1, "the first frame must draw the pointer once");
    for (int i = 0; i < 40; i++) {
        sim_move(&s, 20 + (i * 13) % 400, 20 + (i * 9) % 300);
        sim_present(&s);
        CHECK(s.cursor_paints <= 1,
              "frame %d composited the pointer %d times", i, s.cursor_paints);
    }
    sim_free(&s);
}

/* Both display backends funnel through ml_display_commit(), which copies the
 * whole compositor surface into the scanout buffer. That is what makes the
 * fix hold identically for KMS ("normal") and fbdev/EFI ("Safe Graphics"): the
 * scanout buffer is a pure function of C.fb, so if C.fb is right after every
 * incremental present, the display is right too. This drives the same moves
 * through both models. */
static void test_scanout_matches_framebuffer(void)
{
    sim_t s;
    sim_init(&s);
    ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
    sim_present(&s);
    sim_commit(&s);

    for (int i = 0; i < 90; i++) {
        int x = (i % 2) ? (i * 23) % SIM_W : SIM_W - 1 - ((i * 19) % SIM_W);
        int y = (i % 3) ? (i * 17) % SIM_H : SIM_H - 1 - ((i * 13) % SIM_H);
        sim_move(&s, x, y);
        sim_present(&s);
        /* the scanout buffer is what the user sees; it must equal the
         * framebuffer after every commit, in both display modes */
        CHECK(surfaces_equal(s.fb, s.scanout),
              "scanout (frame %d) does not match the compositor framebuffer", i);
    }
    CHECK(s.commits > 0, "the commits were actually exercised");
    sim_free(&s);
}

/* The compositor's guard is what makes the "one cursor, every old position
 * restored" guarantee structural. These are the two ways a caller can get the
 * damage wrong, and each must still end in a correct frame. */
static void test_guard_repairs_a_wrong_damage_region(void)
{
    /* (a) the old position is not invalidated -> the previous cursor survives */
    {
        sim_t s;
        sim_init(&s);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        sim_move_to(&s, 100, 100);
        sim_present_ex(&s, GUARD_OLD_MISSING);
        verify_frame(&s, "guard: old rect missing");
        sim_free(&s);
    }
    /* (b) the new position is not invalidated -> the frame erases the pointer */
    {
        sim_t s;
        sim_init(&s);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        sim_move_to(&s, 100, 100);
        sim_present_ex(&s, GUARD_NEW_MISSING);
        verify_frame(&s, "guard: new rect missing");
        sim_free(&s);
    }
    /* (c) both wrong: the region does not touch the pointer at all */
    {
        sim_t s;
        sim_init(&s);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        sim_move_to(&s, 100, 100);
        sim_present_ex(&s, GUARD_BOTH_MISSING);
        verify_frame(&s, "guard: both rects missing");
        sim_free(&s);
    }
    /* (d) a whole-screen repaint is always fine, guard or not */
    {
        sim_t s;
        sim_init(&s);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        sim_move_to(&s, 100, 100);
        ml_region_add(&s.damage, ml_rect_make(0, 0, SIM_W, SIM_H));
        sim_present(&s);
        verify_frame(&s, "guard: full repaint");
        sim_free(&s);
    }
}

int main(void)
{
    ml_log_init("test_cursor_render", ML_LOG_WARN, NULL);
    test_slow_movement();
    test_rapid_movement();
    test_edges_and_corners();
    test_overlapping_ui();
    test_repeated_redraws_are_idempotent();
    test_scene_change_under_pointer();
    test_damage_region_covers_pointer();
    test_cursor_painted_once_per_frame();
    test_scanout_matches_framebuffer();
    test_guard_repairs_a_wrong_damage_region();

    if (failures) {
        printf("FAIL: %d of %d cursor rendering checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: %d cursor rendering checks (slow, rapid, edges, overlapping UI, "
           "repeats, scene changes, single-composite, scanout, restore guard)\n",
           checks);
    return 0;
}
