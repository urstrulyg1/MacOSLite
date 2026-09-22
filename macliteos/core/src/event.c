#include "ml/event.h"
#include "ml/log.h"
#include "ml/util.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>

typedef enum { SRC_FD, SRC_TIMER, SRC_DEFER, SRC_IDLE, SRC_SIGNAL } src_kind;

struct ml_source {
    ml_loop *loop;
    src_kind kind;
    int fd;
    bool owns_fd;
    bool armed;
    bool repeat;
    bool deferred_pending;
    bool destroyed;
    uint32_t events;
    ml_fd_fn fdfn;
    ml_void_fn fn;
    void *ud;
    uint64_t deadline_ns;      /* timers/deferred */
    struct itimerspec spec;
};

struct ml_loop {
    int ep;
    int wake_fd;
    struct ml_source **src;
    size_t nsrc, capsrc;
    bool quit;
    int quit_code;
    uint64_t wakeups, blocked_ns, iters;
    sigset_t orig_mask;
    bool sigmask_saved;
};

static void loop_add_src(ml_loop *l, ml_source *s)
{
    if (l->nsrc == l->capsrc) {
        l->capsrc = l->capsrc ? l->capsrc * 2 : 16;
        l->src = ml_realloc(l->src, l->capsrc * sizeof *l->src);
    }
    l->src[l->nsrc++] = s;
}
static void loop_del_src(ml_loop *l, ml_source *s)
{
    for (size_t i = 0; i < l->nsrc; i++)
        if (l->src[i] == s) {
            memmove(&l->src[i], &l->src[i + 1], (l->nsrc - i - 1) * sizeof *l->src);
            l->nsrc--;
            return;
        }
}
static ml_source *src_new(ml_loop *l, src_kind k)
{
    ml_source *s = ml_zalloc(sizeof *s);
    s->loop = l;
    s->kind = k;
    s->fd = -1;
    loop_add_src(l, s);
    return s;
}
static void src_epoll_update(ml_source *s)
{
    if (s->destroyed || s->fd < 0) return;
    struct epoll_event ev = { .events = s->events, .data.ptr = s };
    if (epoll_ctl(s->loop->ep, s->events ? EPOLL_CTL_ADD : EPOLL_CTL_DEL, s->fd, &ev) != 0) {
        if (errno == EEXIST && s->events)
            epoll_ctl(s->loop->ep, EPOLL_CTL_MOD, s->fd, &ev);
        else if (errno != ENOENT)
            ML_WARN("epoll_ctl fd=%d failed: %s", s->fd, strerror(errno));
    }
}

ml_loop *ml_loop_new(void)
{
    ml_loop *l = ml_zalloc(sizeof *l);
    l->ep = epoll_create1(EPOLL_CLOEXEC);
    if (l->ep < 0) { ML_ERR("epoll_create1: %s", strerror(errno)); abort(); }
    l->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    ml_source *s = src_new(l, SRC_FD);
    s->fd = l->wake_fd;
    s->owns_fd = true;
    s->events = EPOLLIN;
    s->fdfn = NULL;
    src_epoll_update(s);
    return l;
}

void ml_loop_destroy(ml_loop *l)
{
    if (!l) return;
    for (size_t i = 0; i < l->nsrc; i++) {
        ml_source *s = l->src[i];
        if (s->owns_fd && s->fd >= 0) close(s->fd);
        ml_free(s);
    }
    ml_free(l->src);
    if (l->sigmask_saved) sigprocmask(SIG_SETMASK, &l->orig_mask, NULL);
    if (l->ep >= 0) close(l->ep);
    ml_free(l);
}

ml_source *ml_loop_add_fd(ml_loop *l, int fd, uint32_t events, ml_fd_fn fn, void *ud)
{
    ml_source *s = src_new(l, SRC_FD);
    s->fd = fd;
    s->events = events;
    s->fdfn = fn;
    s->ud = ud;
    s->armed = true;
    src_epoll_update(s);
    return s;
}
void ml_source_set_events(ml_source *s, uint32_t events)
{
    bool had = s->events != 0;
    s->events = events;
    if (s->fd < 0) return;
    if (events && !had) src_epoll_update(s);
    else if (!events && had) { epoll_ctl(s->loop->ep, EPOLL_CTL_DEL, s->fd, NULL); }
    else if (events) src_epoll_update(s);
}
void ml_source_set_fn(ml_source *s, ml_fd_fn fn, void *ud) { s->fdfn = fn; s->ud = ud; }
void ml_source_take_fd(ml_source *s) { s->owns_fd = true; }

