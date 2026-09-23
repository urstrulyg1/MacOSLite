#ifndef MICA_MOTION_H
#define MICA_MOTION_H

#include "ml/anim.h"

/* Shared motion presets.  The compositor remains time based and can degrade
 * to a short cubic transition or an instant state change on weak hardware. */
typedef enum {
    ML_MOTION_PRESET_SOFT = 0,
    ML_MOTION_PRESET_RESPONSIVE,
    ML_MOTION_PRESET_SNAPPY
} ml_motion_preset;

typedef struct {
    double stiffness;
    double damping;
    double mass;
} ml_spring_params;

ml_spring_params ml_motion_spring(ml_motion_preset preset);
ml_anim *ml_motion_start(ml_anim_engine *engine, double from, double to,
                         double velocity, ml_motion_preset preset,
                         bool reduced, bool disabled);

double ml_motion_duration(double seconds, bool reduced, bool disabled);

#endif
