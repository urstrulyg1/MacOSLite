import React, { useRef, useState } from "react";
import { X, Minus, Plus } from "lucide-react";
import { useOS, type Win } from "./os";

export default function WindowFrame({ win, children, chrome = true }: { win: Win; children: React.ReactNode; chrome?: boolean }) {
  const os = useOS();
  const ref = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState(false);
  const drag = useRef<{ dx: number; dy: number; x: number; y: number } | null>(null);
  const rsz = useRef<{ sw: number; sh: number; sx: number; sy: number } | null>(null);

  const focused = os.wins.filter((w) => !w.min).sort((a, b) => b.z - a.z)[0]?.id === win.id;

  const onDragStart = (e: React.PointerEvent) => {
    if (win.full) return;
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    drag.current = { dx: e.clientX - win.x, dy: e.clientY - win.y, x: win.x, y: win.y };
    setDragging(true);
  };
  const onDragMove = (e: React.PointerEvent) => {
    if (!drag.current || !ref.current) return;
    const x = e.clientX - drag.current.dx;
    const y = Math.max(28, e.clientY - drag.current.dy);
    drag.current.x = x; drag.current.y = y;
    ref.current.style.left = `${x}px`;
    ref.current.style.top = `${y}px`;
  };
  const onDragEnd = () => {
    if (!drag.current) return;
    os.moveWin(win.id, drag.current.x, drag.current.y);
    drag.current = null;
    setDragging(false);
  };

  const onRszStart = (e: React.PointerEvent) => {
    if (win.full) return;
    e.stopPropagation();
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    rsz.current = { sw: win.w, sh: win.h, sx: e.clientX, sy: e.clientY };
    setDragging(true);
  };
  const onRszMove = (e: React.PointerEvent) => {
    if (!rsz.current || !ref.current) return;
    const w = Math.max(280, rsz.current.sw + e.clientX - rsz.current.sx);
    const h = Math.max(200, rsz.current.sh + e.clientY - rsz.current.sy);
    ref.current.style.width = `${w}px`;
    ref.current.style.height = `${h}px`;
    rsz.current.sw = w; rsz.current.sh = h; rsz.current.sx = e.clientX; rsz.current.sy = e.clientY;
  };
  const onRszEnd = () => {
    if (!rsz.current || !ref.current) return;
    os.resizeWin(win.id, parseFloat(ref.current.style.width), parseFloat(ref.current.style.height));
    rsz.current = null;
    setDragging(false);
  };

  const animClass = win.animState ?? "active";
  const focusClass = focused ? "focused" : "unfocused";

  return (
    <div
      ref={ref}
      className={`oswin ${animClass} ${focusClass} ${win.min ? "minimized" : ""} ${win.full ? "full" : ""} ${dragging ? "dragging" : ""}`}
      style={{ left: win.x, top: win.y, width: win.w, height: win.h, zIndex: win.z }}
      onPointerDown={() => !focused && os.focusWin(win.id)}
    >
      {chrome && (
        <div
          className="titlebar relative flex h-[38px] flex-none select-none items-center justify-center border-b border-black/10 bg-white/40 transition-colors duration-200"
          onPointerDown={onDragStart}
          onPointerMove={onDragMove}
          onPointerUp={onDragEnd}
          onDoubleClick={() => os.toggleFull(win.id)}
        >
          <div
            className={`absolute left-3 flex items-center gap-2 transition-opacity duration-200 ${
              focused ? "opacity-100" : "opacity-50 hover:opacity-100"
            }`}
            onPointerDown={(e) => e.stopPropagation()}
          >
            <button
              className="titlebar-btn bg-[#ff5f57]"
              onClick={() => os.closeWin(win.id)}
              title="Close"
            ><X size={9} strokeWidth={2.6} /></button>
            <button
              className="titlebar-btn bg-[#febc2e]"
              onClick={() => os.setMin(win.id, true)}
              title="Minimize"
            ><Minus size={9} strokeWidth={2.6} /></button>
            <button
              className="titlebar-btn bg-[#28c840]"
              onClick={() => os.toggleFull(win.id)}
              title="Full Screen"
            ><Plus size={9} strokeWidth={2.6} /></button>
          </div>
          <div className={`pointer-events-none px-16 text-center text-[13px] font-semibold transition-colors duration-200 ${focused ? "text-black/85" : "text-black/40"}`}>
            {win.title}
          </div>
        </div>
      )}
      <div className="relative min-h-0 flex-1">{children}</div>
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
