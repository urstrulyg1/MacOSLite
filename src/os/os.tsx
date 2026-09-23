import React, {
  createContext, useCallback, useContext, useMemo, useRef, useState,
} from "react";
import type { LucideIcon } from "lucide-react";
import {
  Calculator as CalcIcon, Image as ImageIcon, Info, Film, Music2,
  Settings2, SquareTerminal, StickyNote, LayoutGrid, Folder as FolderIcon,
  FileText, FileArchive, Table2, Presentation, HardDrive, Compass,
} from "lucide-react";

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

export type AppId =
  | "finder" | "installer" | "browser" | "launchpad" | "settings" | "terminal" | "textedit"
  | "calc" | "music" | "photos" | "player" | "sysinfo";

export type PowerState =
  | "running"
  | "shutdown_dialog"
  | "restart_dialog"
  | "logout_dialog"
  | "trash_dialog"
  | "shutting_down"
  | "restarting";

export type VisualMode = "performance" | "balanced" | "beautiful";

export type WinAnimState = "opening" | "active" | "closing";

export interface Win {
  id: number;
  app: AppId;
  title: string;
  x: number; y: number; w: number; h: number;
  z: number;
  min: boolean;
  full: boolean;
  saved?: { x: number; y: number; w: number; h: number };
  payload?: string;
  ws: number;
  animState?: WinAnimState;
}

export interface Note { id: number; app: string; title: string; body: string; time: string; }

export type FSKind = "folder" | "text" | "pdf" | "image" | "video" | "audio" | "archive" | "sheet" | "keynote";
export interface FSItem { name: string; kind: FSKind; size: string; meta?: string; src?: string; }

export interface AppDef {
  id: AppId;
  name: string;
  icon: LucideIcon | "finder" | "browser";
  tile: string;           // gradient classes for squircle
  glyph: string;          // glyph color
  size: [number, number];
  dock: boolean;
}

/* ------------------------------------------------------------------ */
/* app registry                                                        */
/* ------------------------------------------------------------------ */

export const APPS: Record<AppId, AppDef> = {
  finder:   { id: "finder",   name: "Finder",          icon: "finder",      tile: "from-sky-400 to-blue-600",      glyph: "text-white", size: [860, 540], dock: true },
  installer:{ id: "installer",name: "Install G1OS",    icon: HardDrive,     tile: "from-sky-500 to-blue-700",      glyph: "text-white", size: [720, 520], dock: true },
  browser:  { id: "browser",  name: "Safari",          icon: Compass,       tile: "from-sky-300 via-blue-500 to-indigo-600", glyph: "text-white", size: [900, 580], dock: true },
  launchpad:{ id: "launchpad",name: "Launchpad",       icon: LayoutGrid,    tile: "from-slate-400 to-slate-600",   glyph: "text-white", size: [700, 480], dock: true },
  settings: { id: "settings", name: "System Settings", icon: Settings2,     tile: "from-zinc-400 to-zinc-600",     glyph: "text-white", size: [780, 550], dock: true },
  terminal: { id: "terminal", name: "Terminal",        icon: SquareTerminal,tile: "from-zinc-700 to-zinc-900",     glyph: "text-emerald-300", size: [680, 440], dock: true },
  textedit: { id: "textedit", name: "TextEdit",        icon: StickyNote,    tile: "from-amber-100 to-amber-300",   glyph: "text-amber-900", size: [620, 460], dock: true },
  calc:     { id: "calc",     name: "Calculator",      icon: CalcIcon,      tile: "from-orange-400 to-orange-600", glyph: "text-white", size: [280, 420], dock: true },
  music:    { id: "music",    name: "Music",           icon: Music2,        tile: "from-rose-400 to-red-500",      glyph: "text-white", size: [600, 400], dock: true },
  photos:   { id: "photos",   name: "Photos",          icon: ImageIcon,     tile: "from-teal-300 to-cyan-500",     glyph: "text-white", size: [760, 540], dock: true },
  player:   { id: "player",   name: "QuickPlayer",     icon: Film,          tile: "from-violet-400 to-purple-600", glyph: "text-white", size: [820, 560], dock: true },
  sysinfo:  { id: "sysinfo",  name: "About G1OS",      icon: Info,          tile: "from-sky-300 to-indigo-500",    glyph: "text-white", size: [620, 540], dock: false },
};

