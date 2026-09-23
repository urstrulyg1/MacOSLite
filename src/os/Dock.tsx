import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { APPS, DOCK_ORDER, useOS, type AppId, type Win } from "./os";
import { LogoMark } from "./MenuBar";
import { G1Icon, FinderSquircle as FinderFace } from "./icons/IconSystem";

export { FinderFace };

function AppTile({ app, size }: { app: AppId; size: number }) {
  return <G1Icon name={app} size={Math.round(size)} />;
}

/* ------------------------------------------------------------------ */
/* Main macOS Dock Component                                           */
/* ------------------------------------------------------------------ */

export default function Dock() {
  const os = useOS();
  const { dock, perfMode, reduceMotion } = os;
  const wrapRef = useRef<HTMLDivElement>(null);
  const vertical = dock.pos !== "bottom";

  // Mouse coordinate offset from dock's center anchor point
  const [mouseCoord, setMouseCoord] = useState<number | null>(null);
  const [hoveredIdx, setHoveredIdx] = useState<number | null>(null);
  const [bounced, setBounced] = useState<string | null>(null);
  const [hoverStrip, setHoverStrip] = useState(false);
  const rafRef = useRef<number | null>(null);

  const minimized = os.wins.filter((w) => w.min);
  const running = useMemo(() => new Set(os.wins.map((w) => w.app)), [os.wins]);

  const baseSize = dock.size || 50;
  const gap = 8;
  const dividerWidth = 14;
  const isMagEnabled = dock.mag && !perfMode && !reduceMotion;
  const maxScale = isMagEnabled ? 1 + (dock.magScale || 0.45) : 1.0;
  const influenceRadius = baseSize * 2.8; // ~140px smooth influence zone

  // Construct flat ordered list of items for layout indexing
  interface ItemMeta {
    key: string;
    type: "app" | "minimized" | "trash";
    appId?: AppId;
    win?: Win;
    label: string;
  }

  const items: ItemMeta[] = useMemo(() => {
    const list: ItemMeta[] = [];
    for (const app of DOCK_ORDER) {
      const def = APPS[app] || APPS.finder;
      list.push({ key: `app-${app}`, type: "app", appId: app, label: def.name });
    }
    for (const w of minimized) {
      list.push({ key: `win-${w.id}`, type: "minimized", win: w, label: w.title });
    }
    const trashCount = (os.fs.Trash || []).length;
    list.push({
      key: "trash",
      type: "trash",
      label: trashCount > 0 ? `Trash (${trashCount} items)` : "Trash",
    });
    return list;
  }, [minimized, os.fs.Trash]);

  // Precompute static resting centers relative to the dock's center anchor
  const restingOffsets = useMemo(() => {
    const offsets: number[] = [];
    let cur = 0;
    const appCount = DOCK_ORDER.length;

    for (let i = 0; i < items.length; i++) {
      if (i === appCount) {
        cur += dividerWidth + gap;
      }
      const center = cur + baseSize / 2;
      offsets.push(center);
      cur += baseSize + gap;
    }

    const totalSpan = cur - gap;
    return offsets.map((c) => c - totalSpan / 2);
  }, [items.length, baseSize, gap]);

  // Compute magnification scale factor for an item index
  const getScale = useCallback(
    (index: number) => {
      if (!isMagEnabled || mouseCoord === null) return 1.0;
      const targetOffset = restingOffsets[index] ?? 0;
      const dist = Math.abs(mouseCoord - targetOffset);
      if (dist >= influenceRadius) return 1.0;

      // Cosine-squared taper for zero derivative at boundary (no jump or pop)
      const ratio = dist / influenceRadius;
      const cosVal = Math.cos((ratio * Math.PI) / 2);
      return 1.0 + (maxScale - 1.0) * (cosVal * cosVal);
    },
    [isMagEnabled, mouseCoord, restingOffsets, influenceRadius, maxScale]
  );

  // Mouse tracker locked to requestAnimationFrame for locked 60/120 FPS
  const onMouseMove = (e: React.MouseEvent<HTMLDivElement>) => {
    setHoverStrip(false);
    const rect = wrapRef.current?.getBoundingClientRect();
    if (!rect) return;

    // Anchor dock center on screen
    const dockCenter = vertical
      ? rect.top + rect.height / 2
      : rect.left + rect.width / 2;

    const mousePos = vertical ? e.clientY : e.clientX;
    const offset = mousePos - dockCenter;

    if (rafRef.current) cancelAnimationFrame(rafRef.current);
    rafRef.current = requestAnimationFrame(() => {
      setMouseCoord(offset);
    });
  };

  const onMouseLeave = () => {
    if (rafRef.current) cancelAnimationFrame(rafRef.current);
    setMouseCoord(null);
    setHoveredIdx(null);
    setHoverStrip(false);
  };

  useEffect(() => {
    return () => {
      if (rafRef.current) cancelAnimationFrame(rafRef.current);
    };
  }, []);

  const bounce = (id: string) => {
    setBounced(id);
    setTimeout(() => setBounced(null), 1400);
  };

  const hidden = dock.autohide && !hoverStrip && mouseCoord === null;

  const stripCls: Record<string, string> = {
    bottom: "inset-x-0 bottom-0 h-[10px]",
    left: "left-0 inset-y-0 w-[10px] top-7",
    right: "right-0 inset-y-0 w-[10px] top-7",
  };

  const dockPos: Record<string, string> = {
    bottom: "bottom-3 left-1/2 flex-row items-end",
    left: "left-3 top-1/2 flex-col items-start",
    right: "right-3 top-1/2 flex-col items-end",
  };

  const hideShift: Record<string, string> = {
    bottom: "translateY(130%)",
    left: "translateX(-130%)",
    right: "translateX(130%)",
  };

  const trashCount = (os.fs.Trash || []).length;
  const isInteracting = mouseCoord !== null;

  return (
    <>
      {/* Hover strip for autohide activation */}
      {dock.autohide && (
        <div
          className={`absolute z-[390] ${stripCls[dock.pos]}`}
          onMouseEnter={() => setHoverStrip(true)}
          onMouseLeave={() => setHoverStrip(false)}
        />
      )}

      {/* Outer macOS Dock Wrapper */}
      <div
        ref={wrapRef}
        className={`absolute z-[400] flex select-none ${dockPos[dock.pos]}`}
        style={{
          transform: `${dock.pos === "bottom" ? "translateX(-50%)" : "translateY(-50%)"} ${hidden ? hideShift[dock.pos] : ""}`,
          transition: "transform 0.32s cubic-bezier(0.2, 0.9, 0.3, 1)",
        }}
        onMouseMove={onMouseMove}
        onMouseLeave={onMouseLeave}
      >
        {/* Authentic macOS Glass Shelf Tray */}
        <div
          className={`dock-shelf relative flex items-end rounded-[24px] px-3.5 pt-2.5 pb-1.5`}
          style={{
            gap: gap,
          }}
        >
          {items.map((item, idx) => {
            const scale = getScale(idx);
            const slotWidth = Math.round(baseSize * scale);
            const isHovered = hoveredIdx === idx;
            const isDividerBefore = idx === DOCK_ORDER.length;
            const tooltipOffset = Math.round(baseSize * (scale - 1) + 12);

            return (
              <React.Fragment key={item.key}>
                {/* Authentic macOS vertical frosted glass divider */}
                {isDividerBefore && (
                  <div
                    className={`self-center rounded-full ${
                      vertical
                        ? "w-[30px] h-[1px] my-1 bg-white/35 shadow-[0_1px_1px_rgba(0,0,0,0.2)]"
                        : "h-[34px] w-[1px] mx-1.5 bg-white/40 shadow-[1px_0_1px_rgba(0,0,0,0.2)]"
                    }`}
                  />
                )}

                {/* Individual Icon Slot (Slot width perfectly matches scaled icon) */}
                <div
                  className="relative flex flex-col items-center justify-end"
                  style={{
                    width: vertical ? baseSize : slotWidth,
                    height: vertical ? slotWidth : undefined,
                    transition: isInteracting
                      ? "none"
                      : "width 0.28s cubic-bezier(0.25, 1, 0.5, 1), height 0.28s cubic-bezier(0.25, 1, 0.5, 1)",
                  }}
                  onMouseEnter={() => setHoveredIdx(idx)}
                >
                  {/* Floating macOS Tooltip anchored above magnified icon */}
                  {isHovered && isInteracting && (
                    <div
                      className={`pointer-events-none absolute z-50 whitespace-nowrap rounded-[7px] bg-[rgba(25,26,32,0.88)] px-2.5 py-1 text-[11.5px] font-medium tracking-tight text-white shadow-[0_4px_16px_rgba(0,0,0,0.32)] backdrop-blur-md border border-white/15 animate-fade-in ${
                        vertical
                          ? "left-[calc(100%+14px)] top-1/2 -translate-y-1/2"
                          : "left-1/2 -translate-x-1/2"
                      }`}
                      style={{
                        bottom: vertical ? undefined : `calc(100% + ${tooltipOffset}px)`,
                      }}
                    >
                      {item.label}
                      {/* Sub-pixel arrow indicator */}
                      {!vertical && (
                        <div className="absolute -bottom-1 left-1/2 -translate-x-1/2 h-1 w-2 border-l-4 border-r-4 border-t-4 border-transparent border-t-[rgba(25,26,32,0.88)]" />
                      )}
                    </div>
                  )}

                  {/* Icon Button */}
                  {item.type === "app" && item.appId && (
                    <AppIconButton
                      app={item.appId}
                      baseSize={baseSize}
                      scale={scale}
                      isInteracting={isInteracting}
                      isBouncing={bounced === item.appId || os.bouncingApp === item.appId}
                      isRunning={running.has(item.appId)}
                      onClick={() => {
                        const hasWin = os.wins.some((w) => w.app === item.appId);
                        if (!hasWin && item.appId !== "launchpad") bounce(item.appId);
                        os.openApp(item.appId);
                      }}
                    />
                  )}

                  {item.type === "minimized" && item.win && (
                    <MinimizedIconButton
                      win={item.win}
                      baseSize={baseSize}
                      scale={scale}
                      isInteracting={isInteracting}
                      onClick={() => os.setMin(item.win!.id, false)}
                    />
                  )}

                  {item.type === "trash" && (
                    <TrashIconButton
                      baseSize={baseSize}
                      scale={scale}
                      count={trashCount}
                      isInteracting={isInteracting}
                      onClick={() => os.openApp("finder", "Trash", "Trash")}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        os.setPowerState("trash_dialog");
                      }}
                    />
                  )}
                </div>
              </React.Fragment>
            );
          })}
        </div>
      </div>
    </>
  );
}

