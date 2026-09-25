/* Cursor damage geometry — the invariants a damage-only compositor depends on.
 *
 * A compositor that repaints only the dirty rectangles has exactly one hard
 * requirement for the pointer:
 *
 *     damage(new) ∪ damage(old)  ⊇  ink(sprite)
 *
 * Break it and the framebuffer keeps the cursor pixels from the previous
 * position: the user sees a trail of stale arrows behind the live one. These
 * tests pin the requirement down for every position class the pointer can be
 * in — interior, edges, corners, off-screen clamps, rapid bursts — and for the
 * region algebra the compositor uses to decide what to repaint.
 */
#include "ml/common.h"
#include "ml/region.h"
#include "ml/cursor.h"
#include "ml/raster.h"
#include "ml/surface.h"
#include <stdio.h>

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

/* Rasterize the sprite at (x, y) over a known background and return the exact
 * bounding box of the pixels it changed. This is ground truth: it is what the
 * rasterizer actually wrote, not what anybody hoped it would write. */
#define SCRATCH_HOT_X 48
#define SCRATCH_HOT_Y 48

/* Rasterize the sprite at the scratch surface's hotspot over a known background
 * and return the exact bounding box of the pixels it changed, expressed
 * relative to the hotspot. This is ground truth: it is what the rasterizer
 * actually wrote, not what anybody hoped it would write. */
static ml_rect measured_ink(const ml_cursor *c, ml_surface *scratch, uint32_t background)
{
    for (int yy = 0; yy < scratch->h; yy++)
        for (int xx = 0; xx < scratch->w; xx++)
            ml_surface_row(scratch, yy)[xx] = background;
    ml_cursor at = *c;
    at.x = SCRATCH_HOT_X;
    at.y = SCRATCH_HOT_Y;
    ml_ctx ctx;
    ml_ctx_init(&ctx, scratch, ml_rect_make(0, 0, scratch->w, scratch->h));
    ml_cursor_paint(&ctx, &at, ml_rect_make(0, 0, scratch->w, scratch->h));

    ml_rect ink = ml_rect_make(0, 0, 0, 0);
    for (int yy = 0; yy < scratch->h; yy++)
        for (int xx = 0; xx < scratch->w; xx++)
            if (ml_surface_row(scratch, yy)[xx] != background)
                ink = ml_rect_union_bounds(ink, ml_rect_make(xx, yy, 1, 1));
    if (ml_rect_empty(ink)) return ink;
    return ml_rect_make(ink.x - SCRATCH_HOT_X, ink.y - SCRATCH_HOT_Y, ink.w, ink.h);
}

static const int W = 320, H = 240;

static void fill_positions(int (*out)[2], int *n, int max)
{
    int k = 0;
    int pts[][2] = {
        { 0, 0 }, { W - 1, 0 }, { 0, H - 1 }, { W - 1, H - 1 },   /* corners */
        { 0, H / 2 }, { W - 1, H / 2 }, { W / 2, 0 }, { W / 2, H - 1 },
        { 1, 1 }, { W - 2, H - 2 }, { 2, H - 3 }, { W - 3, 2 },
        { 40, 30 }, { 160, 120 }, { 300, 200 }, { 5, 235 },
        { -50, -50 }, { W + 50, H + 50 }, { -1, H / 2 }, { W / 2, -1 }, /* clamp */
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(pts) && k < max; i++) {
        out[k][0] = pts[i][0];
        out[k][1] = pts[i][1];
        k++;
    }
    *n = k;
}

