import React, { useMemo, useRef, useState } from "react";
import { Trash2 } from "lucide-react";
import { APPS, DOCK_ORDER, useOS, type AppId } from "./os";
import { LogoMark } from "./MenuBar";

function AppTile({ app, size }: { app: AppId; size: number }) {
  const def = APPS[app];
  const s = Math.round(size);
  return (
    <div
      className={`grid place-items-center rounded-[24%] bg-gradient-to-b ${def.tile} shadow-[0_2px_8px_rgba(10,15,40,0.3),inset_0_0.5px_0_rgba(255,255,255,0.4)]`}
      style={{ width: s, height: s }}
    >
      {def.icon === "finder" ? (
        <FinderFace size={s} />
      ) : (
        React.createElement(def.icon as any, { size: Math.round(s * 0.56), strokeWidth: 1.9, className: def.glyph })
      )}
    </div>
  );
}

/* original two-tone finder-ish face */
export function FinderFace({ size }: { size: number }) {
  const s = Math.round(size);
  return (
    <svg width={s} height={s} viewBox="0 0 64 64">
      <defs>
        <linearGradient id="ff-l" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#7cc4ff" /><stop offset="1" stopColor="#2f8bff" />
        </linearGradient>
        <linearGradient id="ff-r" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#eef6ff" /><stop offset="1" stopColor="#b8d9ff" />
        </linearGradient>
      </defs>
      <rect x="5" y="5" width="54" height="54" rx="14" fill="url(#ff-l)" />
      <path d="M32 5.2C24 14 21 26 21.6 37c.3 6 2.6 12.6 5.4 21.9l-8 .1a14 14 0 0 1-14-14V19a14 14 0 0 1 14-14Z" fill="url(#ff-r)" opacity="0.9" />
      <path d="M25 24v8M40 24v8" stroke="#123" strokeWidth="3.4" strokeLinecap="round" opacity="0.75" />
      <path d="M22 42c3.5 3.2 6.7 4.6 10.2 4.6S39 45.2 42.5 42" stroke="#123" strokeWidth="3.4" strokeLinecap="round" fill="none" opacity="0.75" />
    </svg>
  );
}