/* ------------------------------------------------------------------ */
/* Subcomponents for Individual Dock Tiles                            */
/* ------------------------------------------------------------------ */

function AppIconButton({
  app,
  baseSize,
  scale,
  isInteracting,
  isBouncing,
  isRunning,
  onClick,
}: {
  app: AppId;
  baseSize: number;
  scale: number;
  isInteracting: boolean;
  isBouncing: boolean;
  isRunning: boolean;
  onClick: () => void;
}) {
  return (
    <div className="relative flex flex-col items-center justify-end select-none">
      <button
        className="group relative flex items-center justify-center cursor-pointer outline-none"
        style={{
          width: baseSize,
          height: baseSize,
          transform: `scale(${scale})`,
          transformOrigin: "bottom center",
          transition: isInteracting
            ? "none"
            : "transform 0.28s cubic-bezier(0.25, 1, 0.5, 1)",
        }}
        onClick={onClick}
      >
        <div className={isBouncing ? "dock-bounce" : ""}>
          <AppTile app={app} size={baseSize} />
        </div>
      </button>

      {/* Running App Indicator Dot (Stationary on dock floor) */}
      <div className="h-[6px] flex items-center justify-center mt-1">
        <span
          className="h-[4.5px] w-[4.5px] rounded-full bg-white shadow-[0_0_2px_rgba(0,0,0,0.6),0_1px_2px_rgba(0,0,0,0.4)]"
        />
      </div>
    </div>
  );
}

