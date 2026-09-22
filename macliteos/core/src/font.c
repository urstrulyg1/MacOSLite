#include "ml/font.h"
#include "ml/fontdata.h"
#include "ml/cache.h"
#include "ml/log.h"
#include <math.h>

#define ML_EM 100.0
#define ML_BASELINE 78.0

typedef struct { ml_glyph g; uint8_t *buf; } glyph_slot;

struct ml_font {
    char family[48];
    double stroke_units;
    bool mono;
    bool builtin;
    ml_cache *glyphs;
    void *ft_face;             /* FT_Face when built with freetype */
};

/* One parsed path per glyph definition, built lazily and kept for the process
 * lifetime: parsing 101 short paths costs microseconds and we never do it in a
 * render loop. */
static ml_path *g_paths;
static char *g_paths_ready;
static struct ml_font g_fonts[8];
static size_t g_nfonts;

static const ml_glyph_def *find_def(uint32_t cp)
{
    /* small linear scan is fine (101 entries) and avoids a hash table */
    for (size_t i = 0; i < ml_glyph_table_n; i++)
        if (ml_glyph_table[i].cp == cp) return &ml_glyph_table[i];
    return NULL;
}
static size_t def_index(const ml_glyph_def *d) { return (size_t)(d - ml_glyph_table); }

static const ml_path *font_path(const ml_glyph_def *d)
{
    if (!g_paths) {
        g_paths = ml_zalloc(ml_glyph_table_n * sizeof *g_paths);
        g_paths_ready = ml_zalloc(ml_glyph_table_n);
    }
    size_t i = def_index(d);
    if (!g_paths_ready[i]) {
        ml_path_init(&g_paths[i]);
        ml_path_parse(&g_paths[i], d->d);
        g_paths_ready[i] = 1;
    }
    return &g_paths[i];
}

static void glyph_slot_free(void *v)
{
    glyph_slot *s = v;
    ml_free(s->buf);
    ml_free(s);
}

ml_font *ml_font_get(const char *family)
{
    if (!family || !*family) family = "mica-sans";
    for (size_t i = 0; i < g_nfonts; i++)
        if (strcmp(g_fonts[i].family, family) == 0) return &g_fonts[i];
    if (g_nfonts >= ML_ARRAY_SIZE(g_fonts)) return &g_fonts[0];
    struct ml_font *f = &g_fonts[g_nfonts++];
    memset(f, 0, sizeof *f);
    snprintf(f->family, sizeof f->family, "%s", family);
    f->builtin = true;
    bool bold = strstr(family, "bold") != NULL;
    f->mono = strcmp(family, "mica-mono") == 0;
    f->stroke_units = bold ? 14.5 : 10.5;
    /* glyph atlas: 512 KB budget is ~3000 glyphs at 13px, plenty for a shell */
    f->glyphs = ml_cache_new(512 * 1024, 4096, glyph_slot_free);
    return f;
}

ml_font *ml_font_load_ttf(const char *path, const char *family)
{
#ifdef ML_HAVE_FREETYPE
    /* Implemented in font_freetype.c (compiled only when the host has ft2). */
    extern ml_font *ml_font_load_ttf_impl(const char *path, const char *family);
    return ml_font_load_ttf_impl(path, family);
#else
    ML_DBG("freetype not compiled in; ignoring TTF request %s (using built-in Mica Sans)", path ? path : "?");
    (void)family;
    return NULL;
#endif
}

void ml_font_release(ml_font *f)
{
    if (!f || f->builtin) return;
    ml_cache_destroy(f->glyphs);
    ml_free(f);
}
const char *ml_font_family(const ml_font *f) { return f ? f->family : "?"; }
bool ml_font_is_vector_builtin(const ml_font *f) { return f && f->builtin; }

int ml_font_ascent(const ml_font *f, int px) { (void)f; return (int)lround(px * (ML_BASELINE - 12) / ML_EM); }
int ml_font_descent(const ml_font *f, int px) { (void)f; return (int)lround(px * (98 - ML_BASELINE) / ML_EM) + 1; }
int ml_font_line_height(const ml_font *f, int px) { return ml_font_ascent(f, px) + ml_font_descent(f, px) + (px / 6); }

uint32_t ml_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p) return 0;
    uint32_t cp;
    int extra;
    if (*p < 0x80) { cp = *p; extra = 0; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
    else { *s = (const char *)(p + 1); return 0xFFFD; }
    p++;
    for (int i = 0; i < extra && *p; i++) {
        if ((*p & 0xC0) != 0x80) break;
        cp = (cp << 6) | (*p & 0x3F);
        p++;
    }
    *s = (const char *)p;
    return cp;
}
size_t ml_utf8_len(const char *s)
{
    size_t n = 0;
    while (*s) { ml_utf8_next(&s); n++; }
    return n;
}

static int advance_for(const struct ml_font *f, const ml_glyph_def *d, int px)
{
    if (f->mono) return (int)lround(60.0 * px / ML_EM);
    return (int)lround(d->adv * (double)px / ML_EM);
}

