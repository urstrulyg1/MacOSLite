import React, { useEffect, useMemo, useRef, useState } from "react";
import {
  Search,
  MousePointerClick,
  Layers,
  HardDrive,
  Trash2,
  AlertTriangle,
  RotateCw,
  Power,
  RotateCcw,
  LogOut,
  FolderPlus,
  Info,
  Palette,
  Check,
} from "lucide-react";
import { APPS, FS, OSProvider, useOS, type AppId } from "./os";
import MenuBar, { LogoMark } from "./MenuBar";
import Dock, { FinderFace } from "./Dock";
import { G1Icon } from "./icons/IconSystem";
import WindowFrame from "./WindowFrame";
import FinderApp from "./apps/FinderApp";
import SettingsApp from "./apps/SettingsApp";
import TerminalApp from "./apps/TerminalApp";
import InstallerApp from "./apps/InstallerApp";
import BrowserApp from "./apps/BrowserApp";
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
    case "installer": return <InstallerApp />;
    case "browser": return <BrowserApp />;
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
      ? Object.entries(os.fs).flatMap(([loc, items]) =>
          items.filter((f) => f.name.toLowerCase().includes(needle)).slice(0, 4)
            .map((f) => ({ kind: "file" as const, id: f.name, name: f.name, sub: `${loc} · ${f.size}`, f })))
      : [];
    return [...apps, ...files].slice(0, 7);
  }, [q, os.fs]);

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
        className="glass panel-in w-[560px] max-w-[86%] overflow-hidden rounded-2xl bg-[rgba(248,248,252,0.85)] shadow-[0_28px_90px_rgba(10,15,40,0.45),0_0_0_0.5px_rgba(0,0,0,0.2)]"
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
            className="w-full bg-transparent text-[19px] font-light outline-none placeholder:text-black/30 text-black"
          />
        </div>
        {results.length > 0 && (
          <div className="border-t border-black/10 p-1.5">
            {results.map((r, i) => (
              <button
                key={r.kind + r.id}
                onMouseEnter={() => setHi(i)}
                onClick={() => { run(r); os.setSpotlight(false); }}
                className={`flex w-full items-center justify-between rounded-lg px-3 py-2 text-left ${i === hi ? "bg-[var(--acc)] text-white" : "text-black/85"}`}
              >
                <span className="text-[13.5px] font-medium">{r.name}</span>
                <span className={`text-[11px] ${i === hi ? "text-white/75" : "text-black/40"}`}>{r.sub}</span>
              </button>
            ))}
          </div>
        )}
        <div className="flex items-center justify-between border-t border-black/10 bg-black/[0.03] px-4 py-1.5 text-[10.5px] text-black/40">
          <span>G1OS Fast Query · 0 background daemons</span>
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
    <div className="glass absolute inset-0 z-[590] bg-[rgba(30,35,50,0.65)] backdrop-blur-md pt-[9vh]" onClick={() => os.setLaunchpad(false)}>
      <div className="mx-auto mb-8 flex w-64 items-center gap-2 rounded-full bg-white/20 border border-white/20 px-4 py-2 backdrop-blur" onClick={(e) => e.stopPropagation()}>
        <Search size={15} className="text-white/80" />
        <input autoFocus value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search G1OS Apps" className="w-full bg-transparent text-[13px] text-white outline-none placeholder:text-white/60" />
      </div>
      <div className="mx-auto grid max-w-[780px] grid-cols-4 gap-y-8 px-8 sm:grid-cols-5" onClick={(e) => e.stopPropagation()}>
        {apps.map((a) => (
          <button key={a.id} className="group flex flex-col items-center gap-2" onClick={() => { os.setLaunchpad(false); os.openApp(a.id); }}>
            <div className="transition-transform duration-200 group-hover:scale-110 drop-shadow-md">
              <G1Icon name={a.id} size={58} />
            </div>
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
    <div className="pointer-events-none absolute right-3 top-9 z-[580] flex w-[320px] flex-col gap-2">
      {visible.map((n) => (
        <button
          key={n.id}
          className="glass note-in pointer-events-auto rounded-2xl bg-[rgba(250,250,253,0.92)] p-3 text-left text-black shadow-[0_14px_44px_rgba(10,15,40,0.3),0_0_0_0.5px_rgba(0,0,0,0.12)] border border-black/5"
          onClick={() => setGone((g) => [...g, n.id])}
        >
          <div className="flex items-start gap-2.5">
            <LogoMark size={26} />
            <div className="min-w-0">
              <div className="flex items-baseline justify-between gap-3">
                <span className="text-[13px] font-semibold">{n.title}</span>
                <span className="flex-none text-[10.5px] text-black/40">{n.time}</span>
              </div>
              <div className="mt-0.5 text-[12px] leading-snug text-black/65">{n.body}</div>
            </div>
          </div>
        </button>
      ))}
    </div>
  );
}



/* ---------------- System Dialogs ---------------- */

function SystemDialogModal() {
  const os = useOS();
  if (
    os.powerState !== "shutdown_dialog" &&
    os.powerState !== "restart_dialog" &&
    os.powerState !== "logout_dialog" &&
    os.powerState !== "trash_dialog"
  ) {
    return null;
  }

  const handleConfirm = () => {
    if (os.powerState === "shutdown_dialog") {
      os.setPowerState("shutting_down");
      setTimeout(() => {
        window.location.reload();
      }, 2500);
    } else if (os.powerState === "restart_dialog") {
      os.setPowerState("restarting");
      setTimeout(() => {
        window.location.reload();
      }, 2500);
    } else if (os.powerState === "logout_dialog") {
      os.setPowerState("running");
      window.location.reload();
    } else if (os.powerState === "trash_dialog") {
      os.emptyTrash();
      os.setPowerState("running");
    }
  };

  const getDialogDetails = () => {
    switch (os.powerState) {
      case "shutdown_dialog":
        return {
          title: "Shut Down G1OS?",
          body: "Are you sure you want to shut down your iMac now? All unsaved work will be preserved.",
          confirmBtn: "Shut Down",
          confirmColor: "bg-red-600 hover:bg-red-500",
          icon: <G1Icon name="shutdown" size={32} className="text-red-400" />,
        };
      case "restart_dialog":
        return {
          title: "Restart G1OS?",
          body: "Are you sure you want to restart your iMac now? The computer will boot back into G1OS.",
          confirmBtn: "Restart",
          confirmColor: "bg-blue-600 hover:bg-blue-500",
          icon: <G1Icon name="restart" size={32} className="text-blue-400" />,
        };
      case "logout_dialog":
        return {
          title: "Log Out of G1OS?",
          body: "Are you sure you want to log out of Jeevan? Open applications will be safely closed.",
          confirmBtn: "Log Out",
          confirmColor: "bg-blue-600 hover:bg-blue-500",
          icon: <G1Icon name="logout" size={32} className="text-blue-400" />,
        };
      case "trash_dialog":
        return {
          title: "Empty Trash?",
          body: "Are you sure you want to permanently erase all items in the Trash? You cannot undo this action.",
          confirmBtn: "Empty Trash",
          confirmColor: "bg-red-600 hover:bg-red-500",
          icon: <G1Icon name="trash" size={32} className="text-red-400" />,
        };
      default:
        return { title: "", body: "", confirmBtn: "OK", confirmColor: "bg-blue-600", icon: null };
    }
  };

  const d = getDialogDetails();

  return (
    <div className="fixed inset-0 z-[800] grid place-items-center bg-black/50 backdrop-blur-sm animate-in fade-in duration-200">
      <div className="w-[420px] max-w-[92%] rounded-2xl border border-white/20 bg-[#252834]/95 p-6 shadow-2xl backdrop-blur-xl text-center space-y-4 animate-in zoom-in-95 duration-200">
        <div className="mx-auto grid h-14 w-14 place-items-center rounded-2xl bg-white/10 shadow-inner">
          {d.icon}
        </div>

        <div>
          <h3 className="text-[17px] font-bold text-white tracking-wide">{d.title}</h3>
          <p className="mt-1.5 text-[13px] leading-relaxed text-white/70">{d.body}</p>
        </div>

        <div className="flex items-center justify-end gap-3 pt-3">
          <button
            type="button"
            onClick={() => os.setPowerState("running")}
            className="rounded-xl bg-white/10 px-5 py-2 text-[13px] font-medium text-white hover:bg-white/20 transition-colors cursor-pointer"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleConfirm}
            className={`rounded-xl px-5 py-2 text-[13px] font-semibold text-white shadow-lg transition-all cursor-pointer ${d.confirmColor}`}
          >
            {d.confirmBtn}
          </button>
        </div>
      </div>
    </div>
  );
}