export const DOCK_ORDER: AppId[] = [
  "finder",
  "installer",
  "browser",
  "launchpad",
  "settings",
  "terminal",
  "music",
  "photos",
  "player",
  "calc",
  "textedit",
];

/* ------------------------------------------------------------------ */
/* file system                                                         */
/* ------------------------------------------------------------------ */

export const INITIAL_FS: Record<string, FSItem[]> = {
  Recents: [
    { name: "reef-drift-1080p.mp4", kind: "video", size: "84.2 MB", meta: "H.264 · VDPAU 1080p" },
    { name: "morning-bloom.flac", kind: "audio", size: "21.6 MB", meta: "FLAC 24/96" },
    { name: "valley-sunrise.jpg", kind: "image", size: "6.1 MB", meta: "4288 × 2848" },
    { name: "giving-life-to-older-machines.md", kind: "text", size: "4 KB" },
    { name: "G1OS-1.0.iso", kind: "archive", size: "612 MB" },
  ],
  Documents: [
    { name: "G1OS Manifesto.md", kind: "text", size: "4 KB", meta: "Edited today" },
    { name: "iMac 2010 Architecture.key", kind: "keynote", size: "1.2 MB" },
    { name: "Hardware Benchmarks.numbers", kind: "sheet", size: "88 KB" },
    { name: "AppleSMC Thermal Control.txt", kind: "text", size: "11 KB" },
    { name: "iMac 11,2 Hardware Study.pdf", kind: "pdf", size: "2.4 MB" },
  ],
  Downloads: [
    { name: "G1OS-1.0-x86_64.iso", kind: "archive", size: "612 MB", meta: "sha256 verified" },
    { name: "firmware-bcm43224.tar", kind: "archive", size: "1.1 MB" },
    { name: "mesa-r600-acceleration.txt", kind: "text", size: "9 KB" },
  ],
  Movies: [
    { name: "reef-drift-1080p.mp4", kind: "video", size: "84.2 MB", meta: "H.264 High@L4.1", src: "video" },
    { name: "Ocean Tide Pools.m4v", kind: "video", size: "142 MB", meta: "1080p24" },
  ],
  Music: [
    { name: "morning-bloom.flac", kind: "audio", size: "21.6 MB", meta: "FLAC 24-bit/96 kHz", src: "audio" },
    { name: "granite-skyline.opus", kind: "audio", size: "6.8 MB", meta: "Opus 160k" },
    { name: "low-sun.alac", kind: "audio", size: "34.1 MB", meta: "ALAC lossless" },
  ],
  Pictures: [
    { name: "valley-sunrise.jpg", kind: "image", size: "6.1 MB", meta: "4288 × 2848", src: "image" },
    { name: "wall-dunes.heic", kind: "image", size: "3.9 MB" },
    { name: "keystones.png", kind: "image", size: "1.8 MB" },
  ],
  Trash: [
    { name: "old-installer-cache.tmp", kind: "archive", size: "32 MB" },
  ],
};

export const VIDEO_SRC =
  "https://videos.pexels.com/video-files/6989127/6989127-hd_1920_1080_25fps.mp4";

/* ------------------------------------------------------------------ */
/* context                                                             */
/* ------------------------------------------------------------------ */

interface DockCfg { size: number; mag: boolean; magScale: number; autohide: boolean; pos: "bottom" | "left" | "right"; }

interface OSCtx {
  wins: Win[];
  notes: Note[];
  openApp: (app: AppId, payload?: string, title?: string) => void;
  closeWin: (id: number) => void;
  focusWin: (id: number) => void;
  setMin: (id: number, v: boolean) => void;
  toggleFull: (id: number) => void;
  moveWin: (id: number, x: number, y: number) => void;
  resizeWin: (id: number, w: number, h: number) => void;
  notify: (app: string, title: string, body: string) => void;
  dismissNote: (id: number) => void;
  clearNotes: () => void;
  activeApp: AppId;
  menuOpen: string | null; setMenuOpen: (m: string | null) => void;
  spotlight: boolean; setSpotlight: (v: boolean) => void;
  launchpad: boolean; setLaunchpad: (v: boolean) => void;
  ws: number; setWs: (n: number) => void;
  accent: string; setAccent: (a: string) => void;
  perfMode: boolean; setPerfMode: (v: boolean) => void;
  visualMode: VisualMode; setVisualMode: (m: VisualMode) => void;
  reduceMotion: boolean; setReduceMotion: (v: boolean) => void;
  wall: number; setWall: (n: number) => void;
  dock: DockCfg; setDock: (d: Partial<DockCfg>) => void;
  finderLoc: string; setFinderLoc: (l: string) => void;
  finderView: "icon" | "list"; setFinderView: (v: "icon" | "list") => void;
  soundVol: number; setSoundVol: (v: number) => void;
  powerState: PowerState; setPowerState: (s: PowerState) => void;
  isLoggedIn: boolean; setIsLoggedIn: (v: boolean) => void;
  bouncingApp: AppId | null; triggerAppBounce: (a: AppId) => void;
  fs: Record<string, FSItem[]>;
  emptyTrash: () => void;
  moveToTrash: (item: FSItem) => void;
}

