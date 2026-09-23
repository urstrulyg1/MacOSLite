/**
 * G1OS Motion Engine
 *
 * One clock, one set of curves, one way to interrupt. Components do not
 * invent their own easings. Springs are analytic so a retarget continues
 * from the current value and velocity — input always wins.
 *
 * The engine never touches disk, network, or layout outside the element
 * it was asked to move. Idle means zero animation frames.
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
  /** 1 = full dock magnification, lower = reduced, 0 = off. */
  magAmount: number;
  blur: number;
  genie: boolean;
  shadow: "full" | "soft" | "lite";
}

/** Hardware-adaptive profile. Performance stays smooth; it only drops cost. */
export function motionProfile(mode: VisualMode, reduceMotion: boolean): MotionProfile {
  if (reduceMotion) {
    return {
      openMs: 80,
      closeMs: 70,
      minMs: 80,
      menuMs: 60,
      sheetMs: 80,
      toastMs: 80,
      ease: EASING.appleEaseOut,
      closeEase: EASING.appleClose,
      quick: EASING.appleQuick,
      magAmount: 0,
      blur: 0,
      genie: false,
      shadow: "lite",
    };
  }
  if (mode === "performance") {
    return {
      openMs: 180,
      closeMs: 140,
      minMs: 220,
      menuMs: 120,
      sheetMs: 180,
      toastMs: 180,
      ease: EASING.appleEaseOut,
      closeEase: EASING.appleClose,
      quick: EASING.appleQuick,
      magAmount: 0.55,
      blur: 0,
      genie: true,
      shadow: "lite",
    };
  }
  if (mode === "beautiful") {
    return {
      openMs: 280,
      closeMs: 170,
      minMs: 340,
      menuMs: 150,
      sheetMs: 260,
      toastMs: 260,
      ease: EASING.appleSpring,
      closeEase: EASING.appleClose,
      quick: EASING.appleQuick,
      magAmount: 1,
      blur: 16,
      genie: true,
      shadow: "full",
    };
  }
  return {
    openMs: 240,
    closeMs: 160,
    minMs: 300,
    menuMs: 140,
    sheetMs: 220,
    toastMs: 220,
    ease: EASING.appleSpring,
    closeEase: EASING.appleClose,
    quick: EASING.appleQuick,
    magAmount: 0.82,
    blur: 8,
    genie: true,
    shadow: "soft",
  };
}

export function getMotionTransition({
  property = "all",
  duration = "normal",
  easing = "appleEaseOut",
  delay = 0,
  visualMode = "balanced",
  reduceMotion = false,
}: TransitionOpts = {}): string {
  const profile = motionProfile(visualMode, reduceMotion);
  if (reduceMotion) return `${property} ${profile.openMs}ms ${profile.ease}`;
  const durMs = typeof duration === "number" ? duration : DURATION[duration];
  const scaled = Math.round(durMs * (visualMode === "performance" ? 0.75 : 1));
  const curve = easing in EASING ? EASING[easing as keyof typeof EASING] : easing;
  const delayStr = delay > 0 ? ` ${delay}ms` : "";
  return `${property} ${scaled}ms ${curve}${delayStr}`;
}

export const MOTION_PRESETS = {
  windowOpen: `transform 260ms ${EASING.appleSpring}, opacity 200ms ${EASING.appleEaseOut}`,
  windowClose: `transform 160ms ${EASING.appleClose}, opacity 150ms ease-out`,
  windowMinimize: `transform 320ms ${EASING.appleSpring}, opacity 240ms ease-out`,
  windowGeometry: `left 240ms ${EASING.appleSpring}, top 240ms ${EASING.appleSpring}, width 240ms ${EASING.appleSpring}, height 240ms ${EASING.appleSpring}, border-radius 200ms ease-out`,
  menuPop: `transform 140ms ${EASING.appleEaseOut}, opacity 120ms ease-out`,
  dialogCard: `transform 240ms ${EASING.appleSpring}, opacity 180ms ease-out`,
  buttonPress: `transform 90ms ${EASING.appleQuick}, background-color 120ms ease-out`,
  toggleSwitch: `transform 180ms ${EASING.appleSpring}, background-color 180ms ease-out`,
  toastIn: `transform 240ms ${EASING.appleSpring}, opacity 180ms ease-out`,
  toastOut: `transform 180ms ${EASING.appleClose}, opacity 160ms ease-out`,
  toastShift: `transform 220ms ${EASING.appleSpring}, max-height 220ms ${EASING.appleEaseOut}, margin 220ms ${EASING.appleEaseOut}, padding 220ms ${EASING.appleEaseOut}`,
} as const;

/* ------------------------------------------------------------------ */
/* Analytic spring — interruptible, allocation-free per sample         */
/* ------------------------------------------------------------------ */

export interface SpringState {
  value: number;
  velocity: number;
  target: number;
}

export function springCreate(value: number, target = value): SpringState {
  return { value, velocity: 0, target };
}

/** Advance one step. dt is seconds. Returns true when visually settled. */
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
  const settled = Math.abs(s.value - s.target) < 0.15 && Math.abs(s.velocity) < 8;
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
/* Dock magnification — continuous, C1 at the influence edge           */
/* ------------------------------------------------------------------ */

/** Cosine-squared bell. Zero derivative at the radius, so icons don't pop. */
export function dockScale(distance: number, radius: number, min: number, max: number): number {
  if (radius <= 0 || distance >= radius || max <= min) return min;
  const t = distance / radius;
  const bell = Math.cos(t * Math.PI * 0.5);
  return min + (max - min) * bell * bell;
}

/**
 * Neighbor displacement. Each icon is pushed by half the extra width of
 * every other icon, away from that icon. The row stays centered, and a
 * growing icon never overlaps its neighbor.
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
    shifts[i] = left / 2 - right / 2;
  }
  return shifts;
}

/* ------------------------------------------------------------------ */
/* Interrupt / retarget helpers for compositor-owned elements          */
/* ------------------------------------------------------------------ */

export function readTranslate(el: HTMLElement): { x: number; y: number; s: number } {
  const t = getComputedStyle(el).transform;
  if (!t || t === "none") return { x: 0, y: 0, s: 1 };
  const m = new DOMMatrix(t);
  return { x: m.e, y: m.f, s: m.a || 1 };
}

/** Freeze the in-flight transform, then animate to `to`. Input can call this at any time. */
export function retargetTransform(el: HTMLElement, to: string, ms: number, ease: string, opacity?: string) {
  const cur = getComputedStyle(el).transform;
  el.style.transition = "none";
  el.style.transform = !cur || cur === "none" ? "none" : cur;
  // Flush so the next transition starts from the sampled frame, not the target.
  el.getBoundingClientRect();
  el.style.transition = `transform ${ms}ms ${ease}, opacity ${Math.max(80, Math.round(ms * 0.75))}ms ease-out`;
  el.style.transform = to;
  if (opacity != null) el.style.opacity = opacity;
}

export function clearMotion(el: HTMLElement) {
  el.style.transition = "none";
  el.style.transform = "none";
  el.style.opacity = "1";
}

/**
 * Commit the element's visual box back into layout coordinates so a drag
 * can take over mid-animation without a jump. Scale is discarded — the
 * window returns to its layout size, centered on the visual center.
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
