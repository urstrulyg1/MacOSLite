/* mica-viewer — image viewer (spec §46 minimal app set).
 * Lazy by design: the file is decoded once on open into a single surface;
 * repaints are damage-only blits. No thumbnail cache, no indexer, no daemon.
 * Usage: mica-viewer [file ...]      n/p cycle, +/- zoom, 0 fit, arrows pan */
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "ml/img.h"
#include "ml/font.h"

static mica_client *G;
static mica_win *WIN;
static char **FILES;
static int NFILES, CUR;
static ml_surface *IMG;
static double ZOOM;          /* 0 = fit */
static int PANX, PANY, LX, LY;
static bool PANNING;

static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(24, 26, 32));
    ml_font *f = ml_font_get("mica-sans");
    if (!IMG) {
        ml_draw_text_box(&c, f, ml_rect_make(0, s->h / 2 - 10, s->w, 20),
                         NFILES ? "cannot decode this file (PNG supported)" : "no image given",
                         14, ml_rgb(190, 196, 210), ML_ALIGN_CENTER);
        mica_win_commit(WIN);
        return;
    }
    ml_rect dst;
    if (ZOOM > 0) {
        int w = (int)(IMG->w * ZOOM), h = (int)(IMG->h * ZOOM);
        dst = ml_rect_make(s->w / 2 - w / 2 + PANX, s->h / 2 - h / 2 + PANY, w, h);
    } else {
        double k = ML_MIN((double)(s->w - 24) / IMG->w, (double)(s->h - 40) / IMG->h);
        if (k > 1) k = 1;
        int w = ML_MAX(1, (int)(IMG->w * k)), h = ML_MAX(1, (int)(IMG->h * k));
        dst = ml_rect_make(s->w / 2 - w / 2, s->h / 2 - h / 2, w, h);
    }
    ml_fill_rounded(&c, ml_rect_make(dst.x - 3, dst.y - 3, dst.w + 6, dst.h + 6), 4, ml_rgba(0, 0, 0, 90));
    ml_blit_scaled(&c, IMG, dst, ml_rect_make(0, 0, IMG->w, IMG->h), 255);
    char cap[96];
    snprintf(cap, sizeof cap, "%dx%d px   %s", IMG->w, IMG->h, ZOOM > 0 ? "zoomed" : "fit");
    ml_draw_text(&c, f, 10, s->h - 10, cap, 11, ml_rgb(150, 156, 172));
    mica_win_commit(WIN);
}

static void load_current(void)
{
    if (IMG) { ml_surface_free(IMG); IMG = NULL; }
    if (CUR < NFILES) {
        IMG = ml_img_read_png(FILES[CUR]);
        char t[160];
        snprintf(t, sizeof t, "%s%s", ml_path_base(FILES[CUR]), IMG ? "" : " (unreadable)");
        mica_win_set_title(WIN, t);
    }
    ZOOM = 0; PANX = PANY = 0;
    ml_surface_damage_all(WIN->surf);
    draw();
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    switch (in->kind) {
    case IN_KEY:
        switch (in->key) {
        case 0xff1b: case 'q': mica_quit(G, 0); return;
        case 'n': if (CUR + 1 < NFILES) { CUR++; load_current(); } return;
        case 'p': if (CUR > 0) { CUR--; load_current(); } return;
        case '0': ZOOM = 0; PANX = PANY = 0; ml_surface_damage_all(WIN->surf); draw(); return;
        case '+': case '=': ZOOM = ZOOM <= 0 ? 1.0 : ML_MIN(8.0, ZOOM * 1.25); break;
        case '-': ZOOM = ZOOM <= 0 ? 0.8 : ML_MAX(0.1, ZOOM / 1.25); break;
        case 0xff53: PANX += 40; break;   /* left  */
        case 0xff51: PANX -= 40; break;   /* right */
        case 0xff52: PANY += 40; break;   /* up    */
        case 0xff54: PANY -= 40; break;   /* down  */
        default: return;
        }
        ml_surface_damage_all(WIN->surf);
        draw();
        break;
    case IN_MOVE:
        if (PANNING && ZOOM > 0) {
            PANX += in->x - LX; PANY += in->y - LY;
            ml_surface_damage_all(WIN->surf);
            draw();
        }
        LX = in->x; LY = in->y;
        break;
    case IN_DOWN: if (in->button == 1) { PANNING = true; LX = in->x; LY = in->y; } break;
    case IN_UP:   if (in->button == 1) PANNING = false; break;
    default: break;
    }
}
static void configure(mica_win *w, const msg_configure *m) { (void)m; (void)w; draw(); }

int main(int argc, char **argv)
{
    ml_log_init("mica-viewer", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("viewer");
    if (!G) return 1;
    FILES = argv + 1;
    NFILES = argc - 1;
    WIN = mica_win_new(G, 760, 560, NFILES ? ml_path_base(FILES[0]) : "Viewer", "viewer", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 140, 70);
    WIN->on_input = input;
    WIN->on_configure = configure;
    load_current();
    return mica_run(G);
}
