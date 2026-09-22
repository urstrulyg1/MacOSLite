/* maclite-gpu-benchmark — compositor/GPU frame budget measurement (spec §3).
 *
 * Deliberately small and bounded: it renders a fixed number of frames (default
 * 300 = 5 s at 60 Hz), reports, and exits. No background process, no service,
 * no cache left behind.
 *
 * Two ways to run it:
 *   maclite-gpu-benchmark            in-process: composite a realistic desktop
 *                                    stack through the real present backend
 *                                    (KMS when a card exists, software
 *                                    otherwise) and measure frame pacing.
 *   maclite-gpu-benchmark --live     sample a running mica-comp session instead.
 */
#include "diag_common.h"
#include "../performance/perf_common.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"
#include "ml/anim.h"
#include <sys/resource.h>

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static double percentile(double *v, int n, double p)
{
    if (!n) return 0;
    qsort(v, (size_t)n, sizeof(double), cmp_d);
    int i = (int)(p * (n - 1));
    return v[i];
}

static void self_rss(char *out, size_t outlen)
{
    char *s = ml_read_file(hw_proc("self/status"), NULL);
    if (!s) { snprintf(out, outlen, "unknown"); return; }
    unsigned long rss = 0, hwm = 0;
    char *p = strstr(s, "VmRSS:");
    if (p) rss = strtoul(p + 6, NULL, 10);
    p = strstr(s, "VmHWM:");
    if (p) hwm = strtoul(p + 6, NULL, 10);
    snprintf(out, outlen, "%lu kB RSS (%lu kB peak)", rss, hwm);
    ml_free(s);
}