void ml_source_destroy(ml_source *s)
{
    if (!s || s->destroyed) return;
    s->destroyed = true;
    if (s->fd >= 0) {
        epoll_ctl(s->loop->ep, EPOLL_CTL_DEL, s->fd, NULL);
        if (s->owns_fd) close(s->fd);
    }
    loop_del_src(s->loop, s);
    ml_free(s);
}

void ml_timer_arm(ml_source *s, uint64_t interval_ms, bool repeat)
{
    if (!s || s->fd < 0) return;
    struct itimerspec spec = { 0 };
    if (interval_ms == 0) interval_ms = 1;
    spec.it_value.tv_sec = (time_t)(interval_ms / 1000);
    spec.it_value.tv_nsec = (long)((interval_ms % 1000) * 1000000L);
    if (repeat) spec.it_interval = spec.it_value;
    if (timerfd_settime(s->fd, 0, &spec, NULL) != 0) { ML_WARN("timerfd_settime: %s", strerror(errno)); return; }
    s->spec = spec;
    s->repeat = repeat;
    s->armed = true;
    s->deadline_ns = ml_now_ns() + interval_ms * 1000000ull;
    if (!s->events) ml_source_set_events(s, EPOLLIN);
}
void ml_timer_disarm(ml_source *s)
{
    if (!s || s->fd < 0) return;
    struct itimerspec zero = { 0 };
    timerfd_settime(s->fd, 0, &zero, NULL);
    s->armed = false;
    s->deadline_ns = 0;
}
uint64_t ml_timer_remaining_ms(ml_source *s)
{
    if (!s || !s->armed || !s->deadline_ns) return 0;
    uint64_t now = ml_now_ns();
    return s->deadline_ns > now ? (s->deadline_ns - now) / 1000000ull : 0;
}

ml_source *ml_loop_add_timer(ml_loop *l, uint64_t interval_ms, bool repeat, ml_void_fn fn, void *ud)
{
    ml_source *s = src_new(l, SRC_TIMER);
    s->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (s->fd < 0) { ML_ERR("timerfd_create: %s", strerror(errno)); ml_source_destroy(s); return NULL; }
    s->owns_fd = true;
    s->fn = fn;
    s->ud = ud;
    if (interval_ms) ml_timer_arm(s, interval_ms, repeat);
    return s;
}

ml_source *ml_loop_add_deferred(ml_loop *l, ml_void_fn fn, void *ud)
{
    ml_source *s = src_new(l, SRC_DEFER);
    s->fn = fn;
    s->ud = ud;
    return s;
}
void ml_deferred_schedule(ml_source *s)
{
    if (!s || s->deferred_pending) return;
    s->deferred_pending = true;
    ml_loop_wakeup(s->loop);
}

ml_source *ml_loop_add_idle(ml_loop *l, ml_void_fn fn, void *ud)
{
    ml_source *s = src_new(l, SRC_IDLE);
    s->fn = fn;
    s->ud = ud;
    s->armed = true;
    return s;
}

ml_source *ml_loop_add_signal(ml_loop *l, int sig, ml_void_fn fn, void *ud)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    if (!l->sigmask_saved) { sigprocmask(SIG_BLOCK, &mask, &l->orig_mask); l->sigmask_saved = true; }
    else sigprocmask(SIG_BLOCK, &mask, NULL);
    ml_source *s = src_new(l, SRC_SIGNAL);
    s->fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (s->fd < 0) { ML_WARN("signalfd: %s", strerror(errno)); ml_source_destroy(s); return NULL; }
    s->owns_fd = true;
    s->events = EPOLLIN;
    s->fn = fn;
    s->ud = ud;
    src_epoll_update(s);
    return s;
}

void ml_loop_wakeup(ml_loop *l)
{
    uint64_t one = 1;
    ssize_t r = write(l->wake_fd, &one, sizeof one);
    (void)r;
}
void ml_loop_quit(ml_loop *l, int code) { l->quit = true; l->quit_code = code; ml_loop_wakeup(l); }
uint64_t ml_loop_wakeups(const ml_loop *l) { return l->wakeups; }
uint64_t ml_loop_blocked_ns(const ml_loop *l) { return l->blocked_ns; }
uint64_t ml_loop_iters(const ml_loop *l) { return l->iters; }