static void test_damage_contains_ink(void)
{
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    CHECK(sprite.ready, "default sprite must be usable");
    CHECK(sprite.ink.w > 0 && sprite.ink.h > 0, "sprite ink must be measured (got %dx%d)",
          sprite.ink.w, sprite.ink.h);

    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);

    int pts[64][2], n = 0;
    fill_positions(pts, &n, 64);

    ml_surface *scratch = ml_surface_new(96, 96);
    const uint32_t bg = ml_color_u32(ml_rgb(11, 22, 33));

    for (int i = 0; i < n; i++) {
        int px = ML_CLAMP(pts[i][0], 0, W - 1);
        int py = ML_CLAMP(pts[i][1], 0, H - 1);
        ml_cursor_warp(&cur, pts[i][0], pts[i][1]);

        ml_rect dmg = ml_cursor_damage(&cur);
        CHECK(!ml_rect_empty(dmg), "damage must never be empty at (%d,%d)", px, py);
        CHECK(dmg.x >= 0 && dmg.y >= 0 &&
              ml_rect_right(dmg) <= W && ml_rect_bottom(dmg) <= H,
              "damage %d,%d %dx%d escapes the screen at (%d,%d)",
              dmg.x, dmg.y, dmg.w, dmg.h, px, py);
        CHECK(ml_cursor_damage_covers_ink(&cur, dmg),
              "damage %d,%d %dx%d does not cover the sprite ink at (%d,%d)",
              dmg.x, dmg.y, dmg.w, dmg.h, px, py);

        /* ground truth: what the rasterizer really wrote */
        ml_rect ink = measured_ink(&cur, scratch, bg);
        ml_rect ink_screen = ml_rect_intersect(
            ml_rect_make(px + ink.x, py + ink.y, ink.w, ink.h),
            ml_rect_make(0, 0, W, H));
        CHECK(ml_rect_contains_rect(dmg, ink_screen),
              "damage %d,%d %dx%d does not cover measured ink %d,%d %dx%d at (%d,%d)",
              dmg.x, dmg.y, dmg.w, dmg.h, ink_screen.x, ink_screen.y,
              ink_screen.w, ink_screen.h, px, py);
    }
    ml_surface_free(scratch);
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_move_reports_both_sides(void)
{
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);

    struct { int x, y; } path[] = {
        { 10, 10 }, { 60, 40 }, { 120, 90 }, { 200, 150 }, { 300, 220 },
        { 40, 200 }, { 5, 5 }, { 315, 235 }, { 160, 120 }, { 10, 10 },
    };
    int prev_x = 0, prev_y = 0;
    for (size_t i = 0; i < ML_ARRAY_SIZE(path); i++) {
        ml_rect old_dmg, new_dmg;
        bool moved = ml_cursor_move(&cur, path[i].x, path[i].y, &old_dmg, &new_dmg);
        if (path[i].x == prev_x && path[i].y == prev_y) {
            CHECK(!moved, "a move to the same position must report no movement");
            continue;
        }
        CHECK(moved, "move %zu to (%d,%d) must report movement",
              i, path[i].x, path[i].y);
        CHECK(!ml_rect_empty(old_dmg) && !ml_rect_empty(new_dmg),
              "move %zu must return two usable rects", i);
        /* the old rect must cover where the sprite *was* */
        ml_cursor probe = cur;
        probe.x = prev_x; probe.y = prev_y;
        CHECK(ml_cursor_damage_covers_ink(&probe, old_dmg),
              "move %zu: old damage %d,%d %dx%d misses the previous ink at (%d,%d)",
              i, old_dmg.x, old_dmg.y, old_dmg.w, old_dmg.h, prev_x, prev_y);
        CHECK(ml_cursor_damage_covers_ink(&cur, new_dmg),
              "move %zu: new damage misses the new ink", i);
        CHECK(ml_rect_eq(old_dmg, ml_cursor_damage_at(&cur, prev_x, prev_y)),
              "move %zu: old damage must equal the damage of the previous position", i);
        prev_x = cur.x; prev_y = cur.y;
    }
    CHECK(cur.moves > 0, "moves must be counted");
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_off_screen_clamping(void)
{
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);

    struct { int in_x, in_y, out_x, out_y; } cases[] = {
        { -100, -100, 0, 0 }, { 10000, 10000, W - 1, H - 1 },
        { -1, 120, 0, 120 }, { 160, -1, 160, 0 },
        { W, H, W - 1, H - 1 }, { 0, 0, 0, 0 },
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(cases); i++) {
        ml_rect o, nw;
        ml_cursor_warp(&cur, W / 2, H / 2);
        bool moved = ml_cursor_move(&cur, cases[i].in_x, cases[i].in_y, &o, &nw);
        CHECK(moved, "case %zu must move", i);
        CHECK(cur.x == cases[i].out_x && cur.y == cases[i].out_y,
              "case %zu: (%d,%d) must clamp to (%d,%d), got (%d,%d)",
              i, cases[i].in_x, cases[i].in_y, cases[i].out_x, cases[i].out_y, cur.x, cur.y);
        CHECK(ml_cursor_damage_covers_ink(&cur, nw),
              "case %zu: clamped damage must still cover the ink", i);
    }
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_screen_resize(void)
{
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);
    ml_cursor_warp(&cur, 300, 200);
    ml_cursor_set_screen(&cur, 64, 48);
    CHECK(cur.x == 63 && cur.y == 47, "a shrink must re-clamp the hotspot, got (%d,%d)",
          cur.x, cur.y);
    CHECK(ml_cursor_damage_covers_ink(&cur, ml_cursor_damage(&cur)),
          "damage must cover the ink after a resize");
    ml_rect o, nw;
    CHECK(ml_cursor_move(&cur, 10, 10, &o, &nw), "move after resize");
    CHECK(ml_cursor_damage_covers_ink(&cur, nw), "new damage must cover the ink");
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_custom_sprite_damage_tracks_geometry(void)
{
    /* A sprite that is nothing like the default arrow. If the damage bounds
     * were still a hand-written constant they would not contain this one. */
    static const char *big = "M0,0 L40,0 L40,40 L0,40 Z";
    ml_cursor_sprite sprite;
    ml_cursor_sprite_init(&sprite);
    CHECK(ml_cursor_sprite_set(&sprite, big, 3.0, ml_rgb(255, 255, 255),
                               ml_rgba(0, 0, 0, 255), 2),
          "a large custom sprite must be accepted");
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);

    int pts[][2] = { { 100, 100 }, { 0, 0 }, { W - 1, H - 1 }, { 160, 120 } };
    ml_surface *scratch = ml_surface_new(96, 96);
    const uint32_t bg = ml_color_u32(ml_rgb(9, 9, 9));
    for (size_t i = 0; i < ML_ARRAY_SIZE(pts); i++) {
        ml_cursor_warp(&cur, pts[i][0], pts[i][1]);
        ml_rect dmg = ml_cursor_damage(&cur);
        CHECK(ml_cursor_damage_covers_ink(&cur, dmg),
              "custom sprite: damage must cover the ink at (%d,%d)", pts[i][0], pts[i][1]);
        ml_rect ink = measured_ink(&cur, scratch, bg);
        ml_rect ink_screen = ml_rect_intersect(
            ml_rect_make(pts[i][0] + ink.x, pts[i][1] + ink.y, ink.w, ink.h),
            ml_rect_make(0, 0, W, H));
        CHECK(ml_rect_contains_rect(dmg, ink_screen),
              "custom sprite: damage %d,%d %dx%d misses measured ink %d,%d %dx%d",
              dmg.x, dmg.y, dmg.w, dmg.h, ink_screen.x, ink_screen.y,
              ink_screen.w, ink_screen.h);
    }
    ml_surface_free(scratch);
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_region_algebra(void)
{
    /* covering / not covering */
    ml_region g;
    ml_region_init(&g);
    ml_region_add(&g, ml_rect_make(0, 0, 10, 10));
    ml_region_add(&g, ml_rect_make(10, 0, 10, 10));
    ml_region_simplify(&g);
    CHECK(ml_region_count(&g) == 1, "horizontally adjacent rects merge");
    CHECK(ml_region_covers_rect(&g, ml_rect_make(0, 0, 20, 10)), "merged rect covers itself");
    CHECK(!ml_region_covers_rect(&g, ml_rect_make(0, 0, 21, 10)), "one column short is a gap");
    CHECK(!ml_region_covers_rect(&g, ml_rect_make(5, 0, 5, 11)), "one row short is a gap");

    ml_region_clear(&g);
    ml_region_add(&g, ml_rect_make(0, 0, 4, 10));
    ml_region_add(&g, ml_rect_make(8, 0, 4, 10));
    CHECK(!ml_region_covers_rect(&g, ml_rect_make(0, 0, 12, 10)),
          "a hole in the middle must not count as covered");
    CHECK(ml_region_covers_rect(&g, ml_rect_make(0, 0, 4, 10)), "left piece is covered");
    CHECK(ml_region_covers_rect(&g, ml_rect_make(8, 0, 4, 10)), "right piece is covered");

    /* vertical adjacency through a union of rects */
    ml_region_clear(&g);
    ml_region_add(&g, ml_rect_make(0, 0, 10, 5));
    ml_region_add(&g, ml_rect_make(0, 5, 10, 5));
    ml_region_add(&g, ml_rect_make(0, 10, 10, 5));
    ml_region_simplify(&g);
    CHECK(ml_region_count(&g) == 1, "three vertically adjacent rects merge to one");
    CHECK(ml_region_covers_rect(&g, ml_rect_make(0, 0, 10, 15)), "merged column is covered");
    ml_region_free(&g);
}

