#ifndef MICA_UI_THEME_H
#define MICA_UI_THEME_H

#include "ml/common.h"
#include "ml/anim.h"

/* Central G1OS design tokens.  These are deliberately tiny, C-native values
 * so the shell/compositor does not need a heavyweight UI toolkit. */
typedef struct {
    double radius_sm;
    double radius_md;
    double radius_lg;
    double shadow_sm;
    double shadow_md;
    double blur_sm;
    double blur_md;
    double panel_alpha;
    double window_alpha;
    double dock_scale_max;
} ml_ui_tokens;

typedef enum {
    ML_MOTION_FULL = 0,
    ML_MOTION_REDUCED,
    ML_MOTION_DISABLED
} ml_motion_level;

const ml_ui_tokens *ml_ui_tokens_for_mode(uint32_t mode, bool dark);
ml_motion_level ml_ui_motion_level(void);
void ml_ui_set_motion_level(ml_motion_level level);

double ml_ui_motion_duration(double normal_seconds);
ml_anim *ml_ui_spring(ml_anim_engine *engine, double from, double to, double velocity);

/* Lightweight color roles shared by all G1OS surfaces. */
ml_color ml_ui_surface_color(bool dark, bool elevated, uint8_t alpha);
ml_color ml_ui_border_color(bool dark, uint8_t alpha);
ml_color ml_ui_text_color(bool dark, bool secondary, uint8_t alpha);

#endif
