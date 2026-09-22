/* Unit tests for the core invariants the whole OS depends on.
 * Fail loudly; these run in CI-equivalent fashion via `make tests && ./run-tests.sh`. */
#include "ml/common.h"
#include "ml/region.h"
#include "ml/cache.h"
#include "ml/anim.h"
#include "ml/font.h"
#include "ml/raster.h"
#include "ml/surface.h"
#include "ml/img.h"
#include "ml/event.h"
#include "ml/util.h"
#include "ml/log.h"
#include <assert.h>

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } } while (0)

static void test_region(void)
{
    ml_region r;
    ml_region_init(&r);
    ml_region_add(&r, ml_rect_make(0, 0, 100, 100));
    ml_region_add(&r, ml_rect_make(0, 100, 100, 100));
    ml_region_simplify(&r);
    CHECK(ml_region_count(&r) == 1, "vertically adjacent rects merge");
    CHECK(ml_rect_eq(ml_region_bounds(&r), ml_rect_make(0, 0, 100, 200)), "bounds after merge");

    ml_region_clear(&r);
    ml_region_add(&r, ml_rect_make(0, 0, 100, 100));
    ml_region_subtract(&r, ml_rect_make(25, 25, 50, 50));
    CHECK(ml_region_count(&r) == 4, "hole cut produces 4 pieces");
    CHECK(!ml_region_contains(&r, 50, 50), "hole not contained");
    CHECK(ml_region_contains(&r, 10, 50), "left band contained");

    ml_region_clear(&r);
    ml_region_add(&r, ml_rect_make(10, 10, 40, 40));
    ml_region_intersect(&r, ml_rect_make(30, 30, 40, 40));
    CHECK(ml_rect_eq(ml_region_bounds(&r), ml_rect_make(30, 30, 20, 20)), "intersect clips");
    ml_region_free(&r);
}

static void dummy_free(void *v) { *(int *)v = -1; ml_free(v); }
static void test_cache_budget(void)
{
    ml_cache *c = ml_cache_new(4096, 100, dummy_free);
    int total = 0;
    for (int i = 0; i < 200; i++) {
        int *v = ml_zalloc(512);
        *v = i;
        char k[16];
        snprintf(k, sizeof k, "%d", i);
        ml_cache_put(c, k, v, 512);
        total++;
    }
    (void)total;
    CHECK(ml_cache_bytes(c) <= 4096, "cache never exceeds byte budget");
    CHECK(ml_cache_count(c) <= 8, "cache evicts to fit budget");
    int *hit = ml_cache_get(c, "199");
    CHECK(hit && *hit == 199, "MRU entry survives");
    CHECK(ml_cache_get(c, "0") == NULL, "LRU entry evicted");
    ml_cache_destroy(c);
}

static void test_easing(void)
{
    CHECK(fabs(ml_ease_apply(ML_EASE_OUT_CUBIC, 0)) < 1e-9, "ease starts at 0");
    CHECK(fabs(ml_ease_apply(ML_EASE_OUT_CUBIC, 1) - 1) < 1e-9, "ease ends at 1");
    CHECK(ml_ease_apply(ML_EASE_OUT_CUBIC, 0.5) > 0.5, "out-ease is ahead of linear");
    CHECK(fabs(ml_ease_apply(ML_EASE_IN_OUT_CUBIC, 0.5) - 0.5) < 1e-9, "in-out symmetric");
}

static void test_spring_interrupt(void)
{
    ml_anim_engine *e = ml_anim_engine_new(32);
    ml_anim *a = ml_anim_spring(e, 0, 100, 260, 26, 1, 0);
    double t = ml_now_s();
    for (int i = 0; i < 20; i++) { t += 1.0 / 60; ml_anim_tick(e, t); }
    double v1 = ml_anim_value(a);
    double vel1 = ml_anim_velocity(a);
    CHECK(v1 > 50 && v1 < 115, "spring moves towards target (may slightly overshoot)");
    /* retarget mid-flight: value continuity is the interruptibility contract */
    ml_anim_retarget(a, 0);
    double v2 = ml_anim_value(a);
    CHECK(fabs(v2 - v1) < 1e-6, "retarget preserves current value");
    (void)vel1;
    for (int i = 0; i < 400; i++) { t += 1.0 / 60; ml_anim_tick(e, t); }
    CHECK(!ml_anim_active(a), "retargeted spring settles");
    CHECK(fabs(ml_anim_value(a)) < 0.5, "settles at new target");
    CHECK(ml_anim_active_count(e) == 0, "engine fully idle after settle (loop may sleep)");
    ml_anim_engine_destroy(e);
}

