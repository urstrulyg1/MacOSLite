/**
 * G1OS Centralized Motion & Animation Engine
 * 
 * Provides authentic Apple macOS cubic-bezier curves, calibrated duration tokens,
 * interruptible transition generators, and hardware-adaptive motion profiles
 * specifically tuned for classic hardware (iMac Mid-2010 / RV730 GPU).
 */

import type { VisualMode } from "./os";

/* ------------------------------------------------------------------ */
/* 1. Authentic macOS Cubic-Bezier Curves                              */
/* ------------------------------------------------------------------ */

export const EASING = {
  // Apple standard fluid spring (windows, modals, sheets, zoom)
  appleSpring: "cubic-bezier(0.2, 0.9, 0.3, 1.0)",

  // Apple deceleration curve (menus, popovers, tooltips, toasts)
  appleEaseOut: "cubic-bezier(0.16, 1.0, 0.3, 1.0)",

  // Apple smooth symmetry curve (fullscreen morph, desktop crossfade)
  appleEaseInOut: "cubic-bezier(0.4, 0.0, 0.2, 1.0)",

  // Apple micro-interaction curve (buttons, hovers, slider thumb, toggles)
  appleQuick: "cubic-bezier(0.25, 1.0, 0.5, 1.0)",

  // Apple closing acceleration curve (dismissal, fast minimize departure)
  appleClose: "cubic-bezier(0.3, 0.0, 0.8, 0.15)",

  // Standard linear (for progress tickers only)
  linear: "linear",
} as const;

/* ------------------------------------------------------------------ */
/* 2. macOS Interaction Timing Tokens (in milliseconds)                */
/* ------------------------------------------------------------------ */

export const DURATION = {
  // Instantaneous (used in Performance Mode & dragging/resizing)
  instant: 0,

  // Micro-feedback (buttons, active press, hover state)
  micro: 120,

  // Fast transitions (menus, dropdowns, tooltips, tags)
  fast: 180,

  // Standard interactive transitions (window open/close, dialogs, sheets)
  normal: 260,

  // Deliberate spatial transitions (minimize to dock, maximize to fullscreen)
  deliberate: 340,

  // Ambient transitions (boot morph, login crossfade, wallpaper settle)
  ambient: 700,
} as const;

/* ------------------------------------------------------------------ */
/* 3. Transition Generator with Hardware & Accessibility Awareness     */
/* ------------------------------------------------------------------ */

export interface TransitionOpts {
  property?: string;
  duration?: keyof typeof DURATION | number;
  easing?: keyof typeof EASING | string;
  delay?: number;
  visualMode?: VisualMode;
  reduceMotion?: boolean;
}

/**
 * Returns a CSS transition property string calibrated to the user's
 * hardware capabilities and accessibility settings.
 */
export function getMotionTransition({
  property = "all",
  duration = "normal",
  easing = "appleEaseOut",
  delay = 0,
  visualMode = "balanced",
  reduceMotion = false,
}: TransitionOpts = {}): string {
  // If Reduce Motion is enabled or Performance Mode is selected, collapse movement
  if (reduceMotion || visualMode === "performance") {
    if (property.includes("transform") || property.includes("left") || property.includes("top")) {
      return "none";
    }
    // Simple fast opacity crossfade only
    return `${property} 120ms ease-out`;
  }

  const durMs = typeof duration === "number" ? duration : DURATION[duration];
  const curve = easing in EASING ? EASING[easing as keyof typeof EASING] : easing;
  const delayStr = delay > 0 ? ` ${delay}ms` : "";

  return `${property} ${durMs}ms ${curve}${delayStr}`;
}

/**
 * Common combined transition strings for elements
 */
export const MOTION_PRESETS = {
  // Windows
  windowOpen: `transform 260ms ${EASING.appleSpring}, opacity 220ms ${EASING.appleEaseOut}, box-shadow 260ms ease-out`,
  windowClose: `transform 180ms ${EASING.appleClose}, opacity 180ms ease-out`,
  windowMinimize: `transform 320ms ${EASING.appleSpring}, opacity 260ms ease-out, filter 260ms ease-out`,
  windowGeometry: `left 240ms ${EASING.appleSpring}, top 240ms ${EASING.appleSpring}, width 240ms ${EASING.appleSpring}, height 240ms ${EASING.appleSpring}, border-radius 240ms ease-out`,

  // Menus & Popovers
  menuPop: `transform 150ms ${EASING.appleEaseOut}, opacity 140ms ease-out`,

  // Dialogs & Modals
  dialogCard: `transform 260ms ${EASING.appleSpring}, opacity 220ms ease-out`,
  backdropDim: `opacity 240ms ease-out, backdrop-filter 240ms ease-out`,

  // Controls
  buttonPress: `transform 100ms ${EASING.appleQuick}, opacity 100ms ease-out, background-color 140ms ease-out`,
  toggleSwitch: `transform 180ms ${EASING.appleSpring}, background-color 180ms ease-out`,

  // Notifications
  toastIn: `transform 280ms ${EASING.appleSpring}, opacity 220ms ease-out`,
  toastOut: `transform 200ms ${EASING.appleClose}, opacity 180ms ease-out`,
  toastShift: `transform 240ms ${EASING.appleSpring}`,
} as const;