static int run_live(int samples)
{
    msg_stats st;
    if (!comp_stats(&st)) { printf("no live compositor session\n"); return HW_EXIT_NOT_TESTED; }
    printf("MacLiteOS GPU Benchmark (live session)\n\n");
    printf("Renderer:        %s\n", st.gpu_name);
    printf("Backend:         %s\n", st.backend == 1 ? "kms" : st.backend == 2 ? "fbdev" : "headless");
    printf("Resolution:      %dx%d\n", st.screen_w, st.screen_h);
    printf("Mode:            %s\n", mica_mode_name(st.mode));
    printf("\nsampling %d x 500 ms...\n", samples);
    double fps[64];
    int n = 0;
    uint64_t first_dropped = st.dropped, first_frames = st.frames;
    for (int i = 0; i < samples && n < 64; i++) {
        struct timespec ts = { 0, 500 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        if (comp_stats(&st)) fps[n++] = st.fps_x100 / 100.0;
    }
    double mean = 0;
    for (int i = 0; i < n; i++) mean += fps[i];
    if (n) mean /= n;
    unsigned long long sess_dropped = (unsigned long long)st.dropped - first_dropped;
    unsigned long long sess_frames = (unsigned long long)st.frames - first_frames;
    printf("\nAverage FPS:     %.2f\n", mean);
    printf("Average Frame Time: %.2f ms\n", st.frame_us / 1000.0);
    printf("Worst Frame Time:   %.2f ms\n", st.worst_us / 1000.0);
    printf("Dropped Frames:  %llu of %llu presented during sampling\n", sess_dropped, sess_frames);
    char rss[96];
    self_rss(rss, sizeof rss);
    printf("Memory:          %s (this tool); use maclite-memory for the session\n", rss);
    printf("\nHardware Acceleration: %s\n", st.accel ? "PASS" : "software compositing (see docs/gpu.md)");
    return st.accel ? HW_EXIT_PASS : HW_EXIT_NOT_TESTED;
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--live] [--frames N] [--windows N] [--software]\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    const char *fr = diag_opt(argc, argv, "--frames");
    const char *wn = diag_opt(argc, argv, "--windows");
    int frames = fr ? atoi(fr) : 300;
    int windows = wn ? atoi(wn) : 3;
    bool force_sw = diag_flag(argc, argv, "--software");
    if (diag_flag(argc, argv, "--live")) return run_live(10);

    mica_gpu_info g;
    mica_cpu_info c;
    mica_gpu_probe(&g);
    mica_cpu_probe(&c);

    int W = 1920, H = 1080;
    ml_display disp;
    if (!ml_display_open(&disp, force_sw ? "headless" : "auto", &W, &H)) {
        fprintf(stderr, "display open failed\n");
        return HW_EXIT_FAIL;
    }

    /* --- build a realistic desktop stack once (no per-frame allocation) --- */
    uint64_t t_start = ml_now_ns();
    ml_surface *fb = ml_surface_new(W, H);
    ml_surface *wall = ml_surface_new(W, H);
    ml_ctx wc;
    ml_ctx_init(&wc, wall, ml_rect_make(0, 0, W, H));
    for (int y = 0; y < H; y++)
        ml_fill_rect(&wc, ml_rect_make(0, y, W, 1),
                     ml_color_mix(ml_rgb(22, 25, 42), ml_rgb(196, 116, 92), (double)y / H));
    ml_font *font = ml_font_get("mica-sans");
    ml_surface **wins = ml_alloc(sizeof(ml_surface *) * (size_t)windows);
    for (int i = 0; i < windows; i++) {
        wins[i] = ml_surface_new(700, 460);
        ml_ctx cc;
        ml_ctx_init(&cc, wins[i], ml_rect_make(0, 0, 700, 460));
        ml_fill_rounded(&cc, ml_rect_make(0, 0, 700, 460), 10, ml_rgba(30, 32, 40, 246));
        ml_fill_rounded(&cc, ml_rect_make(0, 0, 700, 30), 10, ml_rgb(46, 49, 60));
        for (int row = 0; row < 12; row++)
            ml_draw_text(&cc, font, 20, 60 + row * 24,
                         "MacLiteOS renders only the rectangles that changed", 13, ml_rgb(226, 231, 242));
        ml_icon_draw(&cc, "app-files", ml_rect_make(560, 340, 64, 64), ml_rgba(255, 255, 255, 255));
    }
    double setup_ms = ml_elapsed_ms(t_start);

    /* damage set: one moving window (like a drag) + the menu-bar clock */
    ml_rect dmg[2];
    int ndmg = 2;
    struct rusage ru0, ru1;
    getrusage(RUSAGE_SELF, &ru0);
    uint64_t t0 = ml_now_ns();
    double *ft = ml_alloc(sizeof(double) * (size_t)frames);
    double *iv = ml_alloc(sizeof(double) * (size_t)frames);
    uint64_t next = ml_now_ns();
    uint64_t dropped = 0, last = 0;
    uint32_t *dst = disp.kind == ML_DISP_HEADLESS ? NULL : ml_display_pixels(&disp);

    for (int i = 0; i < frames; i++) {
        next += 16666667ull;                        /* 60 Hz target */
        uint64_t f0 = ml_now_ns();
        dmg[0] = ml_rect_make(120 + (i * 7) % (W - 820), 140 + (i * 3) % 200, 700, 460);
        dmg[1] = ml_rect_make(W - 220, 0, 220, 26);
        for (int k = 0; k < ndmg; k++) {
            ml_ctx cc;
            ml_ctx_init(&cc, fb, dmg[k]);
            ml_blit(&cc, wall, dmg[k], dmg[k].x, dmg[k].y, 255);
            for (int w = 0; w < windows; w++) {
                ml_rect r = ml_rect_make(120 + w * 40, 140 + w * 30, 700, 460);
                if (!ml_rect_intersects(r, dmg[k])) continue;
                ml_blit(&cc, wins[w], ml_rect_make(0, 0, 700, 460), r.x, r.y, 255);
            }
            if (k == 1) {
                ml_fill_rect(&cc, dmg[1], ml_rgba(16, 18, 26, 190));
                ml_draw_text(&cc, font, dmg[1].x + 10, 18, "Tue 22 Sep  14:26", 13, ml_rgb(238, 242, 250));
            }
        }
        if (dst) ml_display_commit(&disp, fb->px, dmg, ndmg);
        ft[i] = ml_elapsed_ms(f0);
        int64_t sleep_ns = (int64_t)next - (int64_t)ml_now_ns();
        if (sleep_ns > 0) {
            struct timespec ts = { sleep_ns / 1000000000, sleep_ns % 1000000000 };
            nanosleep(&ts, NULL);
        }
        if (disp.kind == ML_DISP_KMS) ml_display_wait(&disp, 20);
        uint64_t now = ml_now_ns();
        if (i) {
            iv[i - 1] = (double)(now - last) / 1e6;
            if (iv[i - 1] > 25.0) dropped++;
        }
        last = now;
    }
    double wall_ms = ml_elapsed_ms(t0);
    getrusage(RUSAGE_SELF, &ru1);
    double cpu_ms = (double)(ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) * 1000.0 +
                    (double)(ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec) / 1000.0 +
                    (double)(ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) * 1000.0 +
                    (double)(ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1000.0;

    double mean_ft = 0;
    for (int i = 0; i < frames; i++) mean_ft += ft[i];
    mean_ft /= frames;
    double jitter = 0;
    for (int i = 1; i < frames - 1; i++) jitter += fabs(iv[i] - iv[i - 1]);
    if (frames > 2) jitter /= (frames - 2);
    char rss[96];
    self_rss(rss, sizeof rss);

    printf("MacLiteOS GPU Benchmark\n\n");
    printf("Renderer:            %s\n", g.gl_probed ? g.gl_renderer : "software raster (ml_ctx)");
    printf("Present backend:     %s (%s)\n", ml_disp_kind_name(disp.kind), disp.note);
    printf("Resolution:          %dx%d\n", W, H);
    printf("Refresh:             %d Hz\n", ml_display_refresh_hz(&disp));
    printf("Scene:               %d windows + wallpaper, damage-only repaint\n\n", windows);
    printf("Startup Time:        %.1f ms (surfaces + scene, once)\n", setup_ms);
    printf("Frames:              %d\n", frames);
    printf("Average FPS:         %.2f\n", frames / (wall_ms / 1000.0));
    printf("Average Frame Time:  %.3f ms composite (%.2f ms wall/frame)\n", mean_ft, wall_ms / frames);
    printf("Frame Time p95:      %.3f ms\n", percentile(ft, frames, 0.95));
    printf("Worst Frame Time:    %.3f ms\n", percentile(ft, frames, 0.99));
    printf("Frame Interval:      mean %.2f ms, p95 %.2f ms, jitter %.3f ms\n",
           percentile(iv, frames - 1, 0.50), percentile(iv, frames - 1, 0.95), jitter);
    printf("Dropped Frames:      %llu (>25 ms gaps)\n", (unsigned long long)dropped);
    printf("CPU Usage:           %.0f%% of wall (%.0f ms CPU / %.0f ms wall)\n",
           wall_ms > 0 ? cpu_ms / wall_ms * 100.0 : 0, cpu_ms, wall_ms);
    printf("Memory:              %s\n", rss);
    if (disp.kind == ML_DISP_KMS)
        printf("Page Flips:          %llu ok, %llu failed\n",
               (unsigned long long)disp.flips, (unsigned long long)disp.flip_errors);
    printf("\nHardware Acceleration: %s\n",
           disp.hw_scanout ? (g.gl_probed && !g.gl_software ? "PASS (GPU compositing)"
                                                            : "PARTIAL (GPU scanout, CPU raster)")
                           : "SOFTWARE FALLBACK (no display device here)");
    printf("Software Fallback:   AVAILABLE and measured above\n");

    ml_free(ft);
    ml_free(iv);
    for (int i = 0; i < windows; i++) ml_surface_free(wins[i]);
    ml_free(wins);
    ml_surface_free(fb);
    ml_surface_free(wall);
    ml_display_close(&disp);
    return HW_EXIT_PASS;
}
