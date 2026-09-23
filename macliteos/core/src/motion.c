#include "ml/motion.h"
#include <math.h>

ml_spring_params ml_motion_spring(ml_motion_preset preset)
{
    switch (preset) {
    case ML_MOTION_PRESET_SNAPPY:      return (ml_spring_params){ 520.0, 34.0, 1.0 };
    case ML_MOTION_PRESET_RESPONSIVE:  return (ml_spring_params){ 390.0, 30.0, 1.0 };
    case ML_MOTION_PRESET_SOFT:
    default:                            return (ml_spring_params){ 300.0, 26.0, 1.0 };
    }
}

ml_anim *ml_motion_start(ml_anim_engine *engine, double from, double to,
                         double velocity, ml_motion_preset preset,
                         bool reduced, bool disabled)
{
    if (!engine) return NULL;
    if (disabled)
        return ml_anim_start(engine, to, to, 0.0, ML_EASE_LINEAR);
    if (reduced)
        return ml_anim_start(engine, from, to, 0.12, ML_EASE_OUT_CUBIC);
    ml_spring_params p = ml_motion_spring(preset);
    return ml_anim_spring(engine, from, to, p.stiffness, p.damping, p.mass, velocity);
}

double ml_motion_duration(double seconds, bool reduced, bool disabled)
{
    if (seconds < 0.0) seconds = 0.0;
    if (disabled) return 0.0;
    return reduced ? fmin(seconds, 0.12) : seconds;
}
