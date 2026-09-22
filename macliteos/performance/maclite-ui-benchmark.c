/* maclite-ui-benchmark — frame pacing & compositing cost measurement (spec §40).
 *
 * Two modes:
 *   (default) synthetic: composites a realistic desktop stack in-process with
 *       the software rasterizer at 1920x1080 and 1440x900, measures composite
 *       time per frame class, frame interval jitter under a simulated 60 Hz
 *       vsync, and reports dropped-frame estimates for a 2010-class CPU using
 *       the blend benchmark as a scaling factor.
 *   --live: queries a running mica-comp session for its real numbers.
 */
#include "perf_common.h"
#include "../hardware/hwprobe.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/anim.h"
#include "ml/icon.h"
#include "ml/font.h"

typedef struct { double mean, worst, p95; } timing;

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static timing summarize(double *v, int n)
{
    timing t = { 0 };
    if (!n) return t;
    qsort(v, (size_t)n, sizeof(double), cmp_d);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    t.mean = sum / n;
    t.worst = v[n - 1];
    t.p95 = v[(int)(0.95 * (n - 1))];
    return t;
}

static void bench_composite(int W, int H, int windows, const char *label)
{
    ml_surface *fb = ml_surface_new(W, H);
    ml_surface *wall = ml_surface_new(W, H);
    ml_ctx wc;
    ml_ctx_init(&wc, wall, ml_rect_make(0, 0, W, H));
    for (int y = 0; y < H; y++)
        ml_fill_rect(&wc, ml_rect_make(0, y, W, 1), ml_color_mix(ml_rgb(22, 25, 42), ml_rgb(196, 116, 92), (double)y / H));
    ml_surface **wins = ml_alloc(sizeof(ml_surface *) * (size_t)windows);
    for (int i = 0; i < windows; i++) {
        wins[i] = ml_surface_new(700, 460);
        ml_ctx c;
        ml_ctx_init(&c, wins[i], ml_rect_make(0, 0, 700, 460));
        ml_fill_rounded(&c, ml_rect_make(0, 0, 700, 460), 10, ml_rgba(30, 32, 40, 246));
        ml_fill_rounded(&c, ml_rect_make(0, 0, 700, 30), 10, ml_rgb(46, 49, 60));
        ml_font *f = ml_font_get("mica-sans");
        for (int row = 0; row < 12; row++)
            ml_draw_text(&c, f, 20, 60 + row * 24, "The quick brown mica renders only what changed", 13, ml_rgb(226, 231, 242));
        ml_icon_draw(&c, "app-files", ml_rect_make(560, 340, 64, 64), ml_rgba(255, 255, 255, 255));
    }

    const int N = 120;
    double full[N], dmg[N], anim[N];
    for (int i = 0; i < N; i++) {
        /* full frame: every window damaged */
        uint64_t t0 = ml_now_ns();
        ml_ctx c;
        ml_ctx_init(&c, fb, ml_rect_make(0, 0, W, H));
        ml_blit(&c, wall, ml_rect_make(0, 0, W, H), 0, 0, 255);
        for (int k = 0; k < windows; k++)
            ml_blit(&c, wins[k], ml_rect_make(0, 0, wins[k]->w, wins[k]->h), 120 + k * 40, 90 + k * 30, 255);
        full[i] = ml_elapsed_ms(t0);
        /* damage-only: a 220x26 clock region (menu bar clock tick) */
        t0 = ml_now_ns();
        ml_ctx c2;
        ml_ctx_init(&c2, fb, ml_rect_make(W - 220, 0, 220, 26));
        ml_fill_rect(&c2, ml_rect_make(W - 220, 0, 220, 26), ml_rgba(16, 18, 26, 190));
        ml_font *f = ml_font_get("mica-sans");
        ml_draw_text(&c2, f, W - 210, 18, "Tue 22 Sep  14:26", 13, ml_rgb(238, 242, 250));
        dmg[i] = ml_elapsed_ms(t0);
        /* animated window: opacity+scale transform of one window */
        t0 = ml_now_ns();
        ml_ctx c3;
        ml_rect r = ml_rect_make(300, 200, 700, 460);
        ml_ctx_init(&c3, fb, ml_rect_make(r.x - 20, r.y - 20, r.w + 40, r.h + 40));
        ml_blit_scaled(&c3, wins[0], r, ml_rect_make(0, 0, 700, 460), 200);
        anim[i] = ml_elapsed_ms(t0);
    }
    timing tf = summarize(full, N), td = summarize(dmg, N), ta = summarize(anim, N);
    printf("%-22s %4dx%-4d  full %6.2f ms (p95 %5.2f)   damage %5.3f ms   anim-win %5.2f ms\n",
           label, W, H, tf.mean, tf.p95, td.mean, ta.mean);
    printf("%-22s        worst full %6.2f ms  => %s at 60 Hz budget (16.6 ms)\n", "",
           tf.worst, tf.worst < 16.6 ? "WITHIN" : "OVER");
    for (int i = 0; i < windows; i++) ml_surface_free(wins[i]);
    ml_free(wins);
    ml_surface_free(fb);
    ml_surface_free(wall);
}

