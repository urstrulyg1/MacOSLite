#include "ml/ui_theme.h"
#include <math.h>

static ml_motion_level g_motion = ML_MOTION_FULL;

const ml_ui_tokens *ml_ui_tokens_for_mode(uint32_t mode, bool dark)
{
    static ml_ui_tokens performance = { 8.0, 10.0, 12.0, 2.0, 4.0, 0.0, 0.0, 255.0, 250.0, 1.18 };
    static ml_ui_tokens balanced    = { 9.0, 12.0, 16.0, 4.0, 8.0, 4.0, 8.0, 222.0, 242.0, 1.32 };
    static ml_ui_tokens beautiful   = { 10.0, 14.0, 20.0, 6.0, 12.0, 8.0, 16.0, 188.0, 236.0, 1.45 };
    (void)dark;
    if (mode == MODE_PERFORMANCE) return &performance;
    if (mode == MODE_BEAUTIFUL) return &beautiful;
    return &balanced;
}

ml_motion_level ml_ui_motion_level(void) { return g_motion; }
void ml_ui_set_motion_level(ml_motion_level level) { g_motion = level; }

double ml_ui_motion_duration(double normal_seconds)
{
    if (normal_seconds < 0.0) normal_seconds = 0.0;
    if (g_motion == ML_MOTION_DISABLED) return 0.0;
    if (g_motion == ML_MOTION_REDUCED) return fmin(normal_seconds, 0.12);
    return normal_seconds;
}

ml_anim *ml_ui_spring(ml_anim_engine *engine, double from, double to, double velocity)
{
    if (!engine) return NULL;
    if (g_motion == ML_MOTION_DISABLED)
        return ml_anim_start(engine, to, to, 0.0, ML_EASE_LINEAR);
    if (g_motion == ML_MOTION_REDUCED)
        return ml_anim_start(engine, from, to, 0.12, ML_EASE_OUT_CUBIC);
    return ml_anim_spring_default(engine, from, to, velocity);
}

ml_color ml_ui_surface_color(bool dark, bool elevated, uint8_t alpha)
{
    if (dark) return elevated ? ml_rgba(30, 32, 42, alpha) : ml_rgba(18, 20, 28, alpha);
    return elevated ? ml_rgba(255, 255, 255, alpha) : ml_rgba(246, 248, 252, alpha);
}

ml_color ml_ui_border_color(bool dark, uint8_t alpha)
{
    return dark ? ml_rgba(255, 255, 255, alpha) : ml_rgba(0, 0, 0, alpha);
}

ml_color ml_ui_text_color(bool dark, bool secondary, uint8_t alpha)
{
    if (secondary)
        return dark ? ml_rgba(196, 201, 214, alpha) : ml_rgba(82, 88, 102, alpha);
    return dark ? ml_rgba(245, 247, 252, alpha) : ml_rgba(24, 27, 34, alpha);
}
