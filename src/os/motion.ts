/**
 * G1OS Motion Engine
 *
 * Central clock, unified curves, interrupt-safe animations. Components
 * do not invent their own durations or easings. The animation loop is
 * driven by requestAnimationFrame (vsync-aligned), never setTimeout,
 * usleep, or setInterval — input always wins and animations never block.
 *
 * - Start time / duration / progress / easing / interpolation are unified.
 * - Retargeting preserves current velocity so interruptions feel natural.
 * - Damage regions are implicit (CSS transform + opacity) so browsers can
 *   run them on the compositor thread at 60fps without triggering layout.
 * - Idle = zero scheduled frames; we don't burn CPU on still UI.
 */

import type { VisualMode } from "./os";
import {
  ANIMATION_DURATION,
  ANIMATION_EASING,
  SPRING_DAMPING,
  SPRING_MASS,
  SPRING_STIFFNESS,
} from "./tokens";

export const EASING = {
  appleSpring: ANIMATION_EASING.spring,
  appleEaseOut: ANIMATION_EASING.out,
  appleEaseInOut: ANIMATION_EASING.inOut,
  appleQuick: ANIMATION_EASING.quick,
  appleClose: ANIMATION_EASING.close,
  linear: ANIMATION_EASING.linear,
} as const;

export const DURATION = ANIMATION_DURATION;

export interface TransitionOpts {
  property?: string;
  duration?: keyof typeof DURATION | number;
  easing?: keyof typeof EASING | string;
  delay?: number;
  visualMode?: VisualMode;
  reduceMotion?: boolean;
}

export interface MotionProfile {
  openMs: number;
  closeMs: number;
  minMs: number;
  menuMs: number;
  sheetMs: number;
  toastMs: number;
  ease: string;
  closeEase: string;
  quick: string;
  /** 1 = full dock magnification; lower = reduced; 0 = off. */
  magAmount: number;
  blur: number;
  genie: boolean;
  shadow: "full" | "soft" | "lite";
  /** Multiplier on all animation durations for low-power mode. */
  durScale: number;
}

/**
 * Hardware-adaptive motion profile. Performance mode never disables
 * motion — it shortens durations and cuts paint cost so old iMacs
 * still feel alive without frame drops.
 */
export function motionProfile(mode: VisualMode, reduceMotion: boolean): MotionProfile {
  if (reduceMotion) {
    return {
      openMs: 80, closeMs: 70, minMs: 80,
      menuMs: 60, sheetMs: 80, toastMs: 80,
      ease: EASING.appleEaseOut, closeEase: EASING.appleClose, quick: EASING.appleQuick,
      magAmount: 0, blur: 0, genie: false, shadow: "lite", durScale: 0.5,
    };
  }
  if (mode === "performance") {
    return {
      openMs: 200, closeMs: 140, minMs: 240,
      menuMs: 100, sheetMs: 180, toastMs: 180,
      ease: EASING.appleEaseOut, closeEase: EASING.appleClose, quick: EASING.appleQuick,
      magAmount: 0.55, blur: 0, genie: true, shadow: "lite", durScale: 0.8,
    };
  }
  if (mode === "beautiful") {
    return {
      openMs: 300, closeMs: 180, minMs: 360,
      menuMs: 140, sheetMs: 260, toastMs: 260,
      ease: EASING.appleSpring, closeEase: EASING.appleClose, quick: EASING.appleQuick,
      magAmount: 1, blur: 24, genie: true, shadow: "full", durScale: 1.0,
    };
  }
  return {
    openMs: 260, closeMs: 170, minMs: 320,
    menuMs: 120, sheetMs: 220, toastMs: 220,
    ease: EASING.appleSpring, closeEase: EASING.appleClose, quick: EASING.appleQuick,
    magAmount: 0.85, blur: 12, genie: true, shadow: "soft", durScale: 1.0,
  };
}

