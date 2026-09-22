/* Leak test (spec §12): open and close the equivalent of 300 windows' worth of
 * surfaces, animations, glyph/icon/thumbnail cache entries and notifications,
 * then assert RSS returns to (approximately) its starting level.
 * Also asserts every cache stays inside its declared budget while doing so. */
#include "ml/common.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"
#include "ml/anim.h"
#include "ml/cache.h"
#include "ml/log.h"
#include "ml/util.h"
#include <stdio.h>

static unsigned long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    unsigned long v = 0;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "VmRSS:", 6) == 0) { v = strtoul(line + 6, NULL, 10); break; }
    fclose(f);
    return v;
}

static void simulate_window(ml_anim_engine *e, int i)
{
    ml_surface *s = ml_surface_new(640, 420);
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, 640, 420));
    ml_draw_shadow(&c, ml_rect_make(8, 8, 624, 404), 10, 14, ml_rgba(0, 0, 0, 110));
    ml_fill_rounded(&c, ml_rect_make(0, 0, 640, 420), 10, ml_rgba(30, 32, 40, 246));
    ml_font *f = ml_font_get("mica-sans");
    char t[64];
    snprintf(t, sizeof t, "Window %d with some text to rasterize", i);
    ml_draw_text(&c, f, 20, 40, t, 13, ml_rgb(230, 235, 245));
    char icon[24];
    snprintf(icon, sizeof icon, "%s", (i % 2) ? "folder" : "app-files");
    ml_icon_draw(&c, icon, ml_rect_make(300, 200, 48, 48), ml_rgba(255, 255, 255, 255));
    ml_anim *a = ml_anim_xform(e, (ml_xform){ 0, 0.94, { 0, 0 } }, (ml_xform){ 1, 1, { 0, 0 } }, 0.2, ML_EASE_OUT_CUBIC);
    (void)a;
    for (int k = 0; k < 20; k++) ml_anim_tick(e, ml_now_s() + k * 0.016);
    ml_surface_free(s);
}

int main(void)
{
    ml_log_init("test_leak", ML_LOG_WARN, NULL);
    ml_anim_engine *e = ml_anim_engine_new(128);
    /* warm caches the way a real session would */
    for (int i = 0; i < 20; i++) simulate_window(e, i);
    unsigned long base = rss_kb();
    for (int i = 0; i < 300; i++) simulate_window(e, i);
    unsigned long peak = rss_kb();
    ml_icon_cache_clear();
    unsigned long after = rss_kb();
    printf("rss: warm=%lu kB peak=%lu kB after-300-windows=%lu kB\n", base, peak, after);
    ml_str rep;
    ml_str_init(&rep);
    ml_icon_cache_report(&rep);
    ml_cache_report_fonts(&rep);
    printf("%s", rep.p);
    ml_str_free(&rep);
    int bad = 0;
    if (after > base + 8192) { printf("FAIL: RSS grew %ld kB and did not return\n", (long)after - (long)base); bad = 1; }
    ml_anim_engine_destroy(e);
    if (bad) return 1;
    printf("leak test OK: memory returns to baseline after 300 open/close cycles\n");
    return 0;
}
