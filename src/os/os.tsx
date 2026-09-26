import React, {
  createContext, useCallback, useContext, useEffect, useMemo, useRef, useState,
} from "react";
import { motionProfile } from "./motion";

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
export type Appearance = "light" | "dark";
export type FinderView = "icon" | "list" | "column";
export type WinAnimState = "opening" | "active" | "closing" | "minimizing" | "restoring";
export type HudKind = "volume" | "brightness" | "keyboard" | "media";
export interface HudState { kind: HudKind; value: number; label: string; }

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

export interface Note { id: number; app: string; title: string; body: string; time: string; icon?: string; }

export type FSKind = "folder" | "text" | "pdf" | "image" | "video" | "audio" | "archive" | "sheet" | "keynote";
export interface FSItem { name: string; kind: FSKind; size: string; meta?: string; src?: string; born?: boolean; }

export interface AppDef {
  id: AppId;
  name: string;
  icon: string;
  tile: string;
  glyph: string;
  size: [number, number];
  dock: boolean;
}

export const APPS: Record<AppId, AppDef> = {
  finder:    { id: "finder",    name: "Finder",          icon: "finder",    tile: "from-sky-400 to-blue-600", glyph: "text-white", size: [860, 540], dock: true },
  installer: { id: "installer", name: "Install G1OS",    icon: "installer", tile: "from-sky-500 to-blue-700", glyph: "text-white", size: [720, 520], dock: true },
  browser:   { id: "browser",   name: "Safari",          icon: "browser",   tile: "from-sky-300 via-blue-500 to-indigo-600", glyph: "text-white", size: [900, 580], dock: true },
  launchpad: { id: "launchpad", name: "Launchpad",       icon: "launchpad", tile: "from-slate-400 to-slate-600", glyph: "text-white", size: [700, 480], dock: true },
  settings:  { id: "settings",  name: "System Settings", icon: "settings",  tile: "from-zinc-400 to-zinc-600", glyph: "text-white", size: [780, 550], dock: true },
  terminal:  { id: "terminal",  name: "Terminal",        icon: "terminal",  tile: "from-zinc-700 to-zinc-900", glyph: "text-emerald-300", size: [680, 440], dock: true },
  textedit:  { id: "textedit",  name: "TextEdit",        icon: "textedit",  tile: "from-amber-100 to-amber-300", glyph: "text-amber-900", size: [620, 460], dock: true },
  calc:      { id: "calc",      name: "Calculator",      icon: "calc",      tile: "from-orange-400 to-orange-600", glyph: "text-white", size: [280, 420], dock: true },
  music:     { id: "music",     name: "Music",           icon: "music",     tile: "from-rose-400 to-red-500", glyph: "text-white", size: [600, 400], dock: true },
  photos:    { id: "photos",    name: "Photos",          icon: "photos",    tile: "from-teal-300 to-cyan-500", glyph: "text-white", size: [760, 540], dock: true },
  player:    { id: "player",    name: "QuickPlayer",     icon: "player",    tile: "from-violet-400 to-purple-600", glyph: "text-white", size: [820, 560], dock: true },
  sysinfo:   { id: "sysinfo",   name: "About G1OS",      icon: "sysinfo",   tile: "from-sky-300 to-indigo-500", glyph: "text-white", size: [620, 540], dock: false },
};

export const DOCK_ORDER: AppId[] = [
  "finder", "installer", "browser", "launchpad", "settings", "terminal",
  "music", "photos", "player", "calc", "textedit",
];

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
  Desktop: [
    { name: "Giving life to older machines.md", kind: "text", size: "2 KB", meta: "On the desktop" },
  ],
  Trash: [
    { name: "old-installer-cache.tmp", kind: "archive", size: "32 MB" },
  ],
};

export const VIDEO_SRC =
  "https://videos.pexels.com/video-files/6989127/6989127-hd_1920_1080_25fps.mp4";

