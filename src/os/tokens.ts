/**
 * G1OS design tokens — the only place icon, dock, window, and motion
 * constants live. Screens read these (or the CSS variables they publish)
 * instead of inventing their own radii, springs, or spacings.
 *
 * Original G1OS values. They describe a macOS-class visual language
 * without shipping Apple artwork.
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

/** Continuous-corner squircle radius as a fraction of the icon box. */
export const ICON_CORNER_RADIUS = 0.2237;

export const ICON_SHADOW = "0 1px 1px rgba(0,0,0,0.16)";
export const ICON_PADDING = 0;

export const DOCK_SPACING = 4;
export const DOCK_PAD_X = 10;
export const DOCK_PAD_Y = 6;
export const DOCK_RADIUS = 22;
/** Influence radius in multiples of the resting icon size. */
export const DOCK_INFLUENCE = 2.55;
/** Extra gap (px) before the Trash, so the divider has a home. */
export const DOCK_DIVIDER_GAP = 16;
export const DOCK_MAGNIFICATION = 0.62;

export const WINDOW_RADIUS = 12;
export const WINDOW_SHADOW =
  "0 0 0 0.5px rgba(0,0,0,0.18), 0 22px 50px rgba(15,20,40,0.28), 0 2px 8px rgba(15,20,40,0.12)";
export const WINDOW_SHADOW_FOCUS =
  "0 0 0 0.5px rgba(0,0,0,0.22), 0 28px 70px rgba(15,20,40,0.38), 0 4px 14px rgba(15,20,40,0.16)";

export const ANIMATION_DURATION = {
  instant: 0,
  micro: 120,
  fast: 160,
  normal: 240,
  deliberate: 320,
  ambient: 640,
} as const;

export const ANIMATION_EASING = {
  spring: "cubic-bezier(0.2, 0.9, 0.3, 1)",
  out: "cubic-bezier(0.16, 1, 0.3, 1)",
  inOut: "cubic-bezier(0.4, 0, 0.2, 1)",
  quick: "cubic-bezier(0.25, 1, 0.5, 1)",
  close: "cubic-bezier(0.32, 0, 0.67, 0)",
  linear: "linear",
} as const;

/** Critically damped-ish UI spring. High stiffness, no visible bounce. */
export const SPRING_STIFFNESS = 380;
export const SPRING_DAMPING = 34;
export const SPRING_MASS = 1;

export const HOVER_SCALE = 1.035;
export const PRESS_SCALE = 0.965;

export const MOTION = {
  /** Pointer smoothing while the Dock is tracking (0–1, higher = snappier). */
  dockFollow: 0.62,
  /** Settle rate after the pointer leaves. */
  dockSettle: 0.2,
  genieScale: 0.15,
  sheetOffset: 10,
  menuOffset: 4,
  pressMs: 90,
} as const;