const ml_glyph *ml_font_glyph(ml_font *f, uint32_t cp, int px)
{
    if (!f || px <= 0) return NULL;
    const ml_glyph_def *d = find_def(cp);
    if (!d) {
        if (cp == '\t') d = find_def(' ');
        else return NULL;
    }
    char key[48];
    snprintf(key, sizeof key, "%u:%d:%.1f:%d", cp, px, f->stroke_units, f->mono);
    glyph_slot *s = ml_cache_get(f->glyphs, key);
    if (s) return &s->g;

    s = ml_zalloc(sizeof *s);
    s->g.advance = advance_for(f, d, px);
    if (!d->d[0]) {                       /* space / blank */
        ml_cache_put(f->glyphs, key, s, sizeof *s);
        return &s->g;
    }
    const ml_path *p = font_path(d);
    double scale = px / ML_EM;
    double stroke = f->stroke_units * scale;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (size_t i = 0; i < p->n; i++) {
        x0 = ML_MIN(x0, p->p[i].x); x1 = ML_MAX(x1, p->p[i].x);
        y0 = ML_MIN(y0, p->p[i].y); y1 = ML_MAX(y1, p->p[i].y);
    }
    if (x1 < x0) { x0 = y0 = 0; x1 = y1 = 1; }
    double ox = -x0 * scale + stroke * 0.5 + 1.0;
    double oy = -y0 * scale + stroke * 0.5 + 1.0;
    int w = (int)ceil((x1 - x0) * scale + stroke + 2.0);
    int h = (int)ceil((y1 - y0) * scale + stroke + 2.0);
    w = ML_CLAMP(w, 1, 4096);
    h = ML_CLAMP(h, 1, 4096);
    s->buf = ml_zalloc((size_t)w * h);
    ml_raster_stroke_coverage(s->buf, w, h, p, f->stroke_units, ox, oy, scale);
    s->g.w = w; s->g.h = h;
    s->g.ox = (int)floor(ox);
    s->g.oy = (int)floor(oy);
    s->g.cov = s->buf;
    ml_cache_put(f->glyphs, key, s, (size_t)w * h + sizeof *s);
    return &s->g;
}

int ml_text_width_n(ml_font *f, const char *utf8, size_t nbytes, int px)
{
    if (!utf8 || !f) return 0;
    double scale = px / ML_EM;
    int w = 0;
    const char *s = utf8;
    size_t used = 0;
    while (*s && used < nbytes) {
        const char *prev = s;
        uint32_t cp = ml_utf8_next(&s);
        used += (size_t)(s - prev);
        const ml_glyph_def *d = find_def(cp);
        w += d ? advance_for(f, d, px) : (int)lround(30 * scale);
    }
    return w;
}
int ml_text_width(ml_font *f, const char *utf8, int px)
{
    return utf8 ? ml_text_width_n(f, utf8, strlen(utf8), px) : 0;
}

void ml_draw_text(ml_ctx *c, ml_font *f, int x, int baseline_y, const char *utf8, int px, ml_color col)
{
    if (!utf8 || !f || col.a == 0) return;
    double scale = px / ML_EM;
    int pen = x;
    const char *s = utf8;
    while (*s) {
        uint32_t cp = ml_utf8_next(&s);
        const ml_glyph *g = ml_font_glyph(f, cp, px);
        if (!g) { pen += (int)lround(30 * scale); continue; }
        if (g->w && g->h) {
            int dx = pen - g->ox;
            int dy = (int)lround(baseline_y - ML_BASELINE * scale) - g->oy;
            ml_blit_coverage(c, g->cov, g->w, g->h, dx, dy, col);
        }
        pen += g->advance;
    }
}

void ml_draw_text_box(ml_ctx *c, ml_font *f, ml_rect box, const char *utf8, int px, ml_color col, ml_align align)
{
    if (!utf8 || ml_rect_empty(box)) return;
    int w = ml_text_width(f, utf8, px);
    char *txt = NULL;
    if (w > box.w) {
        const char *ell = "\xE2\x80\xA6";
        int ew = ml_text_width(f, ell, px);
        size_t len = strlen(utf8);
        while (len > 1 && ml_text_width_n(f, utf8, len, px) + ew > box.w) len--;
        txt = ml_alloc(len + strlen(ell) + 1);
        memcpy(txt, utf8, len);
        memcpy(txt + len, ell, strlen(ell) + 1);
        utf8 = txt;
        w = ml_text_width(f, utf8, px);
    }
    int x = box.x;
    if (align == ML_ALIGN_CENTER) x = box.x + (box.w - w) / 2;
    else if (align == ML_ALIGN_RIGHT) x = box.x + box.w - w;
    int by = box.y + (box.h + ml_font_ascent(f, px) - ml_font_descent(f, px)) / 2;
    ml_draw_text(c, f, x, by, utf8, px, col);
    ml_free(txt);
}

void ml_cache_report_fonts(ml_str *out)
{
    for (size_t i = 0; i < g_nfonts; i++) {
        uint64_t h, m, e;
        size_t b;
        ml_cache_stats(g_fonts[i].glyphs, &h, &m, &e, &b);
        ml_str_appendf(out, "font[%s] glyphs=%zu bytes=%zu hits=%llu miss=%llu evict=%llu\n",
                       g_fonts[i].family, ml_cache_count(g_fonts[i].glyphs), b,
                       (unsigned long long)h, (unsigned long long)m, (unsigned long long)e);
    }
}
