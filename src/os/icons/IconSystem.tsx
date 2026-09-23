/**
 * G1OS icon resolver.
 *
 * Every system icon — Dock, Finder, Settings, menu bar, dialogs — comes
 * through here so the family stays one style. Pictorial icons are original
 * vector artwork. Chrome glyphs are the template symbol set. Nothing here
 * is a third-party pack, an emoji, or an Apple asset.
 */


import { Symbol, GLYPHS } from "./symbols";
import {
  FinderIcon,
  SafariIcon,
  SettingsIcon,
  TerminalIcon,
  MusicIcon,
  PhotosIcon,
  PlayerIcon,
  LaunchpadIcon,
  CalculatorIcon,
  TextEditIcon,
  NotesIcon,
  CalendarIcon,
  ClockIcon,
  InstallerIcon,
  ComputerIcon,
  SysInfoIcon,
  TrashIcon,
  FolderIcon,
  DriveIcon,
  DocIcon,
  Frame,
} from "./artwork";

export type G1IconName = string;

export interface G1IconProps {
  name: G1IconName;
  size?: number;
  className?: string;
  count?: number;
  title?: string;
  /** Motion participation. The wrapper scales; the artwork does not re-layout. */
  state?: "idle" | "hover" | "press" | "selected";
  /** Transient status drawn with the same motion tokens, only while active. */
  status?: "loading" | "syncing" | "downloading" | "installing" | "error" | "success";
}

const FOLDER_VARIANTS = new Set([
  "plain", "open", "documents", "downloads", "desktop", "pictures", "music",
  "movies", "apps", "shared", "network", "cloud", "home",
]);

function folderVariant(key: string): string | null {
  if (key === "folder") return "plain";
  if (key === "folder-open") return "open";
  if (key.startsWith("folder-")) {
    const v = key.slice(7);
    if (v === "movies" || v === "videos") return "movies";
    if (FOLDER_VARIANTS.has(v)) return v;
    return "plain";
  }
  if (key === "documents") return "documents";
  if (key === "downloads") return "downloads";
  if (key === "desktop") return "desktop";
  if (key === "pictures") return "pictures";
  if (key === "applications" || key === "apps") return "apps";
  if (key === "home") return "home";
  if (key === "shared") return "shared";
  return null;
}

function docKind(key: string): string | null {
  const map: Record<string, string> = {
    "file-pdf": "pdf", pdf: "pdf",
    "file-text": "text", text: "text",
    "file-image": "image", image: "image",
    "file-video": "video", video: "video",
    "file-audio": "audio", audio: "audio",
    "file-archive": "archive", archive: "archive",
    "file-sheet": "sheet", sheet: "sheet",
    "file-presentation": "presentation", keynote: "keynote",
    "file-code": "code", code: "code",
    "file-disk-image": "disk-image",
    "file-executable": "executable",
    "file-config": "config",
    "file-generic": "generic", file: "generic",
  };
  return map[key] ?? null;
}

function Pictorial({ name, size, count }: { name: string; size: number; count: number }) {
  switch (name) {
    case "finder":
      return <FinderIcon size={size} />;
    case "browser":
    case "safari":
      return <SafariIcon size={size} />;
    case "settings":
      return <SettingsIcon size={size} />;
    case "terminal":
      return <TerminalIcon size={size} />;
    case "music":
      return <MusicIcon size={size} />;
    case "photos":
      return <PhotosIcon size={size} />;
    case "player":
    case "quickplayer":
      return <PlayerIcon size={size} />;
    case "launchpad":
      return <LaunchpadIcon size={size} />;
    case "calc":
    case "calculator":
      return <CalculatorIcon size={size} />;
    case "textedit":
      return <TextEditIcon size={size} />;
    case "notes":
      return <NotesIcon size={size} />;
    case "calendar":
      return <CalendarIcon size={size} />;
    case "clock":
    case "date-time":
      return size >= 28 ? <ClockIcon size={size} /> : <Symbol name="clock" size={size} />;
    case "installer":
      return <InstallerIcon size={size} />;
    case "sysinfo":
    case "about":
      return <SysInfoIcon size={size} />;
    case "computer":
      return <ComputerIcon size={size} />;
    case "trash":
    case "delete":
      return size >= 28 ? <TrashIcon size={size} count={count} /> : <Symbol name="trash" size={size} />;
    case "macintosh-hd":
    case "hd":
    case "internal-drive":
    case "system-drive":
    case "internal-disk":
      return <DriveIcon size={size} kind="internal" />;
    case "external-drive":
    case "external-disk":
    case "network-drive":
      return <DriveIcon size={size} kind="external" />;
    case "usb":
    case "usb-drive":
      return <DriveIcon size={size} kind="usb" />;
    case "installer-disk":
      return <InstallerIcon size={size} />;
    default: {
      const fv = folderVariant(name);
      if (fv) return <FolderIcon size={size} variant={fv} />;
      const dk = docKind(name);
      if (dk) return <DocIcon size={size} kind={dk} />;
      return null;
    }
  }
}

