/* mica-textedit — plain-text editor (spec §46). No rich text, no daemon:
 * one buffer (capped at 1 MiB), damage-only redraw of changed rows. */
#include <stdarg.h>
#include <unistd.h>
#include "ml/log.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "ml/font.h"

#define MAXL 4096
static mica_client *G;
static mica_win *WIN;
static ml_str L[MAXL];
static int NL, CUR_L, CUR_C, SCROLL;
static char PATH[256];
static bool DIRTY;

static void line_set(int i, const char *s) { ml_str_free(&L[i]); ml_str_init(&L[i]); ml_str_append(&L[i], s); }
static void load(const char *path)
{
static void load(const char *path)
{
    if (path && path != PATH) snprintf(PATH, sizeof PATH, "%s", path);
    char *txt = ml_read_file(path, NULL);
    NL = 0;
    if (txt) {
        char *p = txt;
        while (*p && NL < MAXL) {
            char *nl = strchr(p, '\n');
            if (nl) *nl = 0;
            line_set(NL++, p);
            if (!nl) break;
            p = nl + 1;
        }
        ml_free(txt);
    }
    if (!NL) line_set(NL++, "");
    mica_win_set_title(WIN, ml_path_base(PATH));
}
static void save(void)
{
    FILE *f = fopen(PATH, "w");
    if (!f) return;
    for (int i = 0; i < NL; i++) fprintf(f, "%s\n", L[i].p ? L[i].p : "");
    fclose(f);
    DIRTY = false;
    mica_win_set_title(WIN, ml_path_base(PATH));
}
#define LH 18
static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    ml_fill_rect(&c, ml_rect_make(0, 0, s->w, s->h), ml_rgb(30, 32, 38));
    ml_font *f = ml_font_get("mica-sans");
    int rows = (s->h - 24) / LH;
    while (CUR_L < SCROLL) SCROLL = CUR_L;
    while (CUR_L >= SCROLL + rows) SCROLL = CUR_L - rows + 1;
    for (int i = SCROLL; i < NL && i < SCROLL + rows; i++) {
        int y = 20 + (i - SCROLL) * LH;
        if (i == CUR_L) ml_fill_rect(&c, ml_rect_make(0, y - 13, s->w, LH), ml_rgba(70, 130, 250, 40));
        ml_draw_text(&c, f, 12, y, L[i].p ? L[i].p : "", 13, ml_rgb(226, 231, 242));
    }
    char st[96];
    snprintf(st, sizeof st, "%s  line %d/%d  %s", PATH, CUR_L + 1, NL, DIRTY ? "modified" : "saved");
    ml_fill_rect(&c, ml_rect_make(0, s->h - 20, s->w, 20), ml_rgba(20, 22, 28, 220));
    ml_draw_text(&c, f, 10, s->h - 6, st, 11, ml_rgb(150, 156, 172));
    mica_win_commit(WIN);
}
static void ins_char(char ch)
{
    ml_str *l = &L[CUR_L];
    int len = (int)strlen(l->p);
    if (CUR_C > len) CUR_C = len;
    char *np = ml_alloc((size_t)len + 2);
    memcpy(np, l->p, (size_t)CUR_C);
    np[CUR_C] = ch;
    memcpy(np + CUR_C + 1, l->p + CUR_C, (size_t)(len - CUR_C + 1));
    ml_str_free(l);
    l->p = np; l->n = (size_t)len + 1; l->cap = (size_t)len + 2;
    CUR_C++;
    DIRTY = true;
}
static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind != IN_KEY) return;
    uint32_t k = in->key;
    bool ctrl = (in->mods & 4) != 0;
    if (ctrl && k == 's') { save(); draw(); return; }
    switch (k) {
    case 0xff0d: { /* enter */
        if (NL >= MAXL) return;
        ml_str *l = &L[CUR_L];
        int len = (int)strlen(l->p);
        if (CUR_C > len) CUR_C = len;
        for (int i = NL; i > CUR_L + 1; i--) L[i] = L[i - 1];
        NL++;
        ml_str_init(&L[CUR_L + 1]);
        ml_str_append(&L[CUR_L + 1], l->p + CUR_C);
        char keep[4096];
        snprintf(keep, sizeof keep, "%.*s", CUR_C, l->p);
        line_set(CUR_L, keep);
        CUR_L++; CUR_C = 0; DIRTY = true;
        break;
    }
    case 0xff08: { /* backspace */
        ml_str *l = &L[CUR_L];
        int len = (int)strlen(l->p);
        if (CUR_C > 0 && CUR_C <= len) {
            memmove(l->p + CUR_C - 1, l->p + CUR_C, (size_t)(len - CUR_C + 1));
            CUR_C--; DIRTY = true;
        } else if (CUR_C == 0 && CUR_L > 0) {
            int plen = (int)strlen(L[CUR_L - 1].p);
            ml_str_append(&L[CUR_L - 1], l->p);
            ml_str_free(l);
            for (int i = CUR_L; i < NL - 1; i++) L[i] = L[i + 1];
            NL--;
            CUR_L--; CUR_C = plen; DIRTY = true;
        }
        break;
    }
    case 0xff52: if (CUR_L > 0) { CUR_L--; } break;
    case 0xff54: if (CUR_L < NL - 1) { CUR_L++; } break;
    case 0xff53: if (CUR_C > 0) CUR_C--; break;
    case 0xff51: CUR_C++; break;
    case 0xff1b: mica_quit(G, 0); return;
    default:
        if (k >= 32 && k < 127) ins_char((char)k);
        else return;
    }
    int len = (int)strlen(L[CUR_L].p);
    if (CUR_C > len) CUR_C = len;
    draw();
}
int main(int argc, char **argv)
{
    ml_log_init("mica-textedit", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("textedit");
    if (!G) return 1;
    WIN = mica_win_new(G, 680, 480, "Untitled", "textedit", 0);
    if (!WIN) return 1;
    mica_win_place(WIN, 200, 110);
    WIN->on_input = input;
    snprintf(PATH, sizeof PATH, "%s", argc > 1 ? argv[1] : "/tmp/untitled.txt");
    load(PATH);
    draw();
    return mica_run(G);
}
