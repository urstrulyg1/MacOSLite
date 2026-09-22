#include "ml/icon.h"
#include "ml/raster.h"
#include "ml/cache.h"
#include "ml/log.h"

typedef struct { ml_surface *s; char key[96]; } icon_slot;
static ml_cache *g_icons;

static void icon_slot_free(void *v)
{
    icon_slot *s = v;
    ml_surface_free(s->s);
    ml_free(s);
}
static void ensure_cache(void)
{
    /* 2 MB: ~500 icons at 48px, more than the shell ever needs at once */
    if (!g_icons) g_icons = ml_cache_new(2u * 1024 * 1024, 512, icon_slot_free);
}

const ml_icon_def *ml_icon_find(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < ml_icon_table_n; i++)
        if (strcmp(ml_icon_table[i].name, name) == 0) return &ml_icon_table[i];
    return NULL;
}
bool ml_icon_exists(const char *name) { return ml_icon_find(name) != NULL; }

const char **ml_icon_names(size_t *n)
{
    static const char **names;
    if (!names) {
        names = ml_alloc((ml_icon_table_n + 1) * sizeof *names);
        for (size_t i = 0; i < ml_icon_table_n; i++) names[i] = ml_icon_table[i].name;
        names[ml_icon_table_n] = NULL;
    }
    if (n) *n = ml_icon_table_n;
    return names;
}

ml_surface *ml_icon_render(const char *name, int size, ml_color tint)
{
    const ml_icon_def *d = ml_icon_find(name);
    if (!d || size <= 0) return NULL;
    ensure_cache();
    char key[96];
    snprintf(key, sizeof key, "%s@%d#%08x", name, size, ml_color_u32(tint));
    icon_slot *slot = ml_cache_get(g_icons, key);
    if (slot) return slot->s;

    ml_surface *s = ml_surface_new(size, size);
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, size, size));
    double tile_r = size * 0.2235;   /* our own corner ratio */

    if (d->tile) {
        ml_color a, b;
        if (!ml_color_parse(d->grad_a, &a)) a = ml_rgb(120, 130, 150);
        if (!ml_color_parse(d->grad_b, &b)) b = a;
        ml_fill_gradient_diag(&c, ml_rect_make(0, 0, size, size), tile_r, a, b);
        /* 1px inner highlight: cheap, no blur, reads as "glass" */
        ml_stroke_rounded(&c, ml_rect_make(0, 0, size, size), tile_r, 1.0, ml_rgba(255, 255, 255, 60));
    }

    double pad = d->pad * size / 100.0;
    double scale = (size - 2 * pad) / 100.0;
    double ox = pad, oy = pad;

    if (d->fill && d->fill[0]) {
        ml_path p;
        ml_path_init(&p);
        if (ml_path_parse(&p, d->fill)) {
            ml_color fc = tint;
            if (d->fill_color && ml_color_parse(d->fill_color, &fc)) { /* explicit */ }
            ml_draw_path_fill(&c, &p, fc, ox, oy, scale);
        }
        ml_path_free(&p);
    }
    if (d->stroke && d->stroke[0]) {
        ml_path p;
        ml_path_init(&p);
        if (ml_path_parse(&p, d->stroke)) {
            ml_color sc = tint;
            if (d->stroke_color && ml_color_parse(d->stroke_color, &sc)) { /* explicit */ }
            ml_draw_path_stroke(&c, &p, d->sw, sc, ox, oy, scale);
        }
        ml_path_free(&p);
    }

    slot = ml_zalloc(sizeof *slot);
    slot->s = s;
    snprintf(slot->key, sizeof slot->key, "%s", key);
    ml_cache_put(g_icons, key, slot, (size_t)size * size * 4 + sizeof *slot);
    return s;
}

void ml_icon_draw(ml_ctx *c, const char *name, ml_rect dst, ml_color tint)
{
    ml_surface *icon = ml_icon_render(name, dst.w, tint);
    if (!icon) return;
    if (dst.h == dst.w) ml_blit(c, icon, ml_rect_make(0, 0, icon->w, icon->h), dst.x, dst.y, 255);
    else ml_blit_scaled(c, icon, dst, ml_rect_make(0, 0, icon->w, icon->h), 255);
}

void ml_icon_cache_report(ml_str *out)
{
    ensure_cache();
    uint64_t h, m, e;
    size_t b;
    ml_cache_stats(g_icons, &h, &m, &e, &b);
    ml_str_appendf(out, "icons cached=%zu bytes=%zu hits=%llu miss=%llu evict=%llu\n",
                   ml_cache_count(g_icons), b, (unsigned long long)h,
                   (unsigned long long)m, (unsigned long long)e);
}
void ml_icon_cache_clear(void)
{
    if (g_icons) ml_cache_clear(g_icons);
}
