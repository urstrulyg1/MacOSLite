import React, { useCallback, useLayoutEffect, useMemo, useRef, useState } from "react";
import { APPS, DOCK_ORDER, useOS, type AppId } from "./os";
import { G1Icon, FinderSquircle as FinderFace } from "./icons/IconSystem";
import { setDockTarget } from "./dockRegistry";
import { dockScale, dockShifts, motionProfile } from "./motion";
import { DOCK_DIVIDER_GAP, DOCK_INFLUENCE, DOCK_PAD_X, DOCK_SPACING } from "./tokens";

export { FinderFace };

interface ItemMeta {
  key: string;
  kind: "app" | "trash";
  appId?: AppId;
  label: string;
}

/**
 * G1OS Dock — macOS-style floating shelf.
 *
 * Design goals:
 *  - One visible cursor: the browser paints the hardware cursor only;
 *    we never paint our own cursor image. Pointer events are managed
 *    carefully so hit-testing never overlaps or lags behind the real
 *    cursor position (no trails, no multiple hot spots).
 *  - Magnification is computed in a single rAF loop that only runs while
 *    the pointer is over the dock or the row is settling. Each icon is
 *    moved via translate3d (compositor-only) so 60fps holds during
 *    rapid movement.
 *  - The shell width is constant (sized for peak magnification), so the
 *    pointer-coordinate origin never moves — scales can't feedback into
 *    the coordinate they were computed from, which eliminates jitter.
 *  - Autohide animates the entire shell with a spring easing; no layout
 *    thrash.
 *  - Running indicator dots track with their icon via shared translate.
 */