function MinimizedIconButton({
  win,
  baseSize,
  scale,
  isInteracting,
  onClick,
}: {
  win: Win;
  baseSize: number;
  scale: number;
  isInteracting: boolean;
  onClick: () => void;
}) {
  return (
    <div className="relative flex flex-col items-center justify-end select-none">
      <button
        className="group relative flex items-center justify-center cursor-pointer outline-none"
        style={{
          width: baseSize,
          height: baseSize,
          transform: `scale(${scale})`,
          transformOrigin: "bottom center",
          transition: isInteracting
            ? "none"
            : "transform 0.28s cubic-bezier(0.25, 1, 0.5, 1)",
        }}
        onClick={onClick}
        title={`Restore: ${win.title}`}
      >
        <div
          className="grid place-items-center rounded-[20%] bg-gradient-to-br from-slate-100 to-slate-300 shadow-[0_2px_8px_rgba(10,15,40,0.25),inset_0_1px_0_rgba(255,255,255,0.6)]"
          style={{ width: baseSize, height: baseSize }}
        >
          <LogoMark size={Math.round(baseSize * 0.45)} />
        </div>
      </button>
      <div className="h-[6px] flex items-center justify-center mt-1">
        <span className="h-[4px] w-[4px] rounded-full bg-black/45" />
      </div>
    </div>
  );
}

function TrashIconButton({
  baseSize,
  scale,
  count,
  isInteracting,
  onClick,
  onContextMenu,
}: {
  baseSize: number;
  scale: number;
  count: number;
  isInteracting: boolean;
  onClick: () => void;
  onContextMenu: (e: React.MouseEvent) => void;
}) {
  return (
    <div className="relative flex flex-col items-center justify-end select-none">
      <button
        className="group relative flex items-center justify-center cursor-pointer outline-none"
        style={{
          width: baseSize,
          height: baseSize,
          transform: `scale(${scale})`,
          transformOrigin: "bottom center",
          transition: isInteracting
            ? "none"
            : "transform 0.28s cubic-bezier(0.25, 1, 0.5, 1)",
        }}
        onClick={onClick}
        onContextMenu={onContextMenu}
        title="Trash (Right click to empty)"
      >
        <G1Icon name="trash" size={baseSize} count={count} />

        {count > 0 && (
          <span className="absolute -top-1 -right-1 grid h-4 w-4 place-items-center rounded-full bg-blue-500 text-[9px] font-bold text-white shadow-[0_1px_3px_rgba(0,0,0,0.3)]">
            {count}
          </span>
        )}
      </button>
      <div className="h-[6px] mt-1" />
    </div>
  );
}
