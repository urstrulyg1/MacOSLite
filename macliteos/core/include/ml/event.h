#ifndef ML_EVENT_H
#define ML_EVENT_H
#include "ml/common.h"
#include <sys/epoll.h>

/* Event loop. Spec §13: when nothing happens, nothing runs.
 * The loop blocks in epoll_wait() with a timeout derived from the nearest armed
 * timer; there is no tick, no poll interval, and no animation timer that keeps
 * running after animations settle. Wakeup counts are exported so tests can
 * prove idleness (tests/test_idle.c). */

typedef struct ml_loop ml_loop;
typedef struct ml_source ml_source;

typedef void (*ml_fd_fn)(void *ud, uint32_t events);
typedef void (*ml_void_fn)(void *ud);

ml_loop *ml_loop_new(void);
void ml_loop_destroy(ml_loop *l);

ml_source *ml_loop_add_fd(ml_loop *l, int fd, uint32_t events, ml_fd_fn fn, void *ud);
void ml_source_set_events(ml_source *s, uint32_t events);
void ml_source_set_fn(ml_source *s, ml_fd_fn fn, void *ud);
void ml_source_destroy(ml_source *s);       /* does not close fd unless owned */
void ml_source_take_fd(ml_source *s);       /* loop closes fd on destroy */

/* timers use timerfd: they cost nothing while disarmed */
ml_source *ml_loop_add_timer(ml_loop *l, uint64_t interval_ms, bool repeat, ml_void_fn fn, void *ud);
void ml_timer_arm(ml_source *s, uint64_t interval_ms, bool repeat);
void ml_timer_disarm(ml_source *s);
uint64_t ml_timer_remaining_ms(ml_source *s);

/* deferred: runs on the next loop iteration (used to coalesce work after a
 * burst of events, e.g. 200 inotify events for one directory refresh) */
ml_source *ml_loop_add_deferred(ml_loop *l, ml_void_fn fn, void *ud);
void ml_deferred_schedule(ml_source *s);

/* idle: runs only when the loop is about to block (rare; used for one-shot
 * post-startup work such as "open the windows that were saved last session") */
ml_source *ml_loop_add_idle(ml_loop *l, ml_void_fn fn, void *ud);

ml_source *ml_loop_add_signal(ml_loop *l, int sig, ml_void_fn fn, void *ud);

int ml_loop_run(ml_loop *l);                       /* until ml_loop_quit */
int ml_loop_run_for(ml_loop *l, uint64_t max_ms);  /* test harness */
int ml_loop_run_once(ml_loop *l, int timeout_ms);  /* single epoll_wait pass */
void ml_loop_quit(ml_loop *l, int code);
void ml_loop_wakeup(ml_loop *l);                   /* thread-safe poke (eventfd) */

uint64_t ml_loop_wakeups(const ml_loop *l);        /* epoll_wait returns */
uint64_t ml_loop_blocked_ns(const ml_loop *l);     /* time spent blocked */
uint64_t ml_loop_iters(const ml_loop *l);
#endif
