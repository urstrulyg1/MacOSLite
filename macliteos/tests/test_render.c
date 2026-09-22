/* Visual smoke test for the rendering stack.
 * Renders a representative desktop frame (wallpaper, menu bar, a window, the
 * Dock with magnification, a notification) entirely with libmaclite and writes
 * a PNG. This is the earliest honest check that fonts, icons, shadows,
 * translucency and the damage path produce something a human would accept.
 *
 * Usage: test_render [out.png]
 */
#include "ml/common.h"
#include "ml/log.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"
#include "ml/img.h"
#include "ml/region.h"
#include "ml/util.h"
#include <math.h>

static void wallpaper(ml_ctx *c, int w, int h)
{
    /* Original gradient wallpaper: dusk over water. Vertical gradient plus a
     * soft horizon glow, no animated layers (spec §18). */
    ml_color top = ml_rgb(24, 27, 44);
    ml_color mid = ml_rgb(70, 74, 128);
    ml_color low = ml_rgb(206, 122, 96);
    for (int y = 0; y < h; y++) {
        double t = (double)y / (h - 1);
        ml_color col;
        if (t < 0.62) col = ml_color_mix(top, mid, t / 0.62);
        else col = ml_color_mix(mid, low, (t - 0.62) / 0.38);
        ml_fill_rect(c, ml_rect_make(0, y, w, 1), col);
    }
    /* horizon glow: analytic radial falloff, blended once into the wallpaper */
    ml_pointd cp = { w * 0.5, h * 0.68 };
    ml_fill_radial_glow(c, cp, w * 0.55, h * 0.30, ml_rgba(255, 176, 128, 90));
    ml_pointd cp2 = { w * 0.30, h * 0.20 };
    ml_fill_radial_glow(c, cp2, w * 0.45, h * 0.35, ml_rgba(96, 110, 200, 40));
}

static void traffic_lights(ml_ctx *c, int x, int y, int d)
{
    ml_color cols[3] = { ml_rgb(236, 106, 96), ml_rgb(240, 189, 84), ml_rgb(120, 200, 130) };
    for (int i = 0; i < 3; i++) {
        ml_rect r = ml_rect_make(x + i * (d + 8), y, d, d);
        ml_fill_rounded(c, r, d / 2.0, cols[i]);
        ml_stroke_rounded(c, r, d / 2.0, 1.0, ml_rgba(0, 0, 0, 40));
    }
}

static void window(ml_ctx *c, ml_rect r, const char *title)
{
    ml_draw_shadow(c, r, 12, 22, ml_rgba(0, 0, 0, 150));
    ml_fill_rounded(c, r, 12, ml_rgba(28, 30, 38, 246));
    ml_rect tb = ml_rect_make(r.x, r.y, r.w, 40);
    ml_fill_rounded(c, tb, 12, ml_rgba(46, 49, 60, 255));
    ml_fill_rect(c, ml_rect_make(r.x, r.y + 24, r.w, 16), ml_rgba(46, 49, 60, 255));
    ml_fill_rect(c, ml_rect_make(r.x, r.y + 39, r.w, 1), ml_rgba(0, 0, 0, 90));
    traffic_lights(c, r.x + 16, r.y + 14, 12);
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");
    ml_draw_text_box(c, fb, ml_rect_make(r.x + 70, r.y, r.w - 140, 40), title, 14, ml_rgba(232, 236, 245, 235), ML_ALIGN_CENTER);

    /* sidebar */
    ml_rect sb = ml_rect_make(r.x + 1, r.y + 40, 188, r.h - 41);
    ml_fill_rounded(c, sb, 0, ml_rgba(36, 38, 47, 255));
    ml_draw_text(c, f, sb.x + 16, sb.y + 30, "FAVOURITES", 10, ml_rgba(160, 168, 186, 255));
    const char *items[] = { "Air", "Desktop", "Documents", "Downloads", "Music", "Pictures" };
    const char *icons[] = { "drive", "display", "folder", "download", "note", "photo" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(items); i++) {
        int y = sb.y + 44 + (int)i * 30;
        if (i == 2) ml_fill_rounded(c, ml_rect_make(sb.x + 8, y - 12, sb.w - 16, 26), 6, ml_rgba(90, 130, 240, 90));
        ml_icon_draw(c, icons[i], ml_rect_make(sb.x + 16, y - 8, 17, 17), ml_rgba(150, 190, 250, 255));
        ml_draw_text(c, f, sb.x + 42, y + 5, items[i], 13, ml_rgba(226, 231, 242, 255));
    }

    /* icon grid */
    const char *files[] = { "Holiday", "Boot.log", "Setup.dmg", "Notes.txt", "Mix.flac", "Renders" };
    const char *fico[] = { "folder", "file", "drive", "doc-lines", "note", "folder" };
    int gx = r.x + 188 + 24, gy = r.y + 40 + 28;
    for (int i = 0; i < 6; i++) {
        int cx = gx + (i % 3) * 132, cy = gy + (i / 3) * 128;
        ml_icon_draw(c, fico[i], ml_rect_make(cx, cy, 56, 56), ml_rgba(255, 255, 255, 255));
        ml_draw_text_box(c, f, ml_rect_make(cx - 30, cy + 62, 116, 18), files[i], 12,
                         ml_rgba(226, 231, 242, 235), ML_ALIGN_CENTER);
    }
    ml_draw_text(c, f, r.x + 204, r.y + 22, "Documents", 13, ml_rgba(190, 198, 214, 255));
}

