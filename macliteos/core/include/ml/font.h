#ifndef ML_FONT_H
#define ML_FONT_H
#include "ml/common.h"
#include "ml/raster.h"
#include "ml/util.h"

/* Typography.
 *
 * Two providers:
 *   1. "mica-sans" — an original monoline vector face shipped as path data in
 *      core/src/font_data.c. Zero dependencies, rasterized through the
 *      distance-field stroker and cached per (codepoint, size).
 *   2. FreeType TTF — compiled in only when the host has freetype2 (the target
 *      rootfs ships DejaVu Sans, which is freely licensed). Same API, so the
 *      shell never knows which one it got.
 *
 * Spec §38: no Apple assets. Spec §44: the glyph cache is byte-budgeted. */

typedef struct ml_font ml_font;
typedef struct {
    int w, h;          /* coverage bitmap size */
    int ox, oy;        /* offset of bitmap origin relative to pen/baseline anchor */
    int advance;       /* pen advance in px */
    const uint8_t *cov;
} ml_glyph;

ml_font *ml_font_get(const char *family);        /* cached: "mica-sans", "mica-sans-bold", "mica-mono" */
ml_font *ml_font_load_ttf(const char *path, const char *family);  /* NULL if unsupported */
void ml_font_release(ml_font *f);
const char *ml_font_family(const ml_font *f);
bool ml_font_is_vector_builtin(const ml_font *f);

int ml_font_ascent(const ml_font *f, int px);
int ml_font_descent(const ml_font *f, int px);
int ml_font_line_height(const ml_font *f, int px);
const ml_glyph *ml_font_glyph(ml_font *f, uint32_t cp, int px);
int ml_text_width(ml_font *f, const char *utf8, int px);
int ml_text_width_n(ml_font *f, const char *utf8, size_t nbytes, int px);
uint32_t ml_utf8_next(const char **s);
size_t ml_utf8_len(const char *s);

void ml_draw_text(ml_ctx *c, ml_font *f, int x, int baseline_y, const char *utf8, int px, ml_color col);
typedef enum { ML_ALIGN_LEFT, ML_ALIGN_CENTER, ML_ALIGN_RIGHT } ml_align;
/* Draws inside box (vertically centred), truncating with an ellipsis. */
void ml_draw_text_box(ml_ctx *c, ml_font *f, ml_rect box, const char *utf8, int px, ml_color col, ml_align align);
void ml_cache_report_fonts(ml_str *out);
#endif
