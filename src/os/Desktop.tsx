import React, { useEffect, useMemo, useRef, useState } from "react";
import { Search, MousePointerClick, Layers } from "lucide-react";
import { APPS, FS, OSProvider, useOS, type AppId } from "./os";
import MenuBar, { LogoMark } from "./MenuBar";
import Dock, { FinderFace } from "./Dock";
import WindowFrame from "./WindowFrame";
import FinderApp from "./apps/FinderApp";
import SettingsApp from "./apps/SettingsApp";
import TerminalApp from "./apps/TerminalApp";
import { CalcApp, MusicApp, PhotosApp, PlayerApp, SysInfoApp, TextEditApp } from "./apps/MediaApps";
import type { Win } from "./os";

const WALLPAPERS = [
  { backgroundImage: "url(./wall.jpg)" },
  { background: "linear-gradient(135deg,#134e5e,#71b280)" },
  { background: "linear-gradient(150deg,#232526,#ff8f5e 130%)" },
];

function AppBody({ win }: { win: Win }) {
  switch (win.app) {
    case "finder": return <FinderApp />;
    case "settings": return <SettingsApp />;
    case "terminal": return <TerminalApp />;
    case "textedit": return <TextEditApp payload={win.payload} />;
    case "calc": return <CalcApp />;
    case "music": return <MusicApp payload={win.payload} />;
    case "photos": return <PhotosApp payload={win.payload} />;
    case "player": return <PlayerApp payload={win.payload} />;
    case "sysinfo": return <SysInfoApp />;
    default: return null;
  }
}

/* ---------------- spotlight ---------------- */

function Spotlight() {
  const os = useOS();
  const [q, setQ] = useState("");
  const [hi, setHi] = useState(0);
  useEffect(() => { setQ(""); setHi(0); }, [os.spotlight]);

  const results = useMemo(() => {
    const needle = q.trim().toLowerCase();
    const apps = Object.values(APPS)
      .filter((a) => !needle || a.name.toLowerCase().includes(needle))
      .map((a) => ({ kind: "app" as const, id: a.id, name: a.name, sub: "Application" }));
    const files = needle
      ? Object.entries(FS).flatMap(([loc, items]) =>
          items.filter((f) => f.name.toLowerCase().includes(needle)).slice(0, 4)
            .map((f) => ({ kind: "file" as const, id: f.name, name: f.name, sub: `${loc} · ${f.size}`, f })))
      : [];
    return [...apps, ...files].slice(0, 7);
  }, [q]);

  if (!os.spotlight) return null;
  const run = (r: (typeof results)[number]) => {
    if (r.kind === "app") os.openApp(r.id as AppId);
    else {
      const f = (r as any).f;
      if (f.kind === "video") os.openApp("player", f.name, f.name);
      else if (f.kind === "audio") os.openApp("music", f.name, f.name);
      else if (f.kind === "image") os.openApp("photos", f.name, f.name);
      else os.openApp("textedit", f.name, f.name);
    }
  };

  return (
    <div className="absolute inset-0 z-[600] flex items-start justify-center pt-[16vh]" onClick={() => os.setSpotlight(false)}>
      <div
        className="glass panel-in w-[560px] max-w-[86%] overflow-hidden rounded-2xl bg-[rgba(248,248,252,0.8)] shadow-[0_28px_90px_rgba(10,15,40,0.45),0_0_0_0.5px_rgba(0,0,0,0.2)]"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex items-center gap-3 px-4 py-3.5">
          <Search size={19} className="flex-none text-black/40" />
          <input
            autoFocus
            value={q}
            onChange={(e) => { setQ(e.target.value); setHi(0); }}
            onKeyDown={(e) => {
              if (e.key === "Enter" && results[hi]) { run(results[hi]); os.setSpotlight(false); }
              if (e.key === "ArrowDown") { e.preventDefault(); setHi((h) => Math.min(h + 1, results.length - 1)); }
              if (e.key === "ArrowUp") { e.preventDefault(); setHi((h) => Math.max(h - 1, 0)); }
            }}
            placeholder="Spotlight Search"
            className="w-full bg-transparent text-[19px] font-light outline-none placeholder:text-black/30"
          />
        </div>
        {results.length > 0 && (
          <div className="border-t border-black/10 p-1.5">
            {results.map((r, i) => (
              <button
                key={r.kind + r.id}
                onMouseEnter={() => setHi(i)}
                onClick={() => { run(r); os.setSpotlight(false); }}
                className={`flex w-full items-center justify-between rounded-lg px-3 py-2 text-left ${i === hi ? "bg-[var(--acc)] text-white" : ""}`}
              >
                <span className="text-[13.5px] font-medium">{r.name}</span>
                <span className={`text-[11px] ${i === hi ? "text-white/75" : "text-black/40"}`}>{r.sub}</span>
              </button>
            ))}
          </div>
        )}
        <div className="flex items-center justify-between border-t border-black/10 bg-black/[0.03] px-4 py-1.5 text-[10.5px] text-black/40">
          <span>No index. No daemon. Scanned 214 entries in 3 ms.</span>
          <span>↵ to open</span>
        </div>
      </div>
    </div>
  );
}