const Ctx = createContext<OSCtx>(null as any);
export const useOS = () => useContext(Ctx);

const now = () =>
  new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });

export function OSProvider({ children, stageW, stageH }: { children: React.ReactNode; stageW: number; stageH: number }) {
  const [wins, setWins] = useState<Win[]>([]);
  const [notes, setNotes] = useState<Note[]>([]);
  const [menuOpen, setMenuOpen] = useState<string | null>(null);
  const [spotlight, setSpotlight] = useState(false);
  const [launchpad, setLaunchpad] = useState(false);
  const [ws, setWs] = useState(0);
  const [accent, setAccentState] = useState("#0a84ff");
  const [perfMode, setPerfMode] = useState(false);
  const [visualMode, setVisualModeState] = useState<VisualMode>("balanced");
  const [reduceMotion, setReduceMotion] = useState(false);
  const [wall, setWall] = useState(0);
  const [dock, setDockCfg] = useState<DockCfg>({ size: 52, mag: true, magScale: 0.45, autohide: false, pos: "bottom" });
  const [finderLoc, setFinderLoc] = useState("Recents");
  const [finderView, setFinderView] = useState<"icon" | "list">("icon");
  const [soundVol, setSoundVol] = useState(68);
  const [powerState, setPowerState] = useState<PowerState>("running");
  const [isLoggedIn, setIsLoggedIn] = useState(true);
  const [bouncingApp, setBouncingApp] = useState<AppId | null>(null);
  const [fs, setFs] = useState<Record<string, FSItem[]>>(INITIAL_FS);

  const zTop = useRef(10);
  const idSeq = useRef(1);
  const noteSeq = useRef(0);
  const cascade = useRef(0);

  const setVisualMode = useCallback((m: VisualMode) => {
    setVisualModeState(m);
    setPerfMode(m === "performance");
  }, []);

  const triggerAppBounce = useCallback((app: AppId) => {
    setBouncingApp(app);
    setTimeout(() => setBouncingApp(null), 1200);
  }, []);

  const focusWin = useCallback((id: number) => {
    zTop.current += 1;
    setWins((w) => w.map((x) => (x.id === id ? { ...x, z: zTop.current, min: false } : x)));
  }, []);

  const openApp = useCallback((app: AppId, payload?: string, title?: string) => {
    if (app === "launchpad") { setLaunchpad((v) => !v); return; }
    triggerAppBounce(app);

    setWins((w) => {
      const existing = payload == null ? w.find((x) => x.app === app) : undefined;
      if (existing && payload == null) {
        zTop.current += 1;
        return w.map((x) => (x.id === existing.id ? { ...x, z: zTop.current, min: false, animState: "active" } : x));
      }
      zTop.current += 1;
      const def = APPS[app] || APPS.finder;
      const off = (cascade.current++ % 5) * 26;
      const [cw, ch] = def.size;
      const ww = Math.min(cw, stageW - 24);
      const wh = Math.min(ch, stageH - 70);
      const newId = idSeq.current++;
      const win: Win = {
        id: newId,
        app,
        title: title ?? def.name,
        x: Math.max(12, (stageW - ww) / 2 + off - 40),
        y: Math.max(40, (stageH - wh) / 2.4 + off),
        w: ww, h: wh,
        z: zTop.current,
        min: false, full: false,
        payload,
        ws,
        animState: "opening",
      };

      // Transition smoothly from opening to active on next animation frame
      requestAnimationFrame(() => {
        setWins((curr) =>
          curr.map((item) => (item.id === newId ? { ...item, animState: "active" } : item))
        );
      });

      return [...w, win];
    });
  }, [stageW, stageH, ws, triggerAppBounce]);

  const closeWin = useCallback((id: number) => {
    // Play macOS close dissolution animation before removing from state
    setWins((w) => w.map((x) => (x.id === id ? { ...x, animState: "closing" } : x)));
    setTimeout(() => {
      setWins((w) => w.filter((x) => x.id !== id));
    }, 190);
  }, []);

  const setMin = useCallback((id: number, v: boolean) => {
    setWins((w) =>
      w.map((x) => {
        if (x.id !== id) return x;
        if (!v) {
          // Restoring from dock
          zTop.current += 1;
          return { ...x, min: false, z: zTop.current, animState: "active" };
        }
        return { ...x, min: true };
      })
    );
  }, []);
  const toggleFull = useCallback((id: number) =>
    setWins((w) =>
      w.map((x) => {
        if (x.id !== id) return x;
        if (!x.full) {
          zTop.current += 1;
          return { ...x, full: true, saved: { x: x.x, y: x.y, w: x.w, h: x.h }, x: 0, y: 28, w: stageW, h: stageH - 28, z: zTop.current };
        }
        const s = x.saved!;
        return { ...x, full: false, x: s.x, y: s.y, w: s.w, h: s.h };
      })
    ), [stageW, stageH]);

  const moveWin = useCallback((id: number, x: number, y: number) =>
    setWins((w) => w.map((t) => (t.id === id ? { ...t, x, y } : t))), []);
  const resizeWin = useCallback((id: number, w2: number, h2: number) =>
    setWins((w) => w.map((t) => (t.id === id ? { ...t, w: Math.max(280, w2), h: Math.max(220, h2) } : t))), []);

  const notify = useCallback((app: string, title: string, body: string) => {
    noteSeq.current += 1;
    setNotes((n) => [...n.slice(-6), { id: noteSeq.current, app, title, body, time: now() }]);
  }, []);
  const dismissNote = useCallback((id: number) => setNotes((n) => n.filter((x) => x.id !== id)), []);
  const clearNotes = useCallback(() => setNotes([]), []);

  const setAccent = useCallback((a: string) => setAccentState(a), []);
  const setDock = useCallback((d: Partial<DockCfg>) => setDockCfg((c) => ({ ...c, ...d })), []);

  const emptyTrash = useCallback(() => {
    setFs((prev) => ({ ...prev, Trash: [] }));
    notify("Finder", "Trash Emptied", "All items have been permanently deleted.");
  }, [notify]);

  const moveToTrash = useCallback((item: FSItem) => {
    setFs((prev) => ({
      ...prev,
      Trash: [...(prev.Trash || []), item],
    }));
    notify("Finder", "Moved to Trash", `"${item.name}" was moved to the Trash.`);
  }, [notify]);

  const activeApp: AppId = useMemo(() => {
    const visible = wins.filter((w) => !w.min && w.ws === ws);
    if (!visible.length) return "finder";
    return visible.reduce((a, b) => (a.z > b.z ? a : b)).app;
  }, [wins, ws]);

  const val: OSCtx = {
    wins, notes, openApp, closeWin, focusWin, setMin, toggleFull, moveWin, resizeWin,
    notify, dismissNote, clearNotes, activeApp,
    menuOpen, setMenuOpen, spotlight, setSpotlight, launchpad, setLaunchpad,
    ws, setWs, accent, setAccent, perfMode, setPerfMode, visualMode, setVisualMode,
    reduceMotion, setReduceMotion, wall, setWall,
    dock, setDock, finderLoc, setFinderLoc, finderView, setFinderView,
    soundVol, setSoundVol, powerState, setPowerState, isLoggedIn, setIsLoggedIn,
    bouncingApp, triggerAppBounce, fs, emptyTrash, moveToTrash,
  };

  return <Ctx.Provider value={val}>{children}</Ctx.Provider>;
}

/* file-kind glyphs */
export const FILE_ICON: Record<FSKind, LucideIcon> = {
  folder: FolderIcon,
  text: FileText,
  pdf: FileText,
  image: ImageIcon,
  video: Film,
  audio: Music2,
  archive: FileArchive,
  sheet: Table2,
  keynote: Presentation,
};