export const FILE_ICON: Record<FSKind, string> = {
  folder: "folder",
  text: "file-text",
  pdf: "file-pdf",
  image: "file-image",
  video: "file-video",
  audio: "file-audio",
  archive: "file-archive",
  sheet: "file-sheet",
  keynote: "file-presentation",
};

interface DockCfg { size: number; mag: boolean; magScale: number; autohide: boolean; pos: "bottom" | "left" | "right"; }

interface OSCtx {
  wins: Win[];
  notes: Note[];
  openApp: (app: AppId, payload?: string, title?: string) => void;
  closeWin: (id: number) => void;
  focusWin: (id: number) => void;
  setMin: (id: number, v: boolean) => void;
  finishMin: (id: number) => void;
  settleAnim: (id: number) => void;
  toggleFull: (id: number) => void;
  moveWin: (id: number, x: number, y: number) => void;
  resizeWin: (id: number, w: number, h: number) => void;
  snapWindow: (id: number, x: number, y: number, w: number, h: number) => { x: number; y: number; w: number; h: number };
  switcherApps: AppId[];
  notify: (app: string, title: string, body: string, icon?: string) => void;
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
  appearance: Appearance; setAppearance: (a: Appearance) => void;
  contrast: boolean; setContrast: (v: boolean) => void;
  reduceMotion: boolean; setReduceMotion: (v: boolean) => void;
  wall: number; setWall: (n: number) => void;
  dock: DockCfg; setDock: (d: Partial<DockCfg>) => void;
  finderLoc: string; setFinderLoc: (l: string) => void;
  finderView: FinderView; setFinderView: (v: FinderView) => void;
  soundVol: number; setSoundVol: (v: number) => void;
  brightness: number; setBrightness: (v: number) => void;
  kbBrightness: number; setKbBrightness: (v: number) => void;
  hud: HudState | null; pulseHud: (kind: HudKind, value: number, label?: string) => void;
  powerState: PowerState; setPowerState: (s: PowerState) => void;
  isLoggedIn: boolean; setIsLoggedIn: (v: boolean) => void;
  locked: boolean; setLocked: (v: boolean) => void;
  bouncingApp: AppId | null; triggerAppBounce: (a: AppId) => void;
  switcherOpen: boolean; switcherIdx: number;
  setSwitcherOpen: (v: boolean) => void; setSwitcherIdx: (n: number) => void;
  devOverlay: boolean; setDevOverlay: (v: boolean) => void;
  fs: Record<string, FSItem[]>;
  emptyTrash: () => void;
  moveToTrash: (item: FSItem, from?: string) => void;
  addFsItem: (loc: string, item: FSItem) => void;
  renameFsItem: (loc: string, from: string, to: string) => void;
}

const Ctx = createContext<OSCtx>(null as unknown as OSCtx);
export const useOS = () => useContext(Ctx);

const now = () => new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });

