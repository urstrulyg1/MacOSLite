/* maclite-performance — the one-screen system view (spec §41). */
#include "perf_common.h"
#include "../hardware/hwprobe.h"
#include <unistd.h>

int main(int argc, char **argv)
{
    int sample_ms = argc > 1 ? atoi(argv[1]) : 800;
    cpu_sample a, b;
    cpu_sample_take(&a);
    /* disk + net baseline */
    char *d0 = ml_read_file("/proc/diskstats", NULL);
    char *n0 = ml_read_file("/proc/net/dev", NULL);
    usleep((useconds_t)sample_ms * 1000);
    cpu_sample_take(&b);
    char *d1 = ml_read_file("/proc/diskstats", NULL);
    char *n1 = ml_read_file("/proc/net/dev", NULL);

    mica_gpu_info g; mica_cpu_info c; mica_mem_info m;
    mica_gpu_probe(&g); mica_cpu_probe(&c); mica_mem_probe(&m);

    printf("MacLiteOS performance\n");
    printf("  CPU:            %s\n", c.model);
    printf("                  %.1f%% busy over %d ms (%u cores / %u threads)\n",
           cpu_percent(&a, &b), sample_ms, c.cores, c.threads);
    printf("  RAM:            %llu MB total, %llu MB available\n", m.total_kb / 1024, m.avail_kb / 1024);
    printf("  GPU:            %s\n", g.present ? g.device : "(none detected)");
    printf("                  driver=%s kms=%d\n", g.driver[0] ? g.driver : "-", g.has_kms);
    printf("  VRAM:           %s\n", g.vram_bytes ? "reported by driver" : "unknown (sysfs silent pre-bind)");
    printf("  Acceleration:   %s\n", mica_gpu_accel_name(&g));

    msg_stats st;
    if (comp_stats(&st)) {
        printf("  Compositor:     %.2f fps, %u us mean frame, %u us worst, %llu dropped\n",
               st.fps_x100 / 100.0, st.frame_us, st.worst_us, (unsigned long long)st.dropped);
        printf("  Frame damage:   %u px on last present (screen %dx%d)\n",
               st.damage_px_last, st.screen_w, st.screen_h);
        printf("  Mode:           %s   windows=%u clients=%u workspace=%u/%u\n",
               mica_mode_name(st.mode), st.nwindows, st.nclients, st.cur_ws + 1, st.n_ws);
    } else {
        printf("  Compositor:     no live session\n");
    }
    /* I/O deltas */
    unsigned long long rs0 = 0, rs1 = 0, ws0 = 0, ws1 = 0;
    if (d0 && d1) {
        char *l0 = strtok(d0, "\n"), *l1 = strtok(d1, "\n");
        while (l0 && l1) {
            unsigned long long r0, w0, r2, w2;
            unsigned int maj, min;
            char nm[64];
            if (sscanf(l0, "%u %u %63s %*d %*d %llu %*d %*d %llu", &maj, &min, nm, &r0, &w0) == 5 &&
                sscanf(l1, "%u %u %63s %*d %*d %llu %*d %*d %llu", &maj, &min, nm, &r2, &w2) == 5) {
                if (maj == 8 || maj == 259 || maj == 3) { rs0 += r0; ws0 += w0; rs1 += r2; ws1 += w2; }
            }
            l0 = strtok(NULL, "\n"); l1 = strtok(NULL, "\n");
        }
        printf("  Disk I/O:       %llu sectors read, %llu written in %d ms\n",
               rs1 - rs0, ws1 - ws0, sample_ms);
    }
    if (n0 && n1) {
        unsigned long long rb0 = 0, rb1 = 0, tb0 = 0, tb1 = 0;
        char *l = strtok(n0, "\n");
        while (l) { if (strstr(l, ":") && !strstr(l, "lo:")) { unsigned long long r, t2; sscanf(strchr(l, ':') + 1, "%llu %*d %*d %*d %*d %*d %*d %*d %llu", &r, &t2); rb0 += r; tb0 += t2; } l = strtok(NULL, "\n"); }
        l = strtok(n1, "\n");
        while (l) { if (strstr(l, ":") && !strstr(l, "lo:")) { unsigned long long r, t2; sscanf(strchr(l, ':') + 1, "%llu %*d %*d %*d %*d %*d %*d %*d %llu", &r, &t2); rb1 += r; tb1 += t2; } l = strtok(NULL, "\n"); }
        printf("  Network:        %llu B rx, %llu B tx in %d ms\n", rb1 - rb0, tb1 - tb0, sample_ms);
    }
    printf("  Media decoder:  idle (no playback); hw decode path: %s\n", mica_gpu_accel_name(&g));
    printf("  Audio:          %s\n", ml_file_exists("/proc/asound/cards") ? "ALSA present" : "none");
    ml_free(d0); ml_free(d1); ml_free(n0); ml_free(n1);
    return 0;
}