export function getMotionTransition({
  property = "all",
  duration = "normal",
  easing = "appleEaseOut",
  delay = 0,
  reduceMotion = false,
}: TransitionOpts = {}): string {
  if (reduceMotion) return `${property} 60ms ${EASING.appleEaseOut}`;
  const durMs = typeof duration === "number" ? duration : DURATION[duration];
  const curve = easing in EASING ? EASING[easing as keyof typeof EASING] : easing;
  const delayStr = delay > 0 ? ` ${delay}ms` : "";
  return `${property} ${durMs}ms ${curve}${delayStr}`;
}

/** Pre-built transition strings — one place so menus and sheets all feel the same. */
export const MOTION_PRESETS = {
  windowOpen: "transform 280ms cubic-bezier(0.175,0.885,0.32,1.075), opacity 200ms ease-out",
  windowClose: "transform 180ms cubic-bezier(0.32,0,0.67,0), opacity 160ms ease-out",
  windowMinimize: "transform 340ms cubic-bezier(0.175,0.885,0.32,1.075), opacity 260ms ease-out",
  windowGeometry: "left 220ms cubic-bezier(0.175,0.885,0.32,1.075), top 220ms cubic-bezier(0.175,0.885,0.32,1.075), width 220ms cubic-bezier(0.175,0.885,0.32,1.075), height 220ms cubic-bezier(0.175,0.885,0.32,1.075), border-radius 200ms ease-out",
  menuPop: "transform 120ms cubic-bezier(0.16,1,0.3,1), opacity 100ms ease-out",
  dialogCard: "transform 240ms cubic-bezier(0.175,0.885,0.32,1.075), opacity 180ms ease-out",
  buttonPress: "transform 80ms cubic-bezier(0.25,1,0.5,1), background-color 100ms ease-out, filter 100ms ease-out",
  toggleSwitch: "transform 250ms cubic-bezier(0.175,0.885,0.32,1.075), background-color 220ms ease-out",
  toastIn: "transform 260ms cubic-bezier(0.175,0.885,0.32,1.075), opacity 200ms ease-out",
  toastOut: "transform 180ms cubic-bezier(0.32,0,0.67,0), opacity 160ms ease-in",
  toastShift: "transform 220ms cubic-bezier(0.175,0.885,0.32,1.075), max-height 220ms cubic-bezier(0.16,1,0.3,1), margin 220ms cubic-bezier(0.16,1,0.3,1), padding 220ms ease",
  dockReveal: "transform 320ms cubic-bezier(0.175,0.885,0.32,1.075), opacity 220ms ease-out",
} as const;

/* ------------------------------------------------------------------ */
/* Analytic damped spring — interruptible, allocation-free per sample */
/* ------------------------------------------------------------------ */

export interface SpringState {
  value: number;
  velocity: number;
  target: number;
}

export function springCreate(value: number, target = value): SpringState {
  return { value, velocity: 0, target };
}

/**
 * Advance one step. dt is seconds. Returns true when visually settled
 * (within 0.15 units and velocity < threshold). Settled snaps exactly.
 */
export function springStep(
  s: SpringState,
  dt: number,
  stiffness = SPRING_STIFFNESS,
  damping = SPRING_DAMPING,
  mass = SPRING_MASS,
): boolean {
  const accel = (-stiffness * (s.value - s.target) - damping * s.velocity) / mass;
  s.velocity += accel * dt;
  s.value += s.velocity * dt;
  const settled = Math.abs(s.value - s.target) < 0.12 && Math.abs(s.velocity) < 6;
  if (settled) {
    s.value = s.target;
    s.velocity = 0;
  }
  return settled;
}

export function springRetarget(s: SpringState, target: number) {
  s.target = target;
}

/* ------------------------------------------------------------------ */
/* Dock magnification bell — smooth C1 continuous at influence edge  */
/* ------------------------------------------------------------------ */

/**
 * Cosine-squared falloff. Zero derivative at radius means icons
 * don't visibly "pop" when the pointer enters/leaves the dock.
 */