/* ---------------- launchpad ---------------- */

function Launchpad() {
  const os = useOS();
  const [q, setQ] = useState("");
  useEffect(() => setQ(""), [os.launchpad]);
  if (!os.launchpad) return null;
  const apps = Object.values(APPS).filter((a) => a.id !== "launchpad" && a.name.toLowerCase().includes(q.toLowerCase()));
  return (
    <div className="glass absolute inset-0 z-[590] bg-[rgba(70,80,110,0.45)] pt-[9vh]" onClick={() => os.setLaunchpad(false)}>
      <div className="mx-auto mb-8 flex w-56 items-center gap-2 rounded-full bg-white/25 px-3.5 py-1.5 backdrop-blur" onClick={(e) => e.stopPropagation()}>
        <Search size={14} className="text-white/80" />
        <input autoFocus value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search" className="w-full bg-transparent text-[13px] text-white outline-none placeholder:text-white/60" />
      </div>
      <div className="mx-auto grid max-w-[720px] grid-cols-4 gap-y-8 px-8 sm:grid-cols-5" onClick={(e) => e.stopPropagation()}>
        {apps.map((a) => (
          <button key={a.id} className="group flex flex-col items-center gap-2" onClick={() => { os.setLaunchpad(false); os.openApp(a.id); }}>
            <span className={`grid h-[62px] w-[62px] place-items-center rounded-[24%] bg-gradient-to-b shadow-[0_10px_28px_rgba(0,0,0,0.35),inset_0_0.5px_0_rgba(255,255,255,0.4)] transition-transform duration-200 group-hover:scale-110 ${a.tile}`}>
              {a.icon === "finder"
                ? <FinderFace size={50} />
                : React.createElement(a.icon as any, { size: 33, strokeWidth: 1.8, className: a.glyph })}
            </span>
            <span className="text-[12.5px] font-medium text-white drop-shadow">{a.name}</span>
          </button>
        ))}
      </div>
    </div>
  );
}

/* ---------------- toasts ---------------- */

