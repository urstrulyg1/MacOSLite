#ifndef ML_TOKENS_H
#define ML_TOKENS_H
/*
 * G1OS design tokens (native shell).
 *
 * Same vocabulary as the session demo's src/os/tokens.ts so a change to
 * the motion system is a change in one place per runtime, not a scatter
 * of magic numbers. These are G1OS constants — not Apple assets.
 *
 * Durations are milliseconds. Springs are stiffness / damping / mass.
 * The compositor still sleeps when no spring is active.
 */

#define G1_ICON_CORNER_RATIO   0.2237
#define G1_DOCK_SPACING        6.0
#define G1_DOCK_INFLUENCE      2.55     /* multiples of DOCK_BASE */
#define G1_DOCK_MAG_BEAUTIFUL  28.0     /* extra pixels at the pointer */
#define G1_DOCK_MAG_BALANCED   20.0
#define G1_DOCK_MAG_PERF       10.0     /* still continuous, just smaller */

#define G1_SPRING_STIFFNESS    380.0
#define G1_SPRING_DAMPING      34.0
#define G1_SPRING_MASS         1.0

#define G1_DUR_MICRO_MS        120
#define G1_DUR_FAST_MS         160
#define G1_DUR_NORMAL_MS       240
#define G1_DUR_DELIBERATE_MS   320

#define G1_WINDOW_RADIUS       12.0
#define G1_HOVER_SCALE         1.035
#define G1_PRESS_SCALE         0.965

#endif