/* ---------------- Power Overlays ---------------- */

function PowerOverlay() {
  const os = useOS();
  if (os.powerState !== "shutting_down" && os.powerState !== "restarting") {
    return null;
  }

  const isRestart = os.powerState === "restarting";

  return (
    <div className="fixed inset-0 z-[999] grid place-items-center bg-[#050608] select-none animate-in fade-in duration-700">
      <div className="flex flex-col items-center space-y-4 text-center">
        <div className="relative mb-2">
          <LogoMark size={70} />
          <span className="absolute -bottom-1 -right-1 flex h-3.5 w-3.5">
            <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-blue-400 opacity-75" />
            <span className="relative inline-flex rounded-full h-3.5 w-3.5 bg-blue-500" />
          </span>
        </div>

        <div className="h-6 w-6 border-2 border-white/20 border-t-white rounded-full animate-spin" />

        <div className="space-y-1 pt-1">
          <h2 className="text-[20px] font-semibold tracking-wide text-white">
            {isRestart ? "Restarting G1OS…" : "Shutting Down G1OS…"}
          </h2>
          <p className="text-[12px] text-white/50">Giving life to older machines.</p>
        </div>
      </div>
    </div>
  );
}

/* ---------------- stage ---------------- */

function Stage() {
  const os = useOS();
  const [hint, setHint] = useState(true);
  const [selectedDesktopIcon, setSelectedDesktopIcon] = useState<string | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number } | null>(null);
  const booted = useRef(false);

  useEffect(() => {
    if (booted.current) return;
    booted.current = true;
    const t1 = setTimeout(() => os.openApp("finder"), 700);
    const t2 = setTimeout(
      () =>
        os.notify(
          "G1OS 1.0",
          "Welcome to G1OS",
          "Giving life to older machines. 384 MB RAM · 0.4% CPU · 1080p 60fps."
        ),
      1600
    );
    const t3 = setTimeout(() => setHint(false), 9000);
    return () => {
      clearTimeout(t1);
      clearTimeout(t2);
      clearTimeout(t3);
    };
  }, []);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.code === "Space") {
        e.preventDefault();
        os.setSpotlight(!os.spotlight);
      } else if (e.key === "Escape") {
        os.setSpotlight(false);
        os.setLaunchpad(false);
        os.setMenuOpen(null);
        setSelectedDesktopIcon(null);
        setContextMenu(null);
      } else if (e.altKey && ["1", "2", "3"].includes(e.key)) {
        e.preventDefault();
        os.setWs(+e.key - 1);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [os.spotlight]);

  const handleContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
    setContextMenu({ x: e.clientX, y: e.clientY });
  };

  const trashCount = (os.fs.Trash || []).length;

  return (
    <div
      className={`os-stage relative h-full w-full select-none ${os.perfMode ? "perf" : ""} ${
        os.visualMode === "performance" ? "mode-perf" : os.visualMode === "beautiful" ? "mode-beautiful" : ""
      }`}
      style={{ ["--acc" as any]: os.accent }}
      onClick={() => {
        setSelectedDesktopIcon(null);
        setContextMenu(null);
      }}
      onContextMenu={handleContextMenu}
    >
      {/* wallpapers */}
      {WALLPAPERS.map((w, i) => (
        <div key={i} className="wallpaper" style={{ ...w, opacity: os.wall === i ? 1 : 0 }} />
      ))}

      <MenuBar />

      {/* Desktop Context Menu */}
      {contextMenu && (
        <div
          className="fixed z-[650] min-w-[200px] rounded-xl border border-white/20 bg-[#252834]/90 p-1.5 shadow-2xl backdrop-blur-xl text-[12.5px] text-white space-y-0.5 animate-in fade-in duration-100"
          style={{ top: contextMenu.y, left: contextMenu.x }}
          onClick={(e) => e.stopPropagation()}
        >
          <button
            onClick={() => {
              os.notify("Finder", "New Folder", "Created 'Untitled Folder' on Desktop.");
              setContextMenu(null);
            }}
            className="w-full flex items-center gap-2 rounded-lg px-2.5 py-1.5 hover:bg-blue-600 text-left transition-colors cursor-pointer"
          >
            <FolderPlus size={14} /> New Folder
          </button>
          <button
            onClick={() => {
              os.openApp("sysinfo");
              setContextMenu(null);
            }}
            className="w-full flex items-center gap-2 rounded-lg px-2.5 py-1.5 hover:bg-blue-600 text-left transition-colors cursor-pointer"
          >
            <Info size={14} /> Get Info
          </button>
          <div className="h-px bg-white/10 my-1" />
          <button
            onClick={() => {
              os.setWall((os.wall + 1) % WALLPAPERS.length);
              setContextMenu(null);
            }}
            className="w-full flex items-center gap-2 rounded-lg px-2.5 py-1.5 hover:bg-blue-600 text-left transition-colors cursor-pointer"
          >
            <Palette size={14} /> Change Wallpaper…
          </button>
          <button
            onClick={() => {
              os.openApp("settings");
              setContextMenu(null);
            }}
            className="w-full flex items-center gap-2 rounded-lg px-2.5 py-1.5 hover:bg-blue-600 text-left transition-colors cursor-pointer"
          >
            G1OS Settings
          </button>
        </div>
      )}

      {/* Desktop Volume & File Icons (Arranged in top-right grid with macOS layout) */}
      <div
        className="absolute right-5 top-12 z-[30] flex flex-col items-center gap-5 select-none"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Macintosh HD */}
        <button
          onClick={() => {
            setSelectedDesktopIcon("hd");
            os.openApp("finder", "Recents", "Macintosh HD");
          }}
          className="group flex w-[84px] flex-col items-center gap-1.5 p-1 transition-all active:scale-95 cursor-pointer"
        >
          <div className="transition-transform group-hover:scale-105">
            <G1Icon name="macintosh-hd" size={48} />
          </div>
          <span
            className={`max-w-[82px] text-center text-[11.5px] font-medium leading-tight line-clamp-2 px-1.5 py-0.5 rounded transition-colors drop-shadow-[0_1px_2px_rgba(0,0,0,0.95)] ${
              selectedDesktopIcon === "hd"
                ? "bg-[#0a84ff] text-white shadow"
                : "text-white group-hover:text-white/95"
            }`}
          >
            Macintosh HD
          </span>
        </button>

        {/* Install G1OS */}
        <button
          onClick={() => {
            setSelectedDesktopIcon("installer");
            os.openApp("installer");
          }}
          className="group flex w-[84px] flex-col items-center gap-1.5 p-1 transition-all active:scale-95 cursor-pointer"
        >
          <div className="transition-transform group-hover:scale-105">
            <G1Icon name="installer" size={48} />
          </div>
          <span
            className={`max-w-[82px] text-center text-[11.5px] font-medium leading-tight line-clamp-2 px-1.5 py-0.5 rounded transition-colors drop-shadow-[0_1px_2px_rgba(0,0,0,0.95)] ${
              selectedDesktopIcon === "installer"
                ? "bg-[#0a84ff] text-white shadow"
                : "text-white group-hover:text-white/95"
            }`}
          >
            Install G1OS
          </span>
        </button>

        {/* G1OS Live USB */}
        <button
          onClick={() => {
            setSelectedDesktopIcon("usb");
            os.openApp("finder", "Downloads", "G1OS USB");
          }}
          className="group flex w-[84px] flex-col items-center gap-1.5 p-1 transition-all active:scale-95 cursor-pointer"
        >
          <div className="transition-transform group-hover:scale-105">
            <G1Icon name="usb-drive" size={48} />
          </div>
          <span
            className={`max-w-[82px] text-center text-[11.5px] font-medium leading-tight line-clamp-2 px-1.5 py-0.5 rounded transition-colors drop-shadow-[0_1px_2px_rgba(0,0,0,0.95)] ${
              selectedDesktopIcon === "usb"
                ? "bg-[#0a84ff] text-white shadow"
                : "text-white group-hover:text-white/95"
            }`}
          >
            G1OS USB
          </span>
        </button>

        {/* Documents */}
        <button
          onClick={() => {
            setSelectedDesktopIcon("docs");
            os.openApp("finder", "Documents", "Documents");
          }}
          className="group flex w-[84px] flex-col items-center gap-1.5 p-1 transition-all active:scale-95 cursor-pointer"
        >
          <div className="transition-transform group-hover:scale-105">
            <G1Icon name="folder-documents" size={48} />
          </div>
          <span
            className={`max-w-[82px] text-center text-[11.5px] font-medium leading-tight line-clamp-2 px-1.5 py-0.5 rounded transition-colors drop-shadow-[0_1px_2px_rgba(0,0,0,0.95)] ${
              selectedDesktopIcon === "docs"
                ? "bg-[#0a84ff] text-white shadow"
                : "text-white group-hover:text-white/95"
            }`}
          >
            Documents
          </span>
        </button>

        {/* Trash */}
        <button
          onClick={() => {
            setSelectedDesktopIcon("trash");
            os.openApp("finder", "Trash", "Trash");
          }}
          onContextMenu={(e) => {
            e.preventDefault();
            e.stopPropagation();
            os.setPowerState("trash_dialog");
          }}
          className="group flex w-[84px] flex-col items-center gap-1.5 p-1 transition-all active:scale-95 cursor-pointer"
        >
          <div className="transition-transform group-hover:scale-105">
            <G1Icon name="trash" size={36} count={trashCount} className="text-white drop-shadow" />
          </div>
          <span
            className={`max-w-[82px] text-center text-[11.5px] font-medium leading-tight line-clamp-2 px-1.5 py-0.5 rounded transition-colors drop-shadow-[0_1px_2px_rgba(0,0,0,0.95)] ${
              selectedDesktopIcon === "trash"
                ? "bg-[#0a84ff] text-white shadow"
                : "text-white group-hover:text-white/95"
            }`}
          >
            Trash
          </span>
        </button>
      </div>

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
      <SystemDialogModal />
      <PowerOverlay />

      {/* hint */}
      {hint && (
        <div className="pointer-events-none absolute left-1/2 top-[12%] z-[560] -translate-x-1/2">
          <div className="glass flex items-center gap-2.5 rounded-full bg-[rgba(20,22,30,0.55)] px-4 py-2 text-[12px] text-white shadow-xl">
            <MousePointerClick size={14} className="text-white/80" />
            <span>Interactive G1OS — click Dock, right-click desktop, press</span>
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