const STATUS_RING: Record<string, string> = {
  loading: "g1-spin",
  syncing: "g1-spin",
  downloading: "",
  installing: "g1-spin",
  error: "",
  success: "",
};

export function G1Icon({
  name,
  size = 24,
  className = "",
  count = 0,
  title,
  state = "idle",
  status,
}: G1IconProps) {
  const key = name.replace(/^g1os-icon:\/\//, "").toLowerCase();
  const pictorial = Pictorial({ name: key, size, count });
  const symbolName = GLYPHS[key] ? key : aliasSymbol(key);
  const motion = state === "press" ? "is-press" : state === "hover" ? "is-hover" : state === "selected" ? "is-selected" : "";

  const body = pictorial ?? (
    symbolName ? (
      <Symbol name={symbolName} size={size} className={className} />
    ) : (
      <Symbol name="info" size={size} className={className} />
    )
  );

  return (
    <span
      className={`g1-icon ${motion} ${className}`}
      style={{ width: size, height: size }}
      title={title}
      data-icon={key}
    >
      {body}
      {status && (
        <span className={`g1-icon-status ${STATUS_RING[status] ?? ""}`} aria-hidden>
          {status === "error" && <i className="g1-dot g1-dot-err" />}
          {status === "success" && <i className="g1-dot g1-dot-ok" />}
          {(status === "loading" || status === "syncing" || status === "installing") && (
            <Symbol name="refresh" size={Math.max(10, Math.round(size * 0.38))} />
          )}
          {status === "downloading" && <Symbol name="download" size={Math.max(10, Math.round(size * 0.38))} />}
        </span>
      )}
    </span>
  );
}

function aliasSymbol(key: string): string | null {
  const map: Record<string, string> = {
    spotlight: "search",
    notifications: "bell",
    privacy: "key",
    security: "shield",
    accessibility: "eye",
    access: "eye",
    updates: "download",
    "software-update": "download",
    appearance: "sparkles",
    dock: "grid",
    perf: "gauge",
    power: "battery",
    sound: "volume",
    wifi: "wifi",
    logout: "logout",
    "new-folder": "folder-plus",
    "new-file": "new-file",
  };
  if (map[key]) return map[key];
  if (GLYPHS[key]) return key;
  return null;
}

export function FinderSquircle({ size = 50 }: { size?: number }) {
  return <FinderIcon size={size} />;
}

const BADGE_SYMBOL: Record<string, { from: string; to: string; symbol: string }> = {
  perf: { from: "#5aa7ff", to: "#2450e0", symbol: "gauge" },
  appearance: { from: "#e15cff", to: "#ff8a3d", symbol: "sparkles" },
  dock: { from: "#5ad0ff", to: "#1d6fe0", symbol: "grid" },
  sound: { from: "#ff5d7a", to: "#e2184a", symbol: "volume" },
  wifi: { from: "#4da3ff", to: "#0a66e0", symbol: "wifi" },
  network: { from: "#49c4ff", to: "#1d6fe0", symbol: "globe" },
  storage: { from: "#c5c8d0", to: "#6e7480", symbol: "hard-drive" },
  keyboard: { from: "#8e93a0", to: "#3e4450", symbol: "keyboard" },
  access: { from: "#4d7dff", to: "#2a2fbf", symbol: "eye" },
  power: { from: "#5ee0a0", to: "#149a72", symbol: "battery" },
  updates: { from: "#ffd15a", to: "#f07a00", symbol: "download" },
  users: { from: "#6aa7ff", to: "#3a46d6", symbol: "user" },
  security: { from: "#4ade80", to: "#15803d", symbol: "shield" },
  about: { from: "#7ecbff", to: "#1d6fe0", symbol: "info" },
  display: { from: "#7aa2ff", to: "#3b4fd0", symbol: "display" },
  bluetooth: { from: "#6aa7ff", to: "#2450e0", symbol: "bluetooth" },
  privacy: { from: "#5ee0c0", to: "#0f8f78", symbol: "key" },
  notifications: { from: "#ff6a6a", to: "#e11d48", symbol: "bell" },
  language: { from: "#5ad0ff", to: "#1d6fe0", symbol: "globe" },
  "date-time": { from: "#2a2c31", to: "#0c0d10", symbol: "clock" },
};

export function G1SettingsBadge({ id, size = 22 }: { id: string; size?: number }) {
  const item = BADGE_SYMBOL[id] ?? { from: "#a1a1aa", to: "#52525b", symbol: "sliders" };
  return (
    <span className="g1-badge" style={{ width: size, height: size, color: "#fff" }}>
      <Frame size={size} from={item.from} to={item.to} />
      <span className="g1-badge-glyph">
        <Symbol name={item.symbol} size={Math.max(11, Math.round(size * 0.56))} strokeWidth={2.15} />
      </span>
    </span>
  );
}

export { Symbol as G1Symbol, GLYPHS };