static void test_simplify_is_complete(void)
{
    /* 120 pointer moves in one frame -> 240 damage rects. The old simplifier
     * merged exactly one pair per pass and stopped after 64 passes, so this
     * left 176 overlapping rects and the cursor was rasterized once per
     * overlapping rect on every frame. */
    ml_region g;
    ml_region_init(&g);
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, 800, 600);
    for (int i = 0; i < 120; i++) {
        int x = 20 + (i * 7) % 700, y = 20 + (i * 5) % 500;
        ml_rect o, nw;
        if (ml_cursor_move(&cur, x, y, &o, &nw)) {
            ml_region_add(&g, o);
            ml_region_add(&g, nw);
        }
    }
    size_t before = ml_region_count(&g);
    CHECK(before > 100, "a rapid burst must produce many damage rects (got %zu)", before);
    ml_region_simplify(&g);
    size_t after = ml_region_count(&g);
    CHECK(after < before / 4, "simplify must collapse a burst (got %zu from %zu)", after, before);

    /* no two rects may still overlap: overlapping clips double-paint, and the
     * pointer's translucent outline would be composited once per clip */
    int overlaps = 0;
    for (size_t i = 0; i < g.n; i++)
        for (size_t j = i + 1; j < g.n; j++)
            if (ml_rect_intersects(g.r[i], g.r[j])) overlaps++;
    CHECK(overlaps == 0, "simplified region must be pairwise disjoint (%d overlaps left)", overlaps);

    /* and the union must still cover every position the pointer occupied */
    for (int i = 0; i < 120; i++) {
        int x = 20 + (i * 7) % 700, y = 20 + (i * 5) % 500;
        ml_rect d = ml_cursor_damage_at(&cur, x, y);
        CHECK(ml_region_covers_rect(&g, d),
              "simplified burst damage must still cover the pointer at (%d,%d)", x, y);
    }
    ml_region_free(&g);
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

