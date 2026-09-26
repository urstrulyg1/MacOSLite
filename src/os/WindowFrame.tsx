import React, { useLayoutEffect, useRef } from "react";
import { useOS, type Win } from "./os";
import { getDockTarget } from "./dockRegistry";
import { clearMotion, commitVisualCenter, motionProfile, retargetTransform } from "./motion";

function deltaToDock(el: HTMLElement, app: string) {
  const dock = getDockTarget(app);
  if (!dock) return null;
  const r = el.getBoundingClientRect();
  const parent = el.offsetParent as HTMLElement | null;
  if (!parent) return null;
  const pr = parent.getBoundingClientRect();
  return {
    x: dock.cx - (r.left + r.width / 2 - pr.left),
    y: dock.cy - (r.top + r.height / 2 - pr.top),
  };
}

export default function WindowFrame({ win, children, chrome = true }: { win: Win; children: React.ReactNode; chrome?: boolean }) {
  const os = useOS();
  const ref = useRef<HTMLDivElement>(null);
  const drag = useRef<{ dx: number; dy: number; x: number; y: number } | null>(null);
  const rsz = useRef<{ sw: number; sh: number; sx: number; sy: number; sx0: number; sy0: number; dir: string } | null>(null);
  const dragging = useRef(false);
  const profile = motionProfile(os.visualMode, os.reduceMotion);

  const focused = os.wins.filter((w) => !w.min).sort((a, b) => b.z - a.z)[0]?.id === win.id;

  /** Animate window open / restore from its Dock icon position. */
  const flyFromDock = (el: HTMLElement) => {
    if (!profile.genie || reduceMotionQuick()) {
      el.style.opacity = "0";
      el.style.transform = "scale(0.96) translateY(6px)";
      el.getBoundingClientRect();
      el.style.transition = `opacity ${Math.max(80, profile.openMs * 0.7)}ms ease-out, transform ${profile.openMs}ms ${profile.ease}`;
      el.style.opacity = "1";
      el.style.transform = "translate3d(0,0,0) scale(1)";
      return;
    }
    const d = deltaToDock(el, win.app);
    el.style.transformOrigin = "center center";
    el.style.transition = "none";
    if (d) {
      // Genie: spawn small at the dock icon and fly to final position.
      el.style.transform = `translate3d(${d.x}px, ${d.y}px, 0) scale(0.12)`;
      el.style.opacity = "0.5";
    } else {
      el.style.transform = "translate3d(0, 10px, 0) scale(0.94)";
      el.style.opacity = "0";
    }
    el.getBoundingClientRect();
    retargetTransform(el, "translate3d(0,0,0) scale(1)", profile.openMs, profile.ease, "1");
  };

  function reduceMotionQuick() {
    return os.reduceMotion;
  }

  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;

    const cleanup = () => {
      // nothing persistent
    };

    if (win.animState === "opening" || win.animState === "restoring") {
      flyFromDock(el);
      const done = (ev: TransitionEvent) => {
        if (ev.propertyName !== "transform" && ev.propertyName !== "opacity") return;
        el.removeEventListener("transitionend", done);
        if (dragging.current) return;
        el.style.transition = "";
        el.style.transform = "";
        el.style.opacity = "";
        os.settleAnim(win.id);
      };
      el.addEventListener("transitionend", done);
      return () => {
        el.removeEventListener("transitionend", done);
        cleanup();
      };
    }
    if (win.animState === "closing") {
      const d = deltaToDock(el, win.app);
      el.style.transformOrigin = "center center";
      const to = d && profile.genie
        ? `translate3d(${d.x}px, ${d.y}px, 0) scale(0.12)`
        : "translate3d(0, 6px, 0) scale(0.94)";
      retargetTransform(el, to, profile.closeMs, profile.closeEase, "0");
      return cleanup;
    }
    if (win.animState === "minimizing") {
      const d = deltaToDock(el, win.app);
      el.style.transformOrigin = "center center";
      const to = d && profile.genie
        ? `translate3d(${d.x}px, ${d.y}px, 0) scale(0.1)`
        : "translate3d(0, 32px, 0) scale(0.9)";
      retargetTransform(el, to, profile.minMs, profile.ease, "0");
      const done = (ev: TransitionEvent) => {
        if (ev.propertyName !== "transform") return;
        el.removeEventListener("transitionend", done);
        if (!dragging.current) os.finishMin(win.id);
      };
      el.addEventListener("transitionend", done);
      return () => el.removeEventListener("transitionend", done);
    }
    if (win.animState === "active" && !dragging.current) {
      // Ensure any residual transform is cleared when we return to active.
      const t = getComputedStyle(el).transform;
      if (t && t !== "none" && t !== "matrix(1, 0, 0, 1, 0, 0)") {
        retargetTransform(el, "translate3d(0,0,0) scale(1)", profile.openMs, profile.ease, "1");
        const clear = () => {
          if (!dragging.current) {
            el.style.transition = "";
            el.style.transform = "";
            el.style.opacity = "";
          }
        };
        window.setTimeout(clear, profile.openMs + 20);
      }
    }
    return cleanup;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [win.animState, win.id]);

  /** When the user grabs the titlebar mid-animation, commit the visual box so there's no pop. */
  const takeOver = (_e: React.PointerEvent) => {
    const el = ref.current;
    if (!el || win.full) return null;
    dragging.current = true;
    const box = commitVisualCenter(el, win.w, win.h);
    if (box) {
      os.moveWin(win.id, box.x, box.y);
      os.settleAnim(win.id);
    }
    el.classList.add("dragging");
    return box;
  };

  const onDragStart = (e: React.PointerEvent) => {
    if ((e.target as HTMLElement).closest("button")) return;
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    // Dragging a fullscreen window exits fullscreen to a restored size near grab.
    if (win.full) {
      os.toggleFull(win.id);
      const restored = { ...win, full: false, x: win.saved?.x ?? 80, y: win.saved?.y ?? 48, w: win.saved?.w ?? 720, h: win.saved?.h ?? 480 };
      const bx = Math.max(80, e.clientX - restored.w / 2);
      os.moveWin(win.id, bx, 48);
      window.setTimeout(() => {
        if (ref.current) {
          ref.current.style.left = `${bx}px`;
          ref.current.style.top = `48px`;
        }
      }, 0);
      drag.current = { dx: e.clientX - bx, dy: e.clientY - 48, x: bx, y: 48 };
      dragging.current = true;
      if (ref.current) ref.current.classList.add("dragging");
      return;
    }
    const box = takeOver(e);
    const x = box?.x ?? win.x;
    const y = box?.y ?? win.y;
    drag.current = { dx: e.clientX - x, dy: e.clientY - y, x, y };
  };
  const onDragMove = (e: React.PointerEvent) => {
    if (!drag.current || !ref.current) return;
    const x = e.clientX - drag.current.dx;
    const y = Math.max(os.appearance ? 0 : 0, e.clientY - drag.current.dy);
    // Constrain to menubar (win-layer starts at y=26 but windows use stage coords)
    const clampedY = Math.max(28, y);
    drag.current.x = x;
    drag.current.y = clampedY;
    // Write directly for 60fps; React state commits on pointer up.
    ref.current.style.left = `${x}px`;
    ref.current.style.top = `${clampedY}px`;
  };
  const onDragEnd = (_e?: React.PointerEvent) => {
    if (!drag.current || !ref.current) return;
    const x = drag.current.x;
    const y = drag.current.y;
    const lw = ref.current.offsetWidth;
    const lh = ref.current.offsetHeight;
    const EDGE = 4;
    // Edge snapping (left/right/top) unless we're already fullscreen.
    if (!win.full) {
      const stageW = window.innerWidth;
      const nearEdge = x <= EDGE || y <= EDGE || x + lw >= stageW - EDGE;
      if (nearEdge) {
        os.snapWindow(win.id, x, y, lw, lh);
        drag.current = null;
        dragging.current = false;
        ref.current.classList.remove("dragging");
        ref.current.style.transition = "";
        return;
      }
    }
    os.moveWin(win.id, x, y);
    drag.current = null;
    dragging.current = false;
    ref.current.classList.remove("dragging");
    ref.current.style.transition = "";
  };

  const onRszStart = (e: React.PointerEvent, dir = "se") => {
    if (win.full) return;
    e.stopPropagation();
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    takeOver(e);
    const el = ref.current;
    const currentW = el?.offsetWidth || win.w;
    const currentH = el?.offsetHeight || win.h;
    const currentX = parseFloat(el?.style.left || String(win.x)) || win.x;
    const currentY = parseFloat(el?.style.top || String(win.y)) || win.y;
    rsz.current = {
      sw: currentW, sh: currentH,
      sx: currentX, sy: currentY,
      sx0: e.clientX, sy0: e.clientY,
      dir,
    };
  };
  const onRszMove = (e: React.PointerEvent, dir?: string) => {
    if (!rsz.current || !ref.current) return;
    const d = dir || rsz.current.dir;
    const dx = e.clientX - rsz.current.sx0;
    const dy = e.clientY - rsz.current.sy0;
    let { sw: w, sh: h, sx: x, sy: y } = rsz.current;
    if (d.includes("e")) w = Math.max(320, rsz.current.sw + dx);
    if (d.includes("s")) h = Math.max(220, rsz.current.sh + dy);
    if (d.includes("w")) {
      w = Math.max(320, rsz.current.sw - dx);
      x = rsz.current.sx + (rsz.current.sw - w);
    }
    if (d.includes("n")) {
      h = Math.max(220, rsz.current.sh - dy);
      y = Math.max(26, rsz.current.sy + (rsz.current.sh - h));
    }
    ref.current.style.width = `${w}px`;
    ref.current.style.height = `${h}px`;
    ref.current.style.left = `${x}px`;
    ref.current.style.top = `${y}px`;
    rsz.current.sw = w;
    rsz.current.sh = h;
    rsz.current.sx = x;
    rsz.current.sy = y;
  };
  const onRszEnd = () => {
    if (!rsz.current || !ref.current) return;
    os.resizeWin(win.id, rsz.current.sw, rsz.current.sh);
    os.moveWin(win.id, rsz.current.sx, rsz.current.sy);
    rsz.current = null;
    dragging.current = false;
    ref.current.classList.remove("dragging");
  };

  const onZoom = (e: React.MouseEvent) => {
    e.stopPropagation();
    const el = ref.current;
    if (el && !win.full) {
      const box = commitVisualCenter(el, win.w, win.h);
      if (box) os.moveWin(win.id, box.x, box.y);
      clearMotion(el);
    }
    os.toggleFull(win.id);
  };

  // If minimized and not animating back, hide.
  if (win.min && win.animState !== "restoring" && win.animState !== "minimizing") return null;

  return (
    <div
      ref={ref}
      className={`oswin ${focused ? "focused" : "unfocused"} ${win.full ? "full" : ""} ${win.animState === "opening" ? "opening" : ""} ${win.animState === "closing" ? "closing" : ""}`}
      style={{
        left: win.x,
        top: win.y,
        width: win.w,
        height: win.h,
        zIndex: win.z,
        borderRadius: win.full ? 0 : undefined,
      }}
      onPointerDown={() => { if (!focused) os.focusWin(win.id); }}
    >
      {chrome && (
        <div
          className="titlebar relative flex h-[30px] flex-none select-none items-center justify-center"
          onPointerDown={onDragStart}
          onPointerMove={onDragMove}
          onPointerUp={onDragEnd}
          onPointerCancel={onDragEnd}
          onDoubleClick={onZoom}
        >
          <div
            className={`absolute left-3 flex items-center gap-2 ${focused ? "opacity-100" : "opacity-50 hover:opacity-100"}`}
            onPointerDown={(e) => e.stopPropagation()}
          >
            <button
              className="titlebar-btn bg-[#ff5f57]"
              onClick={() => os.closeWin(win.id)}
              title="Close"
              aria-label="Close"
            >
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden>
                <path d="M1.4 1.4l5.2 5.2M6.6 1.4L1.4 6.6" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
              </svg>
            </button>
            <button
              className="titlebar-btn bg-[#febc2e]"
              onClick={() => os.setMin(win.id, true)}
              title="Minimize"
              aria-label="Minimize"
            >
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden>
                <path d="M1.3 4h5.4" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
              </svg>
            </button>
            <button
              className="titlebar-btn bg-[#28c840]"
              onClick={onZoom}
              title="Zoom / Full Screen"
              aria-label="Full Screen"
            >
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden>
                <path d="M1.6 3.2V1.6H3.2M6.4 3.2V1.6H4.8M1.6 4.8v1.6h1.6M6.4 4.8v1.6H4.8" stroke="currentColor" strokeWidth="1.1" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
            </button>
          </div>
          <div
            className={`pointer-events-none px-16 text-center text-[13px] font-semibold ${
              focused ? "text-[var(--os-text)]" : "text-[var(--os-text-3)]"
            }`}
          >
            {win.title}
          </div>
        </div>
      )}
      <div className="relative min-h-0 flex-1 overflow-hidden">{children}</div>
      {!win.full && (
        <>
          {/* Edges */}
          <div
            className="absolute left-0 right-0 top-0 z-20 h-1 cursor-n-resize"
            onPointerDown={(e) => onRszStart(e, "n")}
            onPointerMove={(e) => onRszMove(e, "n")}
            onPointerUp={onRszEnd}
            onPointerCancel={onRszEnd}
          />
          <div
            className="absolute bottom-0 left-0 right-0 z-20 h-1 cursor-s-resize"
            onPointerDown={(e) => onRszStart(e, "s")}
            onPointerMove={(e) => onRszMove(e, "s")}
            onPointerUp={onRszEnd}
            onPointerCancel={onRszEnd}
          />
          <div
            className="absolute left-0 top-0 bottom-0 z-20 w-1 cursor-w-resize"
            onPointerDown={(e) => onRszStart(e, "w")}
            onPointerMove={(e) => onRszMove(e, "w")}
            onPointerUp={onRszEnd}
            onPointerCancel={onRszEnd}
          />
          <div
            className="absolute right-0 top-0 bottom-0 z-20 w-1 cursor-e-resize"
            onPointerDown={(e) => onRszStart(e, "e")}
            onPointerMove={(e) => onRszMove(e, "e")}
            onPointerUp={onRszEnd}
            onPointerCancel={onRszEnd}
          />
          {/* Corner grip (bottom-right) */}
          <div
            className="absolute bottom-0 right-0 z-20 h-5 w-5 cursor-nwse-resize"
            onPointerDown={(e) => onRszStart(e, "se")}
            onPointerMove={(e) => onRszMove(e, "se")}
            onPointerUp={onRszEnd}
            onPointerCancel={onRszEnd}
          />
        </>
      )}
    </div>
  );
}