static void menu_bar(ml_ctx *c, int w)
{
    ml_fill_rect(c, ml_rect_make(0, 0, w, 26), ml_rgba(16, 18, 26, 190));
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");
    ml_icon_draw(c, "mica-mark", ml_rect_make(12, 4, 18, 18), ml_rgba(238, 242, 252, 255));
    const char *menus[] = { "Files", "Edit", "View", "Go", "Window", "Help" };
    int x = 42;
    for (size_t i = 0; i < ML_ARRAY_SIZE(menus); i++) {
        bool active = i == 0;
        int tw = ml_text_width(fb, menus[i], 13);
        if (active) ml_fill_rounded(c, ml_rect_make(x - 7, 4, tw + 14, 18), 5, ml_rgba(90, 130, 240, 220));
        ml_draw_text(c, active ? fb : f, x, 18, menus[i], 13, ml_rgba(236, 240, 250, 240));
        x += tw + 22;
    }
    /* status side */
    int sx = w - 16;
    const char *clk = "Sun 22 Sep  4:38 PM";
    int cw = ml_text_width(f, clk, 13);
    sx -= cw;
    ml_draw_text(c, f, sx, 18, clk, 13, ml_rgba(236, 240, 250, 235));
    sx -= 28;
    ml_icon_draw(c, "battery", ml_rect_make(sx, 6, 22, 14), ml_rgba(236, 240, 250, 235));
    sx -= 26;
    ml_icon_draw(c, "volume-high", ml_rect_make(sx, 5, 16, 16), ml_rgba(236, 240, 250, 235));
    sx -= 26;
    ml_icon_draw(c, "wifi", ml_rect_make(sx, 5, 16, 16), ml_rgba(236, 240, 250, 235));
    sx -= 24;
    ml_icon_draw(c, "search", ml_rect_make(sx, 5, 15, 15), ml_rgba(236, 240, 250, 220));
}