static int timeout_for(ml_loop *l)
{
    /* idle sources and pending deferred work mean: don't block at all */
    for (size_t i = 0; i < l->nsrc; i++)
        if ((l->src[i]->kind == SRC_IDLE && l->src[i]->armed) || l->src[i]->deferred_pending) return 0;
    uint64_t best = 0;
    for (size_t i = 0; i < l->nsrc; i++) {
        ml_source *s = l->src[i];
        if (s->kind != SRC_TIMER || !s->armed || !s->deadline_ns) continue;
        uint64_t now = ml_now_ns();
        uint64_t rem = s->deadline_ns > now ? (s->deadline_ns - now) / 1000000ull : 0;
        if (!best || rem < best) best = rem;
    }
    return best ? (int)ML_CLAMP(best, 1, 60000) : -1;   /* -1 = sleep forever */
}

static void dispatch(ml_source *s, uint32_t events)
{
    switch (s->kind) {
    case SRC_FD:
        if (s->fdfn) s->fdfn(s->ud, events);
        break;
    case SRC_TIMER: {
        uint64_t exp = 0;
        ssize_t r = read(s->fd, &exp, sizeof exp);
        (void)r;
        if (!s->repeat) { s->armed = false; s->deadline_ns = 0; }
        else if (s->spec.it_interval.tv_sec || s->spec.it_interval.tv_nsec)
            s->deadline_ns = ml_now_ns() +
                (uint64_t)s->spec.it_interval.tv_sec * 1000000000ull +
                (uint64_t)s->spec.it_interval.tv_nsec;
        if (s->fn) s->fn(s->ud);
        break;
    }
    case SRC_SIGNAL: {
        struct signalfd_siginfo si;
        while (read(s->fd, &si, sizeof si) == (ssize_t)sizeof si)
            if (s->fn) s->fn(s->ud);
        break;
    }
    case SRC_DEFER:
    case SRC_IDLE:
        break;
    }
}

static int run_pass(ml_loop *l, int timeout_ms)
{
    struct epoll_event evs[32];
    uint64_t t0 = ml_now_ns();
    int n = epoll_wait(l->ep, evs, (int)ML_ARRAY_SIZE(evs), timeout_ms);
    uint64_t t1 = ml_now_ns();
    l->wakeups++;
    if (n < 0) {
        if (errno == EINTR) return 0;
        ML_ERR("epoll_wait: %s", strerror(errno));
        return -1;
    }
    l->blocked_ns += (t1 - t0);
    l->iters++;

    /* deferred / idle work first: they may arm timers the events depend on */
    for (size_t i = 0; i < l->nsrc; i++) {
        ml_source *s = l->src[i];
        if (s->deferred_pending) {
            s->deferred_pending = false;
            if (s->fn) s->fn(s->ud);
        }
    }
    if (n == 0) {
        for (size_t i = 0; i < l->nsrc; i++) {
            ml_source *s = l->src[i];
            if (s->kind == SRC_IDLE && s->armed) {
                s->armed = false;
                if (s->fn) s->fn(s->ud);
            }
        }
    }
    /* drain wake_fd */
    for (int i = 0; i < n; i++) {
        ml_source *s = evs[i].data.ptr;
        if (!s || s->destroyed) continue;
        if (s->fd == l->wake_fd && s->kind == SRC_FD && !s->fdfn) {
            uint64_t v;
            ssize_t r = read(l->wake_fd, &v, sizeof v);
            (void)r;
            continue;
        }
        dispatch(s, evs[i].events);
        if (l->quit) break;
    }
    return n;
}

int ml_loop_run_once(ml_loop *l, int timeout_ms) { return l->quit ? -1 : run_pass(l, timeout_ms); }

int ml_loop_run(ml_loop *l)
{
    l->quit = false;
    while (!l->quit) {
        int to = timeout_for(l);
        if (run_pass(l, to) < 0) break;
    }
    return l->quit_code;
}

int ml_loop_run_for(ml_loop *l, uint64_t max_ms)
{
    uint64_t end = ml_now_ns() + max_ms * 1000000ull;
    l->quit = false;
    while (!l->quit) {
        uint64_t now = ml_now_ns();
        if (now >= end) break;
        int to = timeout_for(l);
        uint64_t rem_ns = end - now;
        /* round the tail UP: truncating to 0 ms would spin epoll_wait(0)
         * through the sub-millisecond remainder (measured: thousands of
         * pointless wakeups per run_for). */
        uint64_t left = (rem_ns + 999999ull) / 1000000ull;
        if (to < 0 || (uint64_t)to > left) to = (int)left;
        if (run_pass(l, to) < 0) break;
    }
    return l->quit_code;
}
