#ifndef ML_ANIM_H
#define ML_ANIM_H
#include "ml/common.h"

/* Animation engine (spec §39).
 *
 * Properties:
 *  - time based, never frame-count based  => stable look at any refresh rate
 *  - analytic springs                     => exact value AND velocity at any t,
 *                                            which is what makes animations
 *                                            interruptible (§27): retargeting
 *                                            continues from the current state
 *                                            instead of restarting.
 *  - fixed pool, zero allocation per frame
 *  - auto-settle: an animation that has visually converged marks itself done so
 *                                            the compositor can go back to sleep.
 */

typedef enum {
    ML_EASE_LINEAR = 0,
    ML_EASE_OUT_QUAD,
    ML_EASE_OUT_CUBIC,
    ML_EASE_IN_OUT_CUBIC,
    ML_EASE_OUT_QUINT,
    ML_EASE_IN_OUT_QUINT,
    ML_EASE_OUT_EXPO,
    ML_EASE_IN_OUT_EXPO,
    ML_EASE_OUT_BACK,
    ML_EASE_IN_OUT_BACK,
    ML_EASE_ELASTIC_OUT,
    ML_EASE_SPRING,          /* uses spring params instead of duration */
    ML_EASE_COUNT
} ml_ease;

double ml_ease_apply(ml_ease e, double t);
const char *ml_ease_name(ml_ease e);

typedef struct ml_anim ml_anim;
typedef struct ml_anim_engine ml_anim_engine;

/* NOTE: in the done callback the anim handle is already released back to the
 * pool, so `a` is passed as NULL; use `ud` for your state. */
typedef void (*ml_anim_done_fn)(ml_anim *a, void *ud);
typedef void (*ml_anim_frame_fn)(ml_anim *a, void *ud);

ml_anim_engine *ml_anim_engine_new(size_t pool_size);
void ml_anim_engine_destroy(ml_anim_engine *e);

/* duration based */
ml_anim *ml_anim_start(ml_anim_engine *e, double from, double to, double dur_s, ml_ease ease);
/* spring based: stiffness [N/m], damping [N·s/m], mass [kg] */
ml_anim *ml_anim_spring(ml_anim_engine *e, double from, double to,
                        double stiffness, double damping, double mass, double velocity);
/* macOS-ish defaults used across the shell */
ml_anim *ml_anim_spring_default(ml_anim_engine *e, double from, double to, double velocity);

double ml_anim_value(const ml_anim *a);
double ml_anim_velocity(const ml_anim *a);
double ml_anim_progress(const ml_anim *a);
bool ml_anim_active(const ml_anim *a);
bool ml_anim_is_spring(const ml_anim *a);
void *ml_anim_ud(const ml_anim *a);
void ml_anim_set_ud(ml_anim *a, void *ud);
void ml_anim_on_done(ml_anim *a, ml_anim_done_fn fn, void *ud);
void ml_anim_on_frame(ml_anim *a, ml_anim_frame_fn fn, void *ud);

/* Interruptible retarget: keeps current value + velocity, re-solves to `to`. */
void ml_anim_retarget(ml_anim *a, double to);
void ml_anim_stop(ml_anim *a);           /* freeze at current value */
void ml_anim_cancel(ml_anim *a);         /* discard (no done callback) */
void ml_anim_finish_now(ml_anim *a);     /* jump to end, fire done callback */

/* Advance every animation to `now_s`. Returns number still active.
 * Callers use "0 active" as the signal to stop rendering (idle => no frames). */
size_t ml_anim_tick(ml_anim_engine *e, double now_s);
size_t ml_anim_active_count(const ml_anim_engine *e);
size_t ml_anim_pool_used(const ml_anim_engine *e);
uint64_t ml_anim_started_total(const ml_anim_engine *e);
uint64_t ml_anim_retarget_total(const ml_anim_engine *e);

/* ---- convenience: animate a rectangle (window geometry, dock tiles) ------- */
ml_anim *ml_anim_rect(ml_anim_engine *e, ml_rect from, ml_rect to, double dur_s, ml_ease ease);
ml_anim *ml_anim_rect_spring(ml_anim_engine *e, ml_rect from, ml_rect to, double velocity);
ml_rect ml_anim_rect_value(ml_anim *a);
void ml_anim_rect_retarget(ml_anim *a, ml_rect to);

/* ---- convenience: animate opacity+scale of a surface (window open/close) -- */
typedef struct { double opacity, scale; ml_pointd offset; } ml_xform;
ml_anim *ml_anim_xform(ml_anim_engine *e, ml_xform from, ml_xform to, double dur_s, ml_ease ease);
ml_xform ml_anim_xform_value(ml_anim *a);
#endif