export function dockScale(distance: number, radius: number, min: number, max: number): number {
  if (radius <= 0 || distance >= radius || max <= min) return min;
  const t = distance / radius;
  const bell = Math.cos(t * Math.PI * 0.5);
  return min + (max - min) * bell * bell;
}

/**
 * Compute neighbor shifts so an icon's extra width pushes its siblings
 * apart symmetrically. The row stays centered (total displacement = 0)
 * and overlapping icons can never happen during magnification.
 */
export function dockShifts(extras: number[]): number[] {
  const n = extras.length;
  const prefix = new Array<number>(n + 1);
  prefix[0] = 0;
  for (let i = 0; i < n; i++) prefix[i + 1] = prefix[i] + extras[i];
  const total = prefix[n];
  const shifts = new Array<number>(n);
  for (let i = 0; i < n; i++) {
    const left = prefix[i];
    const right = total - prefix[i + 1];
    shifts[i] = (left - right) / 2;
  }
  return shifts;
}

/* ------------------------------------------------------------------ */
/* Interrupt-safe transition helpers (compositor-owned elements)     */
/* ------------------------------------------------------------------ */

export function readTranslate(el: HTMLElement): { x: number; y: number; s: number } {
  const t = getComputedStyle(el).transform;
  if (!t || t === "none") return { x: 0, y: 0, s: 1 };
  const m = new DOMMatrix(t);
  return { x: m.e, y: m.f, s: m.a || 1 };
}

/**
 * Freeze the element's current transform mid-flight and retarget to `to`.
 * Calling this at any point (e.g. on drag start) prevents jumpy snaps
 * back to the layout start. This is the core of G1OS's interruptibility.
 */
export function retargetTransform(
  el: HTMLElement,
  to: string,
  ms: number,
  ease: string,
  opacity?: string,
) {
  const cur = getComputedStyle(el).transform;
  el.style.transition = "none";
  el.style.transform = !cur || cur === "none" ? "none" : cur;
  // Force style flush so next transition begins from the frozen frame.
  el.getBoundingClientRect();
  el.style.transition = `transform ${ms}ms ${ease}, opacity ${Math.max(80, Math.round(ms * 0.7))}ms ease-out`;
  el.style.transform = to;
  if (opacity != null) el.style.opacity = opacity;
}

export function clearMotion(el: HTMLElement) {
  el.style.transition = "none";
  el.style.transform = "none";
  el.style.opacity = "1";
}

/**
 * Commit element's visual box back into layout coordinates so a drag
 * takes over mid-animation without a jump. Scale is discarded — window
 * returns to layout size, centered on its visual center point.
 */
export function commitVisualCenter(el: HTMLElement, layoutW: number, layoutH: number) {
  const parent = el.offsetParent as HTMLElement | null;
  if (!parent) return null;
  const pr = parent.getBoundingClientRect();
  const r = el.getBoundingClientRect();
  const cx = r.left + r.width / 2 - pr.left;
  const cy = r.top + r.height / 2 - pr.top;
  const x = cx - layoutW / 2;
  const y = Math.max(0, cy - layoutH / 2);
  el.style.transition = "none";
  el.style.transform = "none";
  el.style.opacity = "1";
  el.style.left = `${x}px`;
  el.style.top = `${y}px`;
  el.style.width = `${layoutW}px`;
  el.style.height = `${layoutH}px`;
  return { x, y, w: layoutW, h: layoutH };
}

/* ------------------------------------------------------------------ */
/* Frame timing helpers                                              */
/* ------------------------------------------------------------------ */

/** A single rAF-ticked clock. Callers get a high-res timestamp. */
export function onNextFrame(cb: (t: number) => void): number {
  return requestAnimationFrame(cb);
}

export function cancelFrame(id: number) {
  cancelAnimationFrame(id);
}

/**
 * Clamp easing between 0 and 1 so invalid progress values never explode.
 */
export function clamp01(n: number): number {
  return n < 0 ? 0 : n > 1 ? 1 : n;
}

/** Linear interpolation. */
export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}