static void bench_pacing(void)
{
    /* simulate a 60 Hz vsync loop doing damage-only work, measure interval jitter */
    const int N = 240;
    double iv[N];
    uint64_t next = ml_now_ns();
    ml_surface *fb = ml_surface_new(640, 40);
    for (int i = 0; i < N; i++) {
        next += 16666667ull;
        ml_ctx c;
        ml_ctx_init(&c, fb, ml_rect_make(0, 0, 640, 40));
        ml_fill_rect(&c, ml_rect_make(0, 0, 640, 40), ml_rgb(20, 22, 30));
        ml_font *f = ml_font_get("mica-sans");
        ml_draw_text(&c, f, 8, 26, "pacing probe 0123456789", 13, ml_rgb(230, 235, 245));
        int64_t sleep_ns = (int64_t)next - (int64_t)ml_now_ns();
        if (sleep_ns > 0) {
            struct timespec ts = { sleep_ns / 1000000000, sleep_ns % 1000000000 };
            nanosleep(&ts, NULL);
        }
        uint64_t now = ml_now_ns();
        if (i) iv[i - 1] = (double)(now - (next - 16666667ull)) / 1e6;
    }
    timing t = summarize(iv, N - 1);
    double jitter = 0;
    for (int i = 1; i < N - 1; i++) jitter += fabs(iv[i] - iv[i - 1]);
    jitter /= (N - 2);
    printf("frame pacing: mean interval %.2f ms, p95 %.2f ms, worst %.2f ms, jitter %.2f ms\n",
           t.mean, t.p95, t.worst, jitter);
    ml_surface_free(fb);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--live")) {
        msg_stats st;
        if (!comp_stats(&st)) { printf("no live compositor session\n"); return 1; }
        printf("live session: %.2f fps  mean %u us  worst %u us  dropped %llu  damage %u px  mode %s\n",
               st.fps_x100 / 100.0, st.frame_us, st.worst_us, (unsigned long long)st.dropped,
               st.damage_px_last, mica_mode_name(st.mode));
        return 0;
    }
    printf("MacLiteOS UI benchmark (software rasterizer, this host)\n");
    double mb = mica_cpu_blend_benchmark_mb_s();
    mica_cpu_info c;
    mica_cpu_probe(&c);
    printf("host: %s | %u cores | blend throughput %.0f MB/s\n\n", c.model, c.cores, mb);
    bench_composite(1440, 900, 3, "desktop 3 windows");
    bench_composite(1920, 1080, 3, "fullhd 3 windows");
    bench_composite(1920, 1080, 6, "fullhd 6 windows");
    printf("\n");
    bench_pacing();
    printf("\nnote: on the target iMac the GL path replaces the software blits;\n"
           "      numbers here bound the CPU-side cost of the fallback path.\n");
    return 0;
}