static void test_damage_clamped_to_screen(void)
{
    ml_region g;
    ml_region_init(&g);
    ml_cursor_sprite sprite;
    ml_cursor_sprite_default(&sprite);
    ml_cursor cur;
    ml_cursor_init(&cur, &sprite, W, H);
    for (int i = 0; i < 40; i++) {
        ml_rect o, nw;
        if (ml_cursor_move(&cur, (i * 37) % 400 - 40, (i * 23) % 300 - 30, &o, &nw)) {
            CHECK(o.x >= 0 && o.y >= 0 && ml_rect_right(o) <= W && ml_rect_bottom(o) <= H,
                  "old damage must stay on screen");
            CHECK(nw.x >= 0 && nw.y >= 0 && ml_rect_right(nw) <= W && ml_rect_bottom(nw) <= H,
                  "new damage must stay on screen");
            ml_region_add(&g, o);
            ml_region_add(&g, nw);
        }
    }
    ml_region_simplify(&g);
    for (size_t i = 0; i < g.n; i++)
        CHECK(g.r[i].x >= 0 && g.r[i].y >= 0 &&
              ml_rect_right(g.r[i]) <= W && ml_rect_bottom(g.r[i]) <= H,
              "simplified rect %zu escapes the screen", i);
    ml_region_free(&g);
    ml_cursor_free(&cur);
    ml_cursor_sprite_free(&sprite);
}

int main(void)
{ test_damage_contains_ink(); test_move_reports_both_sides(); test_off_screen_clamping(); test_screen_resize(); test_custom_sprite_damage_tracks_geometry(); test_region_algebra(); test_simplify_is_complete(); test_damage_clamped_to_screen();
    test_move_reports_both_sides();
    test_off_screen_clamping();
    test_screen_resize();
    test_custom_sprite_damage_tracks_geometry();
    test_region_algebra();
    test_simplify_is_complete();
    test_damage_clamped_to_screen();

    if (failures) {
        printf("FAIL: %d of %d cursor damage checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: %d cursor damage / region checks\n", checks);
    return 0;
}
