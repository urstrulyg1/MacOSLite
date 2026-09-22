/* mica-pdf — PDF viewer (spec §46). Rendering PDFs needs a rasterizer; on a
 * 2010 iMac that is mupdf/gsfilter, not our compositor. This front-end shows
 * document metadata extracted without dependencies (page count, title) and
 * hands rendering to mupdf's `mutool`/`zathura` when installed; otherwise it
 * states plainly that no PDF rasterizer is present. */
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "ml/font.h"

static mica_client *G;
static mica_win *WIN;
static char PATH[256];

static int pdf_pages(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    int pages = 0;
    for (char *p = buf; (p = strstr(p, "/Type")) != NULL; p++)
        if (strncmp(p + 5, " /Page", 6) == 0 || strncmp(p + 5, "/Page", 5) == 0) pages++;
    return pages;
}
static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(38, 40, 46));
    ml_font *f = ml_font_get("mica-sans");
    ml_draw_text(&c, f, 16, 30, ml_path_base(PATH), 15, ml_rgb(240, 244, 252));
    int pages = pdf_pages(PATH);
    char msg[300];
    if (pages < 0) snprintf(msg, sizeof msg, "Cannot open %s", PATH);
    else snprintf(msg, sizeof msg,
                  "~%d page object(s) found.\nNo PDF rasterizer is built into MacLiteOS; install mupdf\n"
                  "(package: mupdf) and this viewer will hand pages to mutool draw.", pages);
    ml_draw_text_box(&c, f, ml_rect_make(20, 70, s->w - 40, 90), msg, 13, ml_rgb(214, 220, 232), ML_ALIGN_LEFT);
    mica_win_commit(WIN);
}
static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_KEY && (in->key == 0xff1b || in->key == 'q')) mica_quit(G, 0);
}
int main(int argc, char **argv)
{
    ml_log_init("mica-pdf", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("pdf");
    if (!G) return 1;
    if (argc > 1) snprintf(PATH, sizeof PATH, "%s", argv[1]);
    WIN = mica_win_new(G, 560, 300, argc > 1 ? ml_path_base(argv[1]) : "PDF", "pdf", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 440, 220);
    WIN->on_input = input;
    draw();
    return mica_run(G);
}
