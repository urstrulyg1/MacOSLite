#ifndef ML_RASTER_H
#define ML_RASTER_H
#include "ml/common.h"
#include "ml/surface.h"

/* Software rasterizer — the reference backend and the fallback when the GPU
 * cannot be used (spec §7: fall back, but stay responsive).
 *
 * Everything is clipped to a damage rect, so a 1920x1080 frame that only moves
 * the clock costs a few thousand blended pixels, not two million. */

typedef struct {
    uint64_t pixels_blended;   /* per-frame counters, reset by ml_raster_reset_stats */
    uint64_t draw_calls;
    uint64_t frames;
    uint64_t full_frame_count; /* frames whose damage covered the whole screen */
} ml_raster_stats;

void ml_raster_reset_stats(void);
const ml_raster_stats *ml_raster_get_stats(void);

/* --- vector paths ------------------------------------------------------- */
typedef struct {
    ml_pointd *p;      size_t n, cap;
    size_t *sub;       /* index of first point of each subpath */
    bool *subclosed;   /* whether that subpath is closed */
    size_t nsub, capsub;
} ml_path;
void ml_path_init(ml_path *p);
void ml_path_free(ml_path *p);
void ml_path_reset(ml_path *p);
/* Deep copy: the destination is reset first, so an initialized (or freshly
 * memset) path is a valid target. Used by the cursor sprite cache. */
void ml_path_copy(ml_path *dst, const ml_path *src);
/* Bounding box of every point in the path (control points included: callers
 * that need the *ink* box must rasterize and measure). */
void ml_path_bbox(const ml_path *p, double *x0, double *y0, double *x1, double *y1);
void ml_path_move(ml_path *p, double x, double y);
void ml_path_line(ml_path *p, double x, double y);
void ml_path_quad(ml_path *p, double cx, double cy, double x, double y, int segments);
void ml_path_cubic(ml_path *p, double c1x, double c1y, double c2x, double c2y, double x, double y, int segments);
void ml_path_close(ml_path *p);
void ml_path_rounded_rect(ml_path *p, double x, double y, double w, double h, double r);
void ml_path_rect(ml_path *p, double x, double y, double w, double h);
/* Parses the compact path language used by fonts and icons:
 *   M x y | L x y | Q cx cy x y | C c1x c1y c2x c2y x y | Z | R x y w h r
 * Returns false on malformed input (logs once). */
bool ml_path_parse(ml_path *p, const char *d);

/* --- primitives --------------------------------------------------------- */
typedef struct {
    ml_surface *dst;
    ml_rect clip;              /* damage rect for this draw */
} ml_ctx;

void ml_ctx_init(ml_ctx *c, ml_surface *dst, ml_rect clip);
/* How far outside its rect a shadow painted by ml_draw_shadow() reaches.
 * Callers that invalidate a rectangle because "something with a shadow moved"
 * must add this margin, or the outer ring of the shadow is clipped by the
 * damage rect and a stale ghost of it survives the repaint. */
static inline int ml_shadow_margin(double blur)
{
    return (int)ceil(blur * 2.0) + 2;
}
void ml_fill_rect(ml_ctx *c, ml_rect r, ml_color col);
void ml_fill_rounded(ml_ctx *c, ml_rect r, double radius, ml_color col);
void ml_stroke_rounded(ml_ctx *c, ml_rect r, double radius, double width, ml_color col);
void ml_fill_gradient(ml_ctx *c, ml_rect r, double radius, ml_color top, ml_color bottom);
void ml_fill_gradient_diag(ml_ctx *c, ml_rect r, double radius, ml_color a, ml_color b);
void ml_blit(ml_ctx *c, const ml_surface *src, ml_rect src_rect, int dx, int dy, uint8_t opacity);
/* Blit `src` so that *every* pixel of ctx->clip is written, sampling the source
 * at (x - offset_x, y - offset_y) with the coordinates clamped at the source
 * edges.
 *
 * ml_blit() shrinks its destination when the requested source rectangle falls
 * outside the source surface. For a damage-only compositor that is a trap: the
 * pixels of the clip that fall outside the (shrunk) destination are never
 * written, so they keep whatever the framebuffer held before — a stale slice of
 * the previous frame, exactly the "ghost" class of bug. This variant always
 * covers the whole clip. */
void ml_blit_scrolled(ml_ctx *c, const ml_surface *src, int offset_x, int offset_y,
                      uint8_t opacity);
void ml_blit_scaled(ml_ctx *c, const ml_surface *src, ml_rect dst, ml_rect src_rect, uint8_t opacity);
void ml_blit_coverage(ml_ctx *c, const uint8_t *cov, int cw, int ch, int dx, int dy, ml_color col);
/* Bilinear blit of a rectangular region of a coverage buffer (stride-aware).
 * Used by the 9-slice shadow so corners sample the right rows. */
void ml_blit_coverage_src(ml_ctx *c, const uint8_t *cov, int stride, int ch_total,
                          ml_rect src, ml_rect dst, ml_color col);
void ml_fill_radial_glow(ml_ctx *c, ml_pointd center, double rx, double ry, ml_color col);
void ml_blit_coverage_scaled(ml_ctx *c, const uint8_t *cov, int cw, int ch, ml_rect dst, ml_color col);
void ml_draw_path_stroke(ml_ctx *c, const ml_path *p, double width, ml_color col,
                         double ox, double oy, double scale);
/* Same distance-field stroke, but written as 8-bit coverage into a caller
 * buffer. The font atlas and the icon cache use this so a glyph/icon is
 * rasterized once and then blitted cheaply forever after. */
void ml_raster_stroke_coverage(uint8_t *cov, int cw, int ch, const ml_path *p,
                               double width, double ox, double oy, double scale);
void ml_raster_fill_coverage(uint8_t *cov, int cw, int ch, const ml_path *p,
                             double ox, double oy, double scale);
void ml_draw_path_fill(ml_ctx *c, const ml_path *p, ml_color col,
                       double ox, double oy, double scale);
/* Cached rounded-rect drop shadow (9-slice from a blurred mask). */
void ml_draw_shadow(ml_ctx *c, ml_rect r, double radius, double blur, ml_color col);
#endif
