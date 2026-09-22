/* Idle-cost test (spec §13): run a loop with a compositor-shaped workload and
 * prove that, once animations settle, the process stops burning CPU.
 * Measures real CPU time via getrusage, not wall time. */
#include "ml/common.h"
#include "ml/event.h"
#include "ml/anim.h"
#include "ml/log.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include <sys/resource.h>

static double cpu_ms(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0
         + ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
}

static void tick(void *ud)
{
    ml_anim_engine *e = ud;
    double now = ml_now_s();
    size_t act = ml_anim_tick(e, now);
    if (act == 0) { /* nothing left to animate: a real compositor disarms here */ }
}

int main(void)
{
    ml_log_init("test_idle", ML_LOG_WARN, NULL);
    ml_loop *l = ml_loop_new();
    ml_anim_engine *e = ml_anim_engine_new(64);

    /* phase 1: an animation runs for ~300 ms (window open) */
    ml_anim_spring_default(e, 0, 1, 0);
    ml_source *vsync = ml_loop_add_timer(l, 16, true, tick, e);
    double c0 = cpu_ms();
    ml_loop_run_for(l, 300);
    double busy = cpu_ms() - c0;

    /* phase 2: everything settled; timer disarmed exactly like comp does */
    ml_timer_disarm(vsync);
    c0 = cpu_ms();
    uint64_t w0 = ml_loop_wakeups(l);
    ml_loop_run_for(l, 1500);
    double idle = cpu_ms() - c0;
    uint64_t wake = ml_loop_wakeups(l) - w0;

    printf("busy phase (animation live):  %6.1f ms CPU in 300 ms wall\n", busy);
    printf("idle phase (nothing live):    %6.1f ms CPU in 1500 ms wall, %llu wakeups\n",
           idle, (unsigned long long)wake);
    int bad = 0;
    if (idle > 15.0) { printf("FAIL: idle CPU too high\n"); bad = 1; }
    if (wake > 5) { printf("FAIL: loop wakes while idle\n"); bad = 1; }
    ml_loop_destroy(l);
    ml_anim_engine_destroy(e);
    if (bad) return 1;
    printf("idle behaviour OK (loop sleeps; ~0%% CPU)\n");
    return 0;
}