function Toasts() {
  const os = useOS();
  const [gone, setGone] = useState<number[]>([]);
  const seen = useRef(new Set<number>());

  useEffect(() => {
    const fresh = os.notes.filter((n) => !seen.current.has(n.id));
    fresh.forEach((n) => {
      seen.current.add(n.id);
      setTimeout(() => setGone((g) => [...g, n.id]), 4300);
    });
  }, [os.notes]);

  const visible = os.notes.slice(-3).filter((n) => !gone.includes(n.id));
  return (
    <div className="pointer-events-none absolute right-2.5 top-9 z-[580] flex w-[320px] flex-col gap-2">
      {visible.map((n) => (
        <button
          key={n.id}
          className="glass note-in pointer-events-auto rounded-2xl bg-[rgba(250,250,253,0.88)] p-3 text-left text-black shadow-[0_14px_44px_rgba(10,15,40,0.3),0_0_0_0.5px_rgba(0,0,0,0.12)]"
          onClick={() => setGone((g) => [...g, n.id])}
        >
          <div className="flex items-start gap-2.5">
            <LogoMark size={26} />
            <div className="min-w-0">
              <div className="flex items-baseline justify-between gap-3">
                <span className="text-[13px] font-semibold">{n.title}</span>
                <span className="flex-none text-[10.5px] text-black/40">{n.time}</span>
              </div>
              <div className="mt-0.5 text-[12px] leading-snug text-black/60">{n.body}</div>
            </div>
          </div>
        </button>
      ))}
    </div>
  );
}

/* ---------------- stage ---------------- */

function Stage() {
  const os = useOS();
  const [hint, setHint] = useState(true);
  const booted = useRef(false);

  useEffect(() => {
    if (booted.current) return;
    booted.current = true;
    const t1 = setTimeout(() => os.openApp("finder"), 700);
    const t2 = setTimeout(() => os.notify("MacLiteOS", "Welcome back, 2010.", "384 MB RAM · 0.4% CPU · VDPAU hardware decode online. Click around — everything is live."), 1600);
    const t3 = setTimeout(() => setHint(false), 9000);
    return () => { clearTimeout(t1); clearTimeout(t2); clearTimeout(t3); };
  }, []);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.code === "Space") { e.preventDefault(); os.setSpotlight(!os.spotlight); }
      else if (e.key === "Escape") { os.setSpotlight(false); os.setLaunchpad(false); os.setMenuOpen(null); }
      else if (e.altKey && ["1", "2", "3"].includes(e.key)) { e.preventDefault(); os.setWs(+e.key - 1); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [os.spotlight]);

  return (
    <div className={`os-stage relative h-full w-full ${os.perfMode ? "perf" : ""}`} style={{ ["--acc" as any]: os.accent }}>
      {/* wallpapers */}
      {WALLPAPERS.map((w, i) => (
        <div key={i} className="wallpaper" style={{ ...w, opacity: os.wall === i ? 1 : 0 }} />
      ))}

      <MenuBar />

      {/* windows */}
      <div className="absolute inset-0 top-7">
        {os.wins.filter((w) => w.ws === os.ws).map((w) => (
          <WindowFrame key={w.id} win={w} chrome>
            <AppBody win={w} />
          </WindowFrame>
        ))}
      </div>

      <Dock />
      <Toasts />
      <Spotlight />
      <Launchpad />

      {/* hint */}
      {hint && (
        <div className="pointer-events-none absolute left-1/2 top-[12%] z-[560] -translate-x-1/2">
          <div className="glass flex items-center gap-2.5 rounded-full bg-[rgba(20,22,30,0.55)] px-4 py-2 text-[12px] text-white shadow-xl">
            <MousePointerClick size={14} className="text-white/80" />
            <span>Fully interactive — drag windows, click the Dock, hit</span>
            <span className="flex items-center gap-1 rounded-md bg-white/15 px-1.5 py-0.5 font-mono text-[10.5px]">
              <Layers size={11} /> ⌘ Space
            </span>
          </div>
        </div>
      )}
    </div>
  );
}

export default function Desktop() {
  const ref = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState<[number, number]>([1280, 800]);
  useEffect(() => {
    const el = ref.current!;
    const ro = new ResizeObserver(() => setSize([el.clientWidth, el.clientHeight]));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return (
    <div ref={ref} className="relative h-full w-full overflow-hidden">
      <OSProvider stageW={size[0]} stageH={size[1]}>
        <Stage />
      </OSProvider>
    </div>
  );
}