static void dock(ml_ctx *c, int w, int h, double mouse_x)
{
    const char *icons[] = { "app-files", "app-terminal", "app-settings", "app-sysinfo",
                            "app-image", "app-video", "app-music", "app-pdf",
                            "app-textedit", "app-launcher", "app-trash" };
    int n = (int)ML_ARRAY_SIZE(icons);
    int base = 52;
    double mag = 26.0, sigma = base * 1.5;
    /* gaussian magnification model — the same one the Dock uses at runtime */
    double widths[16];
    double total = 0;
    /* estimate centres from unscaled layout, then apply magnification */
    double x0 = (w - (n * (base + 6))) / 2.0;
    for (int i = 0; i < n; i++) {
        double cx = x0 + i * (base + 6) + base / 2.0;
        double d = fabs(cx - mouse_x);
        widths[i] = base + mag * exp(-(d * d) / (2 * sigma * sigma));
        total += widths[i];
    }
    double panel_w = total + (n - 1) * 6 + 24;
    int px = (int)((w - panel_w) / 2);
    int ph = (int)(base + mag + 26);
    int py = h - ph - 8;
    ml_rect panel = ml_rect_make(px, py, (int)panel_w, ph);
    ml_draw_shadow(c, panel, 20, 18, ml_rgba(0, 0, 0, 120));
    ml_fill_rounded(c, panel, 20, ml_rgba(38, 42, 56, 170));
    ml_stroke_rounded(c, panel, 20, 1.0, ml_rgba(255, 255, 255, 46));

    double cx = px + 12;
    for (int i = 0; i < n; i++) {
        int s = (int)lround(widths[i]);
        int y = py + ph - 18 - s;
        ml_icon_draw(c, icons[i], ml_rect_make((int)lround(cx), y, s, s), ml_rgba(255, 255, 255, 255));
        if (i < 9) {  /* running indicator */
            ml_fill_rounded(c, ml_rect_make((int)lround(cx + s / 2.0) - 2, py + ph - 12, 4, 4), 2,
                            ml_rgba(230, 236, 250, 200));
        }
        cx += s + 6;
    }
}

static void notification(ml_ctx *c, int w)
{
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");
    ml_rect r = ml_rect_make(w - 344, 38, 332, 74);
    ml_draw_shadow(c, r, 14, 16, ml_rgba(0, 0, 0, 120));
    ml_fill_rounded(c, r, 14, ml_rgba(42, 45, 58, 232));
    ml_icon_draw(c, "app-diagnostics", ml_rect_make(r.x + 12, r.y + 12, 30, 30), ml_rgba(255, 255, 255, 255));
    ml_draw_text(c, fb, r.x + 54, r.y + 26, "Hardware check complete", 13, ml_rgba(238, 242, 252, 245));
    ml_draw_text(c, f, r.x + 54, r.y + 50, "VDPAU H.264 decode available — Beautiful mode", 12,
                 ml_rgba(198, 206, 224, 235));
}

int main(int argc, char **argv)
{
    ml_log_init("test_render", ML_LOG_WARN, NULL);
    const char *out = argc > 1 ? argv[1] : "out/test_render.png";
    int W = 1440, H = 900;
    ml_surface *s = ml_surface_new(W, H);
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, W, H));

    uint64_t t0 = ml_now_ns();
    wallpaper(&c, W, H);
    window(&c, ml_rect_make(180, 92, 780, 520), "Documents");
    menu_bar(&c, W);
    dock(&c, W, H, 700);
    notification(&c, W);
    double ms = ml_elapsed_ms(t0);

    /* second pass: only the clock area is damaged — proves the damage path */
    ml_raster_reset_stats();
    ml_ctx c2;
    ml_ctx_init(&c2, s, ml_rect_make(W - 200, 0, 200, 26));
    ml_fill_rect(&c2, ml_rect_make(W - 200, 0, 200, 26), ml_rgba(16, 18, 26, 190));
    ml_font *f = ml_font_get("mica-sans");
    ml_draw_text(&c2, f, W - 190, 18, "Sun 22 Sep  4:39 PM", 13, ml_rgba(236, 240, 250, 235));
    const ml_raster_stats *st = ml_raster_get_stats();

    if (!ml_img_write(out, s)) { fprintf(stderr, "failed to write %s\n", out); return 1; }
    printf("rendered %dx%d in %.1f ms -> %s\n", W, H, ms, out);
    printf("damage-only pass: %llu px blended in %llu draw calls (full frame would be %d px)\n",
           (unsigned long long)st->pixels_blended, (unsigned long long)st->draw_calls, W * 26);
    ml_str rep;
    ml_str_init(&rep);
    ml_icon_cache_report(&rep);
    ml_cache_report_fonts(&rep);
    printf("%s", rep.p);
    ml_str_free(&rep);
    ml_surface_free(s);
    return 0;
}