export default function Dock() {
  const os = useOS();
  const { dock, visualMode, reduceMotion } = os;
  const vertical = dock.pos !== "bottom";
  const profile = motionProfile(visualMode, reduceMotion);

  const base = dock.size || 54;
  const gap = DOCK_SPACING;
  const maxScale = dock.mag ? 1 + Math.max(0, dock.magScale) * profile.magAmount : 1;
  const radius = base * DOCK_INFLUENCE;

  const shellRef = useRef<HTMLDivElement>(null);
  const glassRef = useRef<HTMLDivElement>(null);
  const btnRefs = useRef<(HTMLButtonElement | null)[]>([]);
  const dotRefs = useRef<(HTMLSpanElement | null)[]>([]);
  const bounceRefs = useRef<(HTMLSpanElement | null)[]>([]);
  const divRef = useRef<HTMLDivElement>(null);
  const tipRef = useRef<HTMLDivElement>(null);
  const mouseRef = useRef<number | null>(null);
  const smoothRef = useRef<number | null>(null);
  const scalesRef = useRef<number[]>([]);
  const rafRef = useRef(0);
  const liveRef = useRef(false);
  const hoverRef = useRef(-1);
  const tipTimeoutRef = useRef(0);
  const [revealed, setRevealed] = useState(!dock.autohide);

  const running = useMemo(() => new Set(os.wins.map((w) => w.app)), [os.wins]);
  const trashCount = (os.fs.Trash || []).length;

  const items: ItemMeta[] = useMemo(() => {
    const list: ItemMeta[] = DOCK_ORDER.map((app) => ({
      key: app,
      kind: "app" as const,
      appId: app,
      label: APPS[app]?.name ?? app,
    }));
    list.push({ key: "trash", kind: "trash", label: trashCount ? `Trash, ${trashCount} items` : "Trash" });
    return list;
  }, [trashCount]);

  const trashIndex = items.length - 1;

  /** X-position (along main axis) of an icon's resting center. */
  const restingCenter = useCallback(
    (i: number) => {
      let x = DOCK_PAD_X + base / 2;
      for (let j = 0; j < i; j++) {
        x += base + gap;
        if (j === trashIndex - 1) x += DOCK_DIVIDER_GAP;
      }
      return x;
    },
    [base, gap, trashIndex],
  );

  /** Left edge (along main axis) of an icon at rest. */
  const restingLeft = useCallback(
    (i: number) => restingCenter(i) - base / 2,
    [restingCenter, base],
  );

  const contentWidth = restingLeft(Math.max(0, items.length - 1)) + base;
  const maxGrow = items.length * base * Math.max(0, maxScale - 1);
  const shellMain = contentWidth + maxGrow + DOCK_PAD_X * 2;
  const shellCross = base + 20;

  /** Apply computed scales/shifts directly to element styles (no React state in the hot loop). */
  const apply = useCallback(
    (scales: number[]) => {
      const extras = scales.map((s) => base * (s - 1));
      const shifts = dockShifts(extras);
      let minL = Infinity;
      let maxR = -Infinity;
      items.forEach((_, i) => {
        const s = scales[i] || 1;
        const shift = shifts[i] || 0;
        const center = restingCenter(i) + shift;
        const left = center - (base * s) / 2;
        const right = center + (base * s) / 2;
        if (left < minL) minL = left;
        if (right > maxR) maxR = right;
        const el = btnRefs.current[i];
        if (el) {
          el.style.transform = vertical
            ? `translate3d(0, ${shift}px, 0) scale(${s})`
            : `translate3d(${shift}px, 0, 0) scale(${s})`;
        }
        const dot = dotRefs.current[i];
        if (dot) {
          dot.style.transform = vertical
            ? `translate3d(0, ${shift}px, 0)`
            : `translate3d(${shift}px, 0, 0)`;
        }
      });
      const glass = glassRef.current;
      if (glass && Number.isFinite(minL)) {
        const gLeft = minL - 8;
        const gSize = maxR - minL + 16;
        if (vertical) {
          glass.style.top = `${gLeft}px`;
          glass.style.height = `${gSize}px`;
          glass.style.left = "4px";
          glass.style.width = `${shellCross - 8}px`;
        } else {
          glass.style.left = `${gLeft}px`;
          glass.style.width = `${gSize}px`;
          glass.style.bottom = "2px";
          glass.style.height = `${shellCross - 6}px`;
        }
      }
      const div = divRef.current;
      if (div && trashIndex > 0) {
        const boundary = restingLeft(trashIndex) - DOCK_DIVIDER_GAP / 2 - DOCK_PAD_X / 2;
        const shift = ((shifts[trashIndex - 1] || 0) + (shifts[trashIndex] || 0)) / 2;
        div.style.transform = vertical
          ? `translate3d(0, ${boundary + shift}px, 0)`
          : `translate3d(${boundary + shift}px, 0, 0)`;
      }
      const tip = tipRef.current;
      const hi = hoverRef.current;
      if (tip) {
        if (hi >= 0 && mouseRef.current != null) {
          const s = scales[hi] || 1;
          const shift = shifts[hi] || 0;
          tip.textContent = items[hi]?.label ?? "";
          tip.style.opacity = "1";
          const along = restingCenter(hi) + shift;
          if (vertical) {
            tip.style.top = `${along}px`;
            tip.style.left = `${shellCross + 10}px`;
            tip.style.transform = "translateY(-50%)";
          } else {
            tip.style.left = `${along}px`;
            tip.style.bottom = `${8 + base * s + 10}px`;
            tip.style.transform = "translateX(-50%)";
          }
        } else {
          tip.style.opacity = "0";
        }
      }
    },
    [base, items, restingCenter, restingLeft, shellCross, trashIndex, vertical],
  );

  /** Publish live bounding boxes to the genie-animation registry (no React re-render). */
  const publish = useCallback(() => {
    items.forEach((item, i) => {
      const el = btnRefs.current[i];
      if (!el) return;
      const id = item.kind === "trash" ? "trash" : item.appId!;
      const r = el.getBoundingClientRect();
      setDockTarget(id, {
        cx: r.left + r.width / 2,
        cy: r.top + r.height / 2,
        size: r.width,
      });
    });
  }, [items]);

  const stop = useCallback(() => {
    if (rafRef.current) cancelAnimationFrame(rafRef.current);
    rafRef.current = 0;
    liveRef.current = false;
  }, []);

  /**
   * Single rAF tick. Interpolates scales toward goal with a critically
   * damped feel, applies transforms, publishes dock targets, and either
   * schedules the next frame or stops when settled.
   */
  const tick = useCallback(() => {
    const n = items.length;
    if (scalesRef.current.length !== n) scalesRef.current = Array(n).fill(1);
    const target = mouseRef.current;
    let goal: number[];
    if (target == null || maxScale <= 1.001) {
      goal = Array(n).fill(1);
      smoothRef.current = null;
    } else {
      // Smooth the pointer coordinate with a one-pole filter for buttery feel.
      smoothRef.current = smoothRef.current == null
        ? target
        : smoothRef.current + (target - smoothRef.current) * 0.72;
      goal = items.map((_, i) =>
        dockScale(Math.abs(smoothRef.current! - restingCenter(i)), radius, 1, maxScale),
      );
    }
    // Follow rate is high when actively tracking, slower when settling out.
    const rate = target == null ? 0.18 : 0.68;
    let moving = false;
    const next = goal.map((g, i) => {
      const v = scalesRef.current[i] + (g - scalesRef.current[i]) * rate;
      if (Math.abs(v - g) > 0.002 || (target == null && Math.abs(v - 1) > 0.003)) moving = true;
      return v;
    });
    scalesRef.current = next;
    apply(next);
    publish();
    if (!moving && target == null) {
      scalesRef.current = Array(n).fill(1);
      apply(scalesRef.current);
      publish();
      stop();
      return;
    }
    rafRef.current = requestAnimationFrame(tick);
  }, [apply, items, maxScale, publish, radius, restingCenter, stop]);

  const ensure = useCallback(() => {
    if (liveRef.current) return;
    liveRef.current = true;
    rafRef.current = requestAnimationFrame(tick);
  }, [tick]);

  useLayoutEffect(() => {
    scalesRef.current = Array(items.length).fill(1);
    apply(scalesRef.current);
    publish();
    return () => stop();
  }, [apply, items.length, publish, stop, base, dock.pos, maxScale]);

  useLayoutEffect(() => {
    if (!dock.autohide) setRevealed(true);
  }, [dock.autohide]);

  /** Convert pointer event to a main-axis coordinate in shell space, centered. */
  const toShellCoord = (e: React.PointerEvent): number => {
    const shell = shellRef.current;
    if (!shell) return 0;
    const r = shell.getBoundingClientRect();
    return vertical
      ? e.clientY - r.top - (shellMain / 2)
      : e.clientX - r.left - (shellMain / 2);
  };

  const onMove = (e: React.PointerEvent) => {
    const pos = toShellCoord(e);
    mouseRef.current = pos;
    // Find closest icon for the tooltip/hit.
    let best = -1;
    let bestD = base * 0.78;
    items.forEach((_, i) => {
      const d = Math.abs(pos - (restingCenter(i) - shellMain / 2));
      if (d < bestD) { bestD = d; best = i; }
    });
    hoverRef.current = best;
    window.clearTimeout(tipTimeoutRef.current);
    tipTimeoutRef.current = window.setTimeout(() => {
      // delay tooltip slightly
    }, 200);
    ensure();
  };

  const onLeave = () => {
    mouseRef.current = null;
    hoverRef.current = -1;
    ensure();
    if (dock.autohide) setRevealed(false);
  };

  const openApp = (app: AppId) => {
    const mins = os.wins.filter((w) => w.app === app && w.min);
    if (mins.length) {
      os.setMin(mins[mins.length - 1].id, false);
      return;
    }
    os.openApp(app);
  };

  const posCls =
    dock.pos === "left" ? "left-2 top-1/2" :
    dock.pos === "right" ? "right-2 top-1/2" :
    "bottom-2 left-1/2";
  const parked = !revealed && dock.autohide;
  const shellTransform =
    dock.pos === "left" ? `translate(${parked ? "calc(-100% - 12px)" : "0"}, -50%)` :
    dock.pos === "right" ? `translate(${parked ? "calc(100% + 12px)" : "0"}, -50%)` :
    `translate(-50%, ${parked ? "calc(100% + 16px)" : "0"})`;

  return (
    <>
      {dock.autohide && (
        <div
          className={`absolute z-[390] ${
            dock.pos === "bottom"
              ? "inset-x-0 bottom-0 h-2"
              : dock.pos === "left"
                ? "left-0 inset-y-0 top-6 w-2"
                : "right-0 inset-y-0 top-6 w-2"
          }`}
          onPointerEnter={() => setRevealed(true)}
        />
      )}
      <div
        ref={shellRef}
        className={`dock-shell absolute z-[400] ${posCls}`}
        style={vertical
          ? { width: shellCross, height: shellMain, transform: shellTransform }
          : { width: shellMain, height: shellCross, transform: shellTransform }}
        onPointerEnter={() => {
          setRevealed(true);
          ensure();
        }}
        onPointerMove={onMove}
        onPointerLeave={onLeave}
        onPointerDown={(e) => e.stopPropagation()}
        // Ensure we don't trap the cursor: never set pointer-events: none globally;
        // children handle pointer events individually.
      >
        <div ref={glassRef} className="dock-shelf" />
        <div ref={tipRef} className="dock-tip" />
        {trashIndex > 0 && (
          <div
            ref={divRef}
            className={`dock-divider ${vertical ? "is-h" : ""}`}
            style={vertical
              ? { left: 8, top: shellMain / 2 }
              : { bottom: 10, left: shellMain / 2 }}
          />
        )}
        {items.map((item, i) => {
          const along = restingCenter(i) - base / 2;
          const runningApp = item.appId
            ? running.has(item.appId) || (item.appId === "launchpad" && os.launchpad)
            : false;
          const bouncing = item.appId != null && os.bouncingApp === item.appId;
          return (
            <div
              key={item.key}
              className="dock-slot"
              style={vertical
                ? { top: along, left: 6, width: base, height: base }
                : { left: along, bottom: 6, width: base, height: base }}
            >
              <button
                ref={(el) => { btnRefs.current[i] = el; }}
                className="dock-btn"
                style={{
                  width: base,
                  height: base,
                  transformOrigin: vertical ? "center left" : "bottom center",
                }}
                aria-label={item.label}
                onClick={() => {
                  if (item.kind === "trash") os.openApp("finder", "Trash", "Trash");
                  else if (item.appId) openApp(item.appId);
                }}
                onContextMenu={(e) => {
                  if (item.kind !== "trash") return;
                  e.preventDefault();
                  os.setPowerState("trash_dialog");
                }}
              >
                <span
                  ref={(el) => { bounceRefs.current[i] = el; }}
                  className={bouncing ? "dock-bounce" : "dock-glyph"}
                >
                  <G1Icon
                    name={item.kind === "trash" ? "trash" : item.appId!}
                    size={base}
                    count={item.kind === "trash" ? trashCount : 0}
                  />
                </span>
              </button>
              <span
                ref={(el) => { dotRefs.current[i] = el; }}
                className={`dock-dot ${runningApp ? "on" : ""}`}
              />
            </div>
          );
        })}
      </div>
    </>
  );
}
