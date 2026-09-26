/**
 * G1OS design tokens — single source of truth.
 * Values reflect a modern macOS (Sequoia-class) visual language without
 * distributing Apple artwork. All radii, timings, and springs originate
 * here so the entire OS reads as one coherent system.
 */

export const ICON_SIZE = {
  menubar: 15,
  sidebar: 16,
  inline: 18,
  badge: 22,
  desktop: 52,
  finder: 48,
  dock: 54,
  launch: 58,
  hero: 76,
} as const;

/** Continuous corner squircle radius as fraction of icon box. */
export const ICON_CORNER_RADIUS = 0.2237;

export const ICON_SHADOW = "0 1px 2px rgba(0,0,0,0.18)";
export const ICON_PADDING = 0;

/* Dock */
export const DOCK_SPACING = 2;
export const DOCK_PAD_X = 8;
export const DOCK_PAD_Y = 6;
export const DOCK_RADIUS = 20;
/** Influence radius in multiples of resting icon size. */
export const DOCK_INFLUENCE = 2.6;
/** Extra gap before Trash for the vertical divider. */
export const DOCK_DIVIDER_GAP = 14;
export const DOCK_MAGNIFICATION = 0.65;

/* Windows */
export const WINDOW_RADIUS = 10;
export const WINDOW_SHADOW =
  "0 0 0 0.5px rgba(0,0,0,0.18), 0 1px 2px rgba(0,0,0,0.04), 0 8px 28px rgba(0,0,0,0.16), 0 18px 48px rgba(0,0,0,0.12)";
export const WINDOW_SHADOW_FOCUS =
  "0 0 0 0.5px rgba(0,0,0,0.22), 0 1px 2px rgba(0,0,0,0.05), 0 14px 42px rgba(0,0,0,0.22), 0 26px 80px rgba(0,0,0,0.18)";

/* Animation durations in ms */
export const ANIMATION_DURATION = {
  instant: 0,
  micro: 100,
  fast: 150,
  normal: 220,
  deliberate: 300,
  ambient: 600,
  windowOpen: 280,
  windowClose: 180,
  windowMinimize: 340,
  dockReveal: 320,
  dockBounce: 660,
  sheet: 240,
  menu: 120,
} as const;

/* Apple-accurate cubic bezier easings */
export const ANIMATION_EASING = {
  /* Gentle overshoot — macOS window open */
  spring: "cubic-bezier(0.175, 0.885, 0.32, 1.075)",
  /* Pure ease out — most transitions */
  out: "cubic-bezier(0.16, 1, 0.3, 1)",
  /* Symmetric in/out */
  inOut: "cubic-bezier(0.65, 0, 0.35, 1)",
  /* Micro-interactions — buttons, switches */
  quick: "cubic-bezier(0.25, 1, 0.5, 1)",
  /* Exit curve — fast close without bounce */
  close: "cubic-bezier(0.32, 0, 0.67, 0)",
  linear: "linear",
} as const;

/** Spring constants for analytic damped spring. */
export const SPRING_STIFFNESS = 420;
export const SPRING_DAMPING = 36;
export const SPRING_MASS = 1;

/* Interaction micro-motions */
export const HOVER_SCALE = 1.05;
export const PRESS_SCALE = 0.95;

export const MOTION = {
  /** Pointer smoothing while Dock tracks (0-1, higher = snappier). */
  dockFollow: 0.72,
  /** Settle rate after pointer leaves. */
  dockSettle: 0.18,
  genieScale: 0.12,
  sheetOffset: 10,
  menuOffset: 4,
  pressMs: 80,
} as const;