const HUD_LABEL: Record<HudKind, string> = {
  volume: "Volume",
  brightness: "Brightness",
  keyboard: "Keyboard Brightness",
  media: "Media",
};

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
  const [appearance, setAppearance] = useState<Appearance>("light");
  const [contrast, setContrast] = useState(false);
  const [reduceMotion, setReduceMotion] = useState(false);
  const [wall, setWall] = useState(0);
  const [dock, setDockCfg] = useState<DockCfg>({ size: 52, mag: true, magScale: 0.62, autohide: false, pos: "bottom" });
  const [finderLoc, setFinderLoc] = useState("Recents");
  const [finderView, setFinderView] = useState<FinderView>("icon");
  const [soundVol, setSoundVolState] = useState(68);
  const [brightness, setBrightnessState] = useState(82);
  const [kbBrightness, setKbBrightnessState] = useState(40);
  const [hud, setHud] = useState<HudState | null>(null);
  const [powerState, setPowerState] = useState<PowerState>("running");
  const [isLoggedIn, setIsLoggedIn] = useState(true);
  const [locked, setLocked] = useState(false);
  const [bouncingApp, setBouncingApp] = useState<AppId | null>(null);
  const [switcherOpen, setSwitcherOpen] = useState(false);
  const [switcherIdx, setSwitcherIdx] = useState(0);
  const [devOverlay, setDevOverlay] = useState(false);
  const [fs, setFs] = useState<Record<string, FSItem[]>>(INITIAL_FS);

  const zTop = useRef(10);
  const idSeq = useRef(1);
  const noteSeq = useRef(0);
  const cascade = useRef(0);
  const closeTimers = useRef<Map<number, number>>(new Map());
  const hudTimer = useRef(0);

  const setVisualMode = useCallback((m: VisualMode) => {
    setVisualModeState(m);
    setPerfMode(m === "performance");
  }, []);

  const pulseHud = useCallback((kind: HudKind, value: number, label?: string) => {
    setHud({ kind, value: Math.max(0, Math.min(100, value)), label: label ?? HUD_LABEL[kind] });
    window.clearTimeout(hudTimer.current);
    hudTimer.current = window.setTimeout(() => setHud(null), 1100);
  }, []);

  const setSoundVol = useCallback((v: number) => {
    setSoundVolState(v);
    pulseHud("volume", v);
  }, [pulseHud]);
  const setBrightness = useCallback((v: number) => {
    setBrightnessState(v);
    pulseHud("brightness", v);
  }, [pulseHud]);
  const setKbBrightness = useCallback((v: number) => {
    setKbBrightnessState(v);
    pulseHud("keyboard", v);
  }, [pulseHud]);

  const triggerAppBounce = useCallback((app: AppId) => {
    if (reduceMotion) return;
    setBouncingApp(app);
    window.setTimeout(() => setBouncingApp((cur) => (cur === app ? null : cur)), 700);
  }, [reduceMotion]);

  const cancelClose = useCallback((id: number) => {
    const t = closeTimers.current.get(id);
    if (t) window.clearTimeout(t);
    closeTimers.current.delete(id);
  }, []);

  const focusWin = useCallback((id: number) => {
    zTop.current += 1;
    setWins((w) => w.map((x) => (x.id === id ? { ...x, z: zTop.current, min: false } : x)));
  }, []);

  const settleAnim = useCallback((id: number) => {
    setWins((w) => w.map((x) => (x.id === id && x.animState !== "closing" && x.animState !== "minimizing" ? { ...x, animState: "active" } : x)));
  }, []);

  const finishMin = useCallback((id: number) => {
    setWins((w) => w.map((x) => (x.id === id ? { ...x, min: true, animState: "active" } : x)));
  }, []);

  const openApp = useCallback((app: AppId, payload?: string, title?: string) => {
    if (app === "launchpad") { setLaunchpad((v) => !v); return; }
    if (app === "finder" && payload) setFinderLoc(payload);
    triggerAppBounce(app);
    setWins((w) => {
      const existing = (payload == null || app === "finder") ? w.find((x) => x.app === app) : undefined;
      if (existing && payload == null) {
        cancelClose(existing.id);
        zTop.current += 1;
        const restoring = existing.min || existing.animState === "minimizing" || existing.animState === "closing";
        return w.map((x) => (x.id === existing.id
          ? { ...x, z: zTop.current, min: false, animState: restoring ? "restoring" : "active" }
          : x));
      }
      zTop.current += 1;
      const def = APPS[app] || APPS.finder;
      const off = (cascade.current++ % 5) * 26;
      const [cw, ch] = def.size;
      const ww = Math.min(cw, stageW - 24);
      const wh = Math.min(ch, stageH - 86);
      const win: Win = {
        id: idSeq.current++,
        app,
        title: title ?? def.name,
        x: Math.max(12, (stageW - ww) / 2 + off - 40),
        y: Math.max(8, (stageH - wh) / 2.6 + off),
        w: ww, h: wh,
        z: zTop.current,
        min: false, full: false,
        payload, ws,
        animState: "opening",
      };
      return [...w, win];
    });
  }, [stageW, stageH, ws, triggerAppBounce, cancelClose]);

  const closeWin = useCallback((id: number) => {
    cancelClose(id);
    setWins((w) => w.map((x) => (x.id === id ? { ...x, animState: "closing" } : x)));
    const ms = motionProfile(visualMode, reduceMotion).closeMs + 40;
    const t = window.setTimeout(() => {
      setWins((w) => w.filter((x) => x.id !== id));
      closeTimers.current.delete(id);
    }, ms);
    closeTimers.current.set(id, t);
  }, [cancelClose, visualMode, reduceMotion]);

  const setMin = useCallback((id: number, v: boolean) => {
    setWins((w) => w.map((x) => {
      if (x.id !== id) return x;
      if (!v) {
        zTop.current += 1;
        return { ...x, min: false, z: zTop.current, animState: "restoring" as const };
      }
      if (x.animState === "minimizing") return x;
      return { ...x, animState: "minimizing" as const };
    }));
  }, []);

  const toggleFull = useCallback((id: number) => {
    setWins((w) => w.map((x) => {
      if (x.id !== id) return x;
      if (!x.full) {
        zTop.current += 1;
        return {
          ...x, full: true, animState: "active" as const,
          saved: { x: x.x, y: x.y, w: x.w, h: x.h },
          x: 0, y: 26, w: stageW, h: stageH - 26, z: zTop.current,
        };
      }
      const s = x.saved ?? { x: 80, y: 48, w: 720, h: 480 };
      return { ...x, full: false, animState: "active" as const, x: s.x, y: s.y, w: s.w, h: s.h };
    }));
  }, [stageW, stageH]);

  const moveWin = useCallback((id: number, x: number, y: number) =>
    setWins((w) => w.map((t) => (t.id === id ? { ...t, x, y, animState: t.animState === "opening" || t.animState === "restoring" ? "active" : t.animState } : t))), []);
  const resizeWin = useCallback((id: number, w2: number, h2: number) =>
    setWins((w) => w.map((t) => (t.id === id ? { ...t, w: Math.max(280, w2), h: Math.max(180, h2) } : t))), []);

  const notify = useCallback((app: string, title: string, body: string, icon?: string) => {
    noteSeq.current += 1;
    setNotes((n) => [...n.slice(-5), { id: noteSeq.current, app, title, body, time: now(), icon }]);
  }, []);
  const dismissNote = useCallback((id: number) => setNotes((n) => n.filter((x) => x.id !== id)), []);
  const clearNotes = useCallback(() => setNotes([]), []);

  const setAccent = useCallback((a: string) => {
    setAccentState(a);
    document.documentElement.style.setProperty("--acc", a);
  }, []);
  const setDock = useCallback((d: Partial<DockCfg>) => setDockCfg((c) => ({ ...c, ...d })), []);

  const emptyTrash = useCallback(() => {
    setFs((prev) => ({ ...prev, Trash: [] }));
    notify("Finder", "Trash Emptied", "All items have been permanently deleted.", "trash");
  }, [notify]);

  const moveToTrash = useCallback((item: FSItem, from?: string) => {
    setFs((prev) => {
      const next: Record<string, FSItem[]> = {};
      for (const [k, list] of Object.entries(prev)) {
        if (k === "Trash") continue;
        next[k] = (from && k !== from) ? list : list.filter((f) => f.name !== item.name);
      }
      next.Trash = [...(prev.Trash || []), { ...item, born: false }];
      return next;
    });
    notify("Finder", "Moved to Trash", `“${item.name}” was moved to the Trash.`, "trash");
  }, [notify]);

  const addFsItem = useCallback((loc: string, item: FSItem) => {
    setFs((prev) => ({ ...prev, [loc]: [...(prev[loc] || []), { ...item, born: true }] }));
  }, []);

  const renameFsItem = useCallback((loc: string, from: string, to: string) => {
    const name = to.trim();
    if (!name || name === from) return;
    setFs((prev) => ({
      ...prev,
      [loc]: (prev[loc] || []).map((f) => (f.name === from ? { ...f, name } : f)),
    }));
  }, []);

  const activeApp: AppId = useMemo(() => {
    const visible = wins.filter((w) => !w.min && w.ws === ws && w.animState !== "closing");
    if (!visible.length) return "finder";
    return visible.reduce((a, b) => (a.z > b.z ? a : b)).app;
  }, [wins, ws]);

  /** macOS-style window snap: drag to edges = half/full tile. */
  const snapWindow = useCallback((id: number, x: number, y: number, w: number, h: number) => {
    const EDGE = 4;
    let newX = x, newY = y, newW = w, newH = h, makeFull = false;
    // Top edge = maximize
    if (y <= EDGE) { newX = 0; newY = 26; newW = stageW; newH = stageH - 26; makeFull = true; }
    // Left half
    else if (x <= EDGE) { newX = 0; newY = 26; newW = Math.floor(stageW / 2); newH = stageH - 26; }
    // Right half
    else if (x + w >= stageW - EDGE) { newX = Math.ceil(stageW / 2); newY = 26; newW = Math.floor(stageW / 2); newH = stageH - 26; }
    setWins((prev) => prev.map((p) => {
      if (p.id !== id) return p;
      if (makeFull && !p.full) {
        return {
          ...p, full: true,
          saved: { x: p.x, y: p.y, w: p.w, h: p.h },
          x: newX, y: newY, w: newW, h: newH,
        };
      }
      return { ...p, full: false, x: newX, y: newY, w: newW, h: newH };
    }));
    return { x: newX, y: newY, w: newW, h: newH };
  }, [stageW, stageH]);

  // App switcher: list of running apps in z-order, most-recent first, always with Finder.
  const switcherApps = useMemo(() => {
    const seen = new Set<AppId>();
    const ordered = [...wins]
      .filter((w) => w.ws === ws && w.animState !== "closing")
      .sort((a, b) => b.z - a.z)
      .map((w) => w.app)
      .filter((a) => (seen.has(a) ? false : (seen.add(a), true)));
    if (!ordered.includes("finder")) ordered.push("finder");
    return ordered;
  }, [wins, ws]);

  useEffect(() => () => {
    closeTimers.current.forEach((t) => window.clearTimeout(t));
    window.clearTimeout(hudTimer.current);
  }, []);

  const val: OSCtx = {
    wins, notes, openApp, closeWin, focusWin, setMin, finishMin, settleAnim, toggleFull, moveWin, resizeWin, snapWindow,
    notify, dismissNote, clearNotes, activeApp, switcherApps,
    menuOpen, setMenuOpen, spotlight, setSpotlight, launchpad, setLaunchpad,
    ws, setWs, accent, setAccent, perfMode, setPerfMode, visualMode, setVisualMode,
    appearance, setAppearance, contrast, setContrast,
    reduceMotion, setReduceMotion, wall, setWall,
    dock, setDock, finderLoc, setFinderLoc, finderView, setFinderView,
    soundVol, setSoundVol, brightness, setBrightness, kbBrightness, setKbBrightness,
    hud, pulseHud,
    powerState, setPowerState, isLoggedIn, setIsLoggedIn, locked, setLocked,
    bouncingApp, triggerAppBounce,
    switcherOpen, switcherIdx, setSwitcherOpen, setSwitcherIdx,
    devOverlay, setDevOverlay,
    fs, emptyTrash, moveToTrash, addFsItem, renameFsItem,
  };

  return <Ctx.Provider value={val}>{children}</Ctx.Provider>;
}