static void test_font(void)
{
    ml_font *f = ml_font_get("mica-sans");
    CHECK(ml_text_width(f, "MM", 13) > ml_text_width(f, "M", 13), "width grows with text");
    CHECK(ml_text_width(f, "", 13) == 0, "empty string zero width");
    int covered = 0;
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) if (ml_font_glyph(f, cp, 14)) covered++;
    CHECK(covered == 26, "lowercase coverage");
    covered = 0;
    for (uint32_t cp = '0'; cp <= '9'; cp++) if (ml_font_glyph(f, cp, 14)) covered++;
    CHECK(covered == 10, "digit coverage");
    CHECK(ml_font_glyph(f, 'M', 14) != NULL, "glyph rasterizes");
    const char *s = "héllo";
    CHECK(ml_utf8_len(s) == 5, "utf8 length");
}

static void test_png_roundtrip(void)
{
    ml_surface *s = ml_surface_new(37, 23);
    for (int y = 0; y < 23; y++)
        for (int x = 0; x < 37; x++)
            s->px[y * 37 + x] = ml_color_u32(ml_rgba((uint8_t)(x * 7), (uint8_t)(y * 11), 128, 255));
    const char *path = "/tmp/ml_unit_png.png";
    CHECK(ml_img_write_png(path, s), "png writes");
    ml_surface *r = ml_img_read_png(path);
    CHECK(r && r->w == 37 && r->h == 23, "png reads back");
    if (r) {
        int bad = 0;
        for (int i = 0; i < 37 * 23; i++)
            if ((r->px[i] & 0xFFFFFF) != (s->px[i] & 0xFFFFFF)) bad++;
        CHECK(bad == 0, "png pixels lossless");
        ml_surface_free(r);
    }
    ml_surface_free(s);
    unlink(path);
}

static void test_raster_damage(void)
{
    ml_surface *s = ml_surface_new(200, 100);
    ml_surface_damage_all(s);
    ml_region r;
    ml_region_init(&r);
    CHECK(ml_surface_take_damage(s, &r), "damage reported once");
    CHECK(!ml_surface_take_damage(s, &r), "no damage second time (event-driven)");
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(10, 10, 20, 20));
    ml_fill_rect(&c, ml_rect_make(0, 0, 200, 100), ml_rgb(1, 2, 3));
    CHECK(ml_surface_take_damage(s, &r), "draw re-damages clipped area");
    CHECK(ml_rect_eq(ml_region_bounds(&r), ml_rect_make(10, 10, 20, 20)), "damage equals clip");
    ml_region_free(&r);
    ml_surface_free(s);
}

static void timer_hit(void *ud) { (*(int *)ud)++; }
static void test_loop_idle(void)
{
    ml_loop *l = ml_loop_new();
    int hits = 0;
    ml_loop_add_timer(l, 30, false, timer_hit, &hits);
    uint64_t w0 = ml_loop_wakeups(l);
    ml_loop_run_for(l, 120);
    CHECK(hits == 1, "one-shot timer fires exactly once");
    uint64_t w1 = ml_loop_wakeups(l);
    /* after the timer, with nothing armed, the loop must not spin */
    ml_loop_run_for(l, 200);
    uint64_t w2 = ml_loop_wakeups(l);
    (void)w0; (void)w1;
    CHECK(w2 - w1 <= 3, "idle loop produces ~no wakeups (spec §13)");
    ml_loop_destroy(l);
}

int main(void)
{
    ml_log_init("test_units", ML_LOG_ERROR, NULL);
    test_region();
    test_cache_budget();
    test_easing();
    test_spring_interrupt();
    test_font();
    test_png_roundtrip();
    test_raster_damage();
    test_loop_idle();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all unit tests passed\n");
    return 0;
}
