import React, { useLayoutEffect, useRef } from "react";
import { useOS, type Win } from "./os";
import { getDockTarget } from "./dockRegistry";
import { clearMotion, commitVisualCenter, motionProfile, retargetTransform } from "./motion";

function deltaToDock(el: HTMLElement, app: string) {
  const dock = getDockTarget(app);
  if (!dock) return null;
  const r = el.getBoundingClientRect();
  return {
    x: dock.cx - (r.left + r.width / 2),
    y: dock.cy - (r.top + r.height / 2),
  };
}

export default function WindowFrame({ win, children, chrome = true }: { win: Win; children: React.ReactNode; chrome?: boolean }) {
  const os = useOS();
  const ref = useRef<HTMLDivElement>(null);
  const drag = useRef<{ dx: number; dy: number; x: number; y: number } | null>(null);
  const rsz = useRef<{ sw: number; sh: number; sx: number; sy: number } | null>(null);
  const dragging = useRef(false);
  const profile = motionProfile(os.visualMode, os.reduceMotion);

  const focused = os.wins.filter((w) => !w.min).sort((a, b) => b.z - a.z)[0]?.id === win.id;

  const flyFromDock = (el: HTMLElement) => {
    if (!profile.genie) {
      el.style.opacity = "0";
      el.getBoundingClientRect();
      el.style.transition = `opacity ${profile.openMs}ms ease-out`;
      el.style.opacity = "1";
      return;
    }
    const d = deltaToDock(el, win.app);
    el.style.transformOrigin = "center center";
    el.style.transition = "none";
    if (d) {
      el.style.transform = `translate3d(${d.x}px, ${d.y}px, 0) scale(${profile.openMs < 200 ? 0.4 : 0.16})`;
      el.style.opacity = "0.45";
    } else {
      el.style.transform = "translate3d(0, 8px, 0) scale(0.96)";
      el.style.opacity = "0";
    }
    el.getBoundingClientRect();
    retargetTransform(el, "translate3d(0,0,0) scale(1)", profile.openMs, profile.ease, "1");
  };

  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
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
      return () => el.removeEventListener("transitionend", done);
    }
    if (win.animState === "closing") {
      const d = deltaToDock(el, win.app);
      el.style.transformOrigin = "center center";
      const to = d && profile.genie
        ? `translate3d(${d.x}px, ${d.y}px, 0) scale(0.16)`
        : "translate3d(0, 6px, 0) scale(0.94)";
      retargetTransform(el, to, profile.closeMs, profile.closeEase, "0");
      return;
    }
    if (win.animState === "minimizing") {
      const d = deltaToDock(el, win.app);
      el.style.transformOrigin = "center center";
      const to = d && profile.genie
        ? `translate3d(${d.x}px, ${d.y}px, 0) scale(0.12)`
        : "translate3d(0, 28px, 0) scale(0.92)";
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
      const t = getComputedStyle(el).transform;
      if (t && t !== "none" && t !== "matrix(1, 0, 0, 1, 0, 0)") {
        retargetTransform(el, "translate3d(0,0,0) scale(1)", profile.openMs, profile.ease, "1");
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [win.animState, win.id]);

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
    if (win.full || (e.target as HTMLElement).closest("button")) return;
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    const box = takeOver(e);
    const x = box?.x ?? win.x;
    const y = box?.y ?? win.y;
    drag.current = { dx: e.clientX - x, dy: e.clientY - y, x, y };
  };
  const onDragMove = (e: React.PointerEvent) => {
    if (!drag.current || !ref.current) return;
    const x = e.clientX - drag.current.dx;
    const y = Math.max(0, e.clientY - drag.current.dy);
    drag.current.x = x;
    drag.current.y = y;
    ref.current.style.left = `${x}px`;
    ref.current.style.top = `${y}px`;
  };
  const onDragEnd = () => {
    if (!drag.current) return;
    os.moveWin(win.id, drag.current.x, drag.current.y);
    drag.current = null;
    dragging.current = false;
    if (ref.current) {
      ref.current.classList.remove("dragging");
      ref.current.style.transition = "";
    }
  };

  const onRszStart = (e: React.PointerEvent) => {
    if (win.full) return;
    e.stopPropagation();
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    takeOver(e);
    const el = ref.current;
    rsz.current = {
      sw: el ? parseFloat(el.style.width) || win.w : win.w,
      sh: el ? parseFloat(el.style.height) || win.h : win.h,
      sx: e.clientX,
      sy: e.clientY,
    };
  };
  const onRszMove = (e: React.PointerEvent) => {
    if (!rsz.current || !ref.current) return;
    const w = Math.max(280, rsz.current.sw + e.clientX - rsz.current.sx);
    const h = Math.max(200, rsz.current.sh + e.clientY - rsz.current.sy);
    ref.current.style.width = `${w}px`;
    ref.current.style.height = `${h}px`;
    rsz.current.sw = w;
    rsz.current.sh = h;
    rsz.current.sx = e.clientX;
    rsz.current.sy = e.clientY;
  };
  const onRszEnd = () => {
    if (!rsz.current || !ref.current) return;
    os.resizeWin(win.id, rsz.current.sw, rsz.current.sh);
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

  if (win.min && win.animState !== "restoring" && win.animState !== "minimizing") return null;

  return (
    <div
      ref={ref}
      className={`oswin ${focused ? "focused" : "unfocused"} ${win.full ? "full" : ""}`}
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
          className="titlebar relative flex h-[38px] flex-none select-none items-center justify-center"
          onPointerDown={onDragStart}
          onPointerMove={onDragMove}
          onPointerUp={onDragEnd}
          onDoubleClick={onZoom}
        >
          <div className={`absolute left-3 flex items-center gap-2 ${focused ? "opacity-100" : "opacity-50 hover:opacity-100"}`} onPointerDown={(e) => e.stopPropagation()}>
            <button className="titlebar-btn bg-[#ff5f57]" onClick={() => os.closeWin(win.id)} title="Close" aria-label="Close">
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden><path d="M1.4 1.4l5.2 5.2M6.6 1.4L1.4 6.6" stroke="currentColor" strokeWidth="1.35" strokeLinecap="round" /></svg>
            </button>
            <button className="titlebar-btn bg-[#febc2e]" onClick={() => os.setMin(win.id, true)} title="Minimize" aria-label="Minimize">
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden><path d="M1.3 4h5.4" stroke="currentColor" strokeWidth="1.35" strokeLinecap="round" /></svg>
            </button>
            <button className="titlebar-btn bg-[#28c840]" onClick={onZoom} title="Full Screen" aria-label="Full Screen">
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden><path d="M1.6 3.2V1.6H3.2M6.4 3.2V1.6H4.8M1.6 4.8v1.6h1.6M6.4 4.8v1.6H4.8" stroke="currentColor" strokeWidth="1.15" strokeLinecap="round" strokeLinejoin="round" /></svg>
            </button>
          </div>
          <div className={`pointer-events-none px-16 text-center text-[13px] font-semibold ${focused ? "text-[var(--os-text)]" : "text-[var(--os-text-3)]"}`}>
            {win.title}
          </div>
        </div>
      )}
      <div className="relative min-h-0 flex-1 overflow-hidden">{children}</div>
      {!win.full && (
        <div
          className="absolute bottom-0 right-0 z-20 h-4 w-4 cursor-nwse-resize"
          onPointerDown={onRszStart}
          onPointerMove={onRszMove}
          onPointerUp={onRszEnd}
        />
      )}
    </div>
  );
}
