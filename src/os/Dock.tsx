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
 * Floating Dock.
 *
 * The shell width is constant (worst-case magnification), so pointer
 * distance is measured against a stable origin — scales cannot feedback
 * into the coordinate they were computed from. Icons are translated by
 * neighbor displacement and scaled from the shelf floor. A single rAF
 * runs only while the pointer is over the Dock or the row is settling.
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
  const divRef = useRef<HTMLDivElement>(null);
  const tipRef = useRef<HTMLDivElement>(null);
  const mouseRef = useRef<number | null>(null);
  const smoothRef = useRef<number | null>(null);
  const scalesRef = useRef<number[]>([]);
  const rafRef = useRef(0);
  const liveRef = useRef(false);
  const hoverRef = useRef(-1);
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

  const restingLeft = useCallback((i: number) => {
    let x = 0;
    for (let j = 0; j < i; j++) {
      x += base + gap;
      if (j === trashIndex - 1) x += DOCK_DIVIDER_GAP;
    }
    return x;
  }, [base, gap, trashIndex]);

  const contentWidth = restingLeft(Math.max(0, items.length - 1)) + base;
  const maxGrow = items.length * base * Math.max(0, maxScale - 1);
  const origin = maxGrow / 2 + DOCK_PAD_X;
  const shellMain = contentWidth + maxGrow + DOCK_PAD_X * 2;
  const shellCross = base + 22;

  const apply = useCallback((scales: number[]) => {
    const extras = scales.map((s) => base * (s - 1));
    const shifts = dockShifts(extras);
    let minL = Infinity;
    let maxR = -Infinity;
    items.forEach((_, i) => {
      const s = scales[i] || 1;
      const shift = shifts[i] || 0;
      const left = restingLeft(i) + shift - (base * (s - 1)) / 2;
      const right = left + base * s;
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
      const gLeft = origin + minL - DOCK_PAD_X;
      const gSize = maxR - minL + DOCK_PAD_X * 2;
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
      const boundary = restingLeft(trashIndex) - DOCK_DIVIDER_GAP / 2;
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
        const along = origin + restingLeft(hi) + base / 2 + shift;
        if (vertical) {
          tip.style.top = `${along}px`;
          tip.style.left = `${shellCross + 8}px`;
        } else {
          tip.style.left = `${along}px`;
          tip.style.bottom = `${8 + base * s + 10}px`;
        }
      } else {
        tip.style.opacity = "0";
      }
    }
  }, [base, items, origin, restingLeft, shellCross, trashIndex, vertical]);

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

  const tick = useCallback(() => {
    const n = items.length;
    if (scalesRef.current.length !== n) scalesRef.current = Array(n).fill(1);
    const target = mouseRef.current;
    let goal: number[];
    if (target == null || maxScale <= 1.001) {
      goal = Array(n).fill(1);
      smoothRef.current = null;
    } else {
      smoothRef.current = smoothRef.current == null ? target : smoothRef.current + (target - smoothRef.current) * 0.62;
      goal = items.map((_, i) => dockScale(Math.abs(smoothRef.current! - (restingLeft(i) + base / 2)), radius, 1, maxScale));
    }
    const rate = target == null ? 0.22 : 0.58;
    let moving = false;
    const next = goal.map((g, i) => {
      const v = scalesRef.current[i] + (g - scalesRef.current[i]) * rate;
      if (Math.abs(v - g) > 0.003 || (target == null && Math.abs(v - 1) > 0.004)) moving = true;
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
  }, [apply, base, items, maxScale, publish, radius, restingLeft, stop]);

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

  const onMove = (e: React.PointerEvent) => {
    const shell = shellRef.current;
    if (!shell) return;
    const r = shell.getBoundingClientRect();
    mouseRef.current = vertical ? e.clientY - r.top - origin : e.clientX - r.left - origin;
    let best = -1;
    let bestD = base * 0.72;
    items.forEach((_, i) => {
      const d = Math.abs((mouseRef.current ?? 0) - (restingLeft(i) + base / 2));
      if (d < bestD) { bestD = d; best = i; }
    });
    hoverRef.current = best;
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
    "bottom-1.5 left-1/2";
  const parked = !revealed && dock.autohide;
  const shellTransform =
    dock.pos === "left" ? `translate(${parked ? "-120%" : "0"}, -50%)` :
    dock.pos === "right" ? `translate(${parked ? "120%" : "0"}, -50%)` :
    `translate(-50%, ${parked ? "130%" : "0"})`;

  return (
    <>
      {dock.autohide && (
        <div
          className={`absolute z-[390] ${dock.pos === "bottom" ? "inset-x-0 bottom-0 h-3" : dock.pos === "left" ? "left-0 inset-y-0 top-7 w-3" : "right-0 inset-y-0 top-7 w-3"}`}
          onPointerEnter={() => setRevealed(true)}
        />
      )}
      <div
        ref={shellRef}
        className={`dock-shell absolute z-[400] ${posCls}`}
        style={vertical
          ? { width: shellCross, height: shellMain, transform: shellTransform }
          : { width: shellMain, height: shellCross, transform: shellTransform }}
        onPointerEnter={() => setRevealed(true)}
        onPointerMove={onMove}
        onPointerLeave={onLeave}
        onPointerDown={(e) => e.stopPropagation()}
      >
        <div ref={glassRef} className="dock-shelf" />
        <div ref={tipRef} className="dock-tip" />
        {trashIndex > 0 && (
          <div
            ref={divRef}
            className={`dock-divider ${vertical ? "is-h" : ""}`}
            style={vertical ? { left: 8, top: origin } : { bottom: 10, left: origin }}
          />
        )}
        {items.map((item, i) => {
          const along = origin + restingLeft(i);
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
                : { left: along, bottom: 7, width: base, height: base }}
            >
              <button
                ref={(el) => { btnRefs.current[i] = el; }}
                className="dock-btn"
                style={{ width: base, height: base, transformOrigin: vertical ? "center left" : "bottom center" }}
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
                <span className={bouncing ? "dock-bounce" : "dock-glyph"}>
                  <G1Icon name={item.kind === "trash" ? "trash" : item.appId!} size={base} count={item.kind === "trash" ? trashCount : 0} />
                </span>
              </button>
              <span ref={(el) => { dotRefs.current[i] = el; }} className={`dock-dot ${runningApp ? "on" : ""}`} />
            </div>
          );
        })}
      </div>
    </>
  );
}
