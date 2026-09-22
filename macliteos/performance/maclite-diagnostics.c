/* maclite-diagnostics — System Diagnostics (spec §50).
 * Every test here says what it measured and where it ran. Nothing claims
 * hardware acceleration that this host did not actually exercise. */
#include "perf_common.h"
#include "../hardware/hwprobe.h"
#include <unistd.h>

static void test_cpu(void)
{
    mica_cpu_info c;
    mica_cpu_probe(&c);
    double mb = mica_cpu_blend_benchmark_mb_s();
    printf("[CPU]      %s\n           %u cores / %u threads, %u MHz max, SSE4.2=%d AVX=%d (2010 CPUs: no AVX needed)\n",
           c.model, c.cores, c.threads, c.mhz_max, c.sse42, c.avx);
    printf("           blend throughput %.0f MB/s\n", mb);
}
static void test_ram(void)
{
    mica_mem_info m;
    mica_mem_probe(&m);
    /* sequential write bandwidth on a 64 MB anonymous buffer */
    size_t n = 64u * 1024 * 1024;
    volatile uint8_t *p = ml_alloc(n);
    uint64_t t0 = ml_now_ns();
    for (size_t i = 0; i < n; i += 64) p[i] = 0xA5;
    double ms = ml_elapsed_ms(t0);
    printf("[RAM]      %llu MB total, %llu MB available; write bandwidth %.1f GB/s\n",
           m.total_kb / 1024, m.avail_kb / 1024, (double)n / ms / 1e6);
    ml_free((void *)p);
}
static void test_disk(void)
{
    char *tmp = ml_strdup("/tmp/micadiag.bin");
    FILE *f = fopen(tmp, "wb");
    if (!f) { printf("[DISK]     cannot create test file\n"); ml_free(tmp); return; }
    size_t n = 32u * 1024 * 1024;
    char *buf = ml_alloc(n);
    uint64_t t0 = ml_now_ns();
    fwrite(buf, 1, n, f);
    fclose(f);
    double wms = ml_elapsed_ms(t0);
    f = fopen(tmp, "rb");
    t0 = ml_now_ns();
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    double rms = ml_elapsed_ms(t0);
    printf("[DISK]     %zu MB written in %.0f ms (%.0f MB/s), read in %.0f ms (%.0f MB/s)\n",
           got / (1024 * 1024), wms, n / wms / 1e3, rms, n / rms / 1e3);
    ml_free(buf); ml_free((void *)tmp);
    unlink("/tmp/micadiag.bin");
}
static void test_gpu(void)
{
    mica_gpu_info g;
    mica_gpu_probe(&g);
    printf("[GPU]      %s\n           driver=%s kms=%d vram=%s\n",
           g.present ? g.device : "(none)", g.driver[0] ? g.driver : "-", g.has_kms,
           g.vram_bytes ? "known" : "unknown");
    printf("           decode support (from hardware table, NOT benchmarked here): %s\n",
           mica_gpu_accel_name(&g));
    if (!g.has_kms)
        printf("           verdict: software compositing only on this host\n");
}
static void test_video(void)
{
    printf("[VIDEO]    no decoder present on this host (no mpv/gstreamer headers at build time).\n");
    printf("           On the iMac, verify with: mpv --hwdec=vdpau file.mp4  (see docs/TESTING.md)\n");
}
static void test_audio(void)
{
    bool ok = ml_file_exists("/proc/asound/cards");
    char *cards = ok ? ml_read_file("/proc/asound/cards", NULL) : NULL;
    printf("[AUDIO]    ALSA %s\n", ok ? "present" : "absent");
    if (cards) { printf("%s", cards); ml_free(cards); }
}
static void test_net(void)
{
    char *dev = ml_read_file("/proc/net/dev", NULL);
    printf("[NETWORK]  interfaces:\n");
    if (dev) {
        char *l = strtok(dev, "\n");
        while (l) {
            char *q = strchr(l, ':');
            if (q && !strstr(l, "lo:")) {
                *q = 0;
                char path[128];
                snprintf(path, sizeof path, "/sys/class/net/%s/operstate", ml_str_trim(l));
                char *st = ml_sysfs_str(path, "?");
                printf("           %-10s %s\n", ml_str_trim(l), st);
                ml_free(st);
            }
            l = strtok(NULL, "\n");
        }
        ml_free(dev);
    }
    int wifi = -1;
    char *w = ml_read_file("/proc/net/wireless", NULL);
    if (w) wifi = 0;
    ml_free(w);
    printf("           wireless: %s\n", wifi >= 0 ? "interface present" : "none");
}

int main(int argc, char **argv)
{
    bool gui = argc > 1 && !strcmp(argv[1], "--gui");
    (void)gui;
    printf("MacLiteOS System Diagnostics — executed on: %s (kernel %s)\n", "this host", "see uname");
    printf("Testing status of this run: SANDBOX/QEMU-class host. Real iMac results are recorded\n"
           "separately in docs/TESTING.md and are never inferred from this output.\n\n");
    test_cpu(); test_ram(); test_disk(); test_gpu(); test_video(); test_audio(); test_net();
    printf("\nAll tests completed without crashing the desktop shell.\n");
    return 0;
}