export default function Dock() {
  const os = useOS();
  const { dock } = os;
  const [mouse, setMouse] = useState<number | null>(null);
  const [bounced, setBounced] = useState<string | null>(null);
  const [hoverStrip, setHoverStrip] = useState(false);
  const wrapRef = useRef<HTMLDivElement>(null);
  const vertical = dock.pos !== "bottom";

  const minimized = os.wins.filter((w) => w.min);
  const running = useMemo(() => new Set(os.wins.map((w) => w.app)), [os.wins]);

  const iconSize = (center: number) => {
    const base = dock.size;
    if (!dock.mag || mouse == null) return base;
    const d = Math.abs(center - mouse);
    const sigma = base * 1.35;
    return base + base * dock.magScale * Math.exp(-(d * d) / (2 * sigma * sigma));
  };

  const hidden = dock.autohide && !hoverStrip && mouse == null;

  const onMove = (e: React.MouseEvent) => {
    const rect = wrapRef.current?.getBoundingClientRect();
    if (!rect) return;
    setHoverStrip(false);
    setMouse(vertical ? e.clientY - rect.top : e.clientX - rect.left);
  };

  const stripCls: Record<string, string> = {
    bottom: "inset-x-0 bottom-0 h-[10px]",
    left: "left-0 inset-y-0 w-[10px] top-7",
    right: "right-0 inset-y-0 w-[10px] top-7",
  };

  const dockPos: Record<string, string> = {
    bottom: "bottom-2 left-1/2 -translate-x-1/2 flex-row items-end",
    left: "left-2 top-1/2 -translate-y-1/2 flex-col items-start",
    right: "right-2 top-1/2 -translate-y-1/2 flex-col items-end",
  };
  const hideShift: Record<string, string> = {
    bottom: "translateY(120%)",
    left: "translateX(-130%)",
    right: "translateX(130%)",
  };

  const bounce = (id: string) => {
    setBounced(id);
    setTimeout(() => setBounced(null), 1450);
  };

  return (
    <>
      {/* hover strip for autohide */}
      {dock.autohide && (
        <div
          className={`absolute z-[390] ${stripCls[dock.pos]}`}
          onMouseEnter={() => setHoverStrip(true)}
          onMouseLeave={() => setHoverStrip(false)}
        />
      )}
      <div
        ref={wrapRef}
        className={`glass absolute z-[400] flex rounded-[26px] bg-[rgba(240,242,248,0.42)] p-[7px] shadow-[0_0_0_0.5px_rgba(0,0,0,0.14),0_14px_42px_rgba(10,15,40,0.3)] ${dockPos[dock.pos]}`}
        style={{
          gap: 5,
          transform: `${dock.pos === "bottom" ? "translateX(-50%)" : dock.pos === "left" || dock.pos === "right" ? "translateY(-50%)" : ""} ${hidden ? hideShift[dock.pos] : ""}`,
          transition: "transform 0.35s cubic-bezier(0.3, 0.9, 0.3, 1)",
        }}
        onMouseMove={onMove}
        onMouseLeave={() => { setMouse(null); setHoverStrip(false); }}
        onMouseEnter={() => dock.autohide && setHoverStrip(false)}
      >
        {DOCK_ORDER.map((app) => {
          const def = APPS[app];
          return (
            <DockIcon key={app} vertical={vertical} label={def.name} moved={mouse != null}>
              {({ center }) => (
                <button
                  className={`dock-icon relative grid place-items-center ${bounced === app ? "dock-bounce" : ""}`}
                  style={{ width: iconSize(center), height: iconSize(center) }}
                  onClick={() => {
                    const hasWin = os.wins.some((w) => w.app === app);
                    if (!hasWin && app !== "launchpad") bounce(app);
                    os.openApp(app);
                  }}
                >
                  <AppTile app={app} size={iconSize(center)} />
                  <span
                    className={`absolute rounded-full bg-black/45 transition-opacity ${vertical ? "-left-[7px] top-1/2 -translate-y-1/2 h-[5px] w-[5px]" : "-bottom-[4.5px] left-1/2 -translate-x-1/2 h-[4.5px] w-[4.5px]"} ${running.has(app) ? "opacity-100" : "opacity-0"}`}
                  />
                </button>
              )}
            </DockIcon>
          );
        })}

        {(minimized.length > 0 || true) && (
          <div className={`self-stretch ${vertical ? "h-px w-full my-[3px]" : "w-px h-full mx-[3px]"} bg-black/15`} />
        )}

        {minimized.map((w) => (
          <DockIcon key={w.id} vertical={vertical} label={w.title} moved={mouse != null}>
            {({ center }) => (
              <button
                className="dock-icon grid place-items-center"
                style={{ width: iconSize(center), height: iconSize(center) }}
                onClick={() => os.setMin(w.id, false)}
                title={`Restore: ${w.title}`}
              >
                <div
                  className={`grid place-items-center rounded-[8px] bg-gradient-to-br from-slate-100 to-slate-300 shadow-inner`}
                  style={{ width: iconSize(center) * 0.86, height: iconSize(center) * 0.7 }}
                >
                  <LogoMark size={Math.round(iconSize(center) * 0.4)} />
                </div>
              </button>
            )}
          </DockIcon>
        ))}

        <DockIcon vertical={vertical} label="Trash" moved={mouse != null}>
          {({ center }) => (
            <button
              className="dock-icon grid place-items-center"
              style={{ width: iconSize(center), height: iconSize(center) }}
              onClick={() => os.notify("Trash", "Trash is empty", "Nothing to recover. Zero bytes wasted on indexing, naturally.")}
              title="Trash"
            >
              <div
                className="grid place-items-center rounded-[24%] bg-gradient-to-b from-zinc-200/80 to-zinc-400/80 text-zinc-700 shadow-[inset_0_0.5px_0_rgba(255,255,255,0.6)]"
                style={{ width: iconSize(center) * 0.94, height: iconSize(center) * 0.94 }}
              >
                <Trash2 size={Math.round(iconSize(center) * 0.5)} strokeWidth={1.7} />
              </div>
            </button>
          )}
        </DockIcon>
      </div>
    </>
  );
}

/* measures each icon's center so magnification math stays cheap */
function DockIcon({
  children, vertical, label, moved,
}: {
  children: (p: { center: number }) => React.ReactNode;
  vertical: boolean;
  label: string;
  moved: boolean;
}) {
  const ref = useRef<HTMLDivElement>(null);
  void moved;
  const rect = ref.current?.getBoundingClientRect();
  const parentRect = ref.current?.parentElement?.getBoundingClientRect();
  let center = 0;
  if (rect && parentRect) {
    center = vertical
      ? rect.top - parentRect.top + rect.height / 2
      : rect.left - parentRect.left + rect.width / 2;
  }
  return (
    <div ref={ref} className="group relative grid place-items-center">
      {children({ center })}
      <span
        className={`pointer-events-none absolute z-10 whitespace-nowrap rounded-md bg-[rgba(40,42,52,0.9)] px-2.5 py-1 text-[12px] font-medium text-white opacity-0 shadow-lg transition-opacity duration-150 group-hover:opacity-100 ${
          vertical ? "left-[calc(100%+12px)] top-1/2 -translate-y-1/2" : "-top-9 left-1/2 -translate-x-1/2"
        }`}
      >
        {label}
      </span>
    </div>
  );
}
