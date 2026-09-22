#ifndef ML_ICON_H
#define ML_ICON_H
#include "ml/common.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/util.h"

/* Original vector icon set (spec §38: no third-party or Apple artwork).
 *
 * Icons are path data on a 100x100 em grid, rasterized on demand into RGBA
 * surfaces and cached in a byte-budgeted LRU. Two styles:
 *   - "template" icons: monochrome, tinted by the caller (menu bar, buttons)
 *   - "tile" icons: rounded gradient tile + glyph (Dock, launcher, Finder)
 */
typedef struct {
    const char *name;
    uint8_t tile;                    /* draw rounded gradient tile behind */
    const char *grad_a, *grad_b;     /* tile gradient (NULL = flat) */
    const char *fill;                /* filled path data (may be NULL) */
    const char *stroke;              /* stroked path data (may be NULL) */
    double sw;                       /* stroke width, em units */
    const char *fill_color;          /* NULL = use caller tint */
    const char *stroke_color;        /* NULL = use caller tint */
    double pad;                      /* em units of inset for the glyph */
} ml_icon_def;

extern const ml_icon_def ml_icon_table[];
extern const size_t ml_icon_table_n;

const ml_icon_def *ml_icon_find(const char *name);
bool ml_icon_exists(const char *name);
/* Returns a cached surface owned by the icon cache — do not free or draw into.
 * `tint` applies to template icons only. */
ml_surface *ml_icon_render(const char *name, int size_px, ml_color tint);
void ml_icon_draw(ml_ctx *c, const char *name, ml_rect dst, ml_color tint);
void ml_icon_cache_report(ml_str *out);
void ml_icon_cache_clear(void);
const char **ml_icon_names(size_t *n);
#endif
