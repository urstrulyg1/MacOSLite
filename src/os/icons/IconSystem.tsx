import React from "react";
import {
  FileText,
  Clock,
  Sparkles,
  Info,
  Power,
  RotateCcw,
  LogOut,
  Trash2,
  Lock,
  Unlock,
  ChevronRight,
  ChevronLeft,
  Search,
  Check,
  X,
  Minus,
  Plus,
  Wifi,
  WifiOff,
  Bluetooth,
  Volume2,
  VolumeX,
  Sun,
  Moon,
  Battery,
  BatteryCharging,
  Sliders,
  Bell,
  Shield,
  ShieldCheck,
  User,
  Users,
  Key,
  Eye,
  Calendar,
  Layers,
  HelpCircle,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  RefreshCw,
  Maximize2,
  Minimize2,
  Share2,
  Upload,
  Download,
  ArrowUpDown,
  List,
  LayoutGrid,
  Columns,
  Tag,
  Star,
  FolderPlus,
  FilePlus,
  Copy,
  Scissors,
  Clipboard,
  Edit3,
  Undo2,
  Redo2,
  Airplay,
  Cast,
  Globe,
  HardDrive,
  Cpu,
  Keyboard,
  Mouse,
  Tv,
} from "lucide-react";

/* ------------------------------------------------------------------ */
/* G1OS macOS-Caliber Vector Design System Tokens & Types              */
/* ------------------------------------------------------------------ */

export type G1IconName =
  // Applications
  | "finder"
  | "installer"
  | "browser"
  | "safari"
  | "launchpad"
  | "settings"
  | "terminal"
  | "music"
  | "photos"
  | "player"
  | "quickplayer"
  | "calc"
  | "calculator"
  | "textedit"
  | "trash"
  | "sysinfo"
  | "calendar"
  | "clock"
  | "notes"

  // Folders
  | "folder"
  | "folder-open"
  | "folder-documents"
  | "folder-downloads"
  | "folder-desktop"
  | "folder-pictures"
  | "folder-music"
  | "folder-movies"
  | "folder-apps"
  | "folder-shared"
  | "folder-network"
  | "folder-cloud"

  // Drives & Storage
  | "macintosh-hd"
  | "hd"
  | "usb-drive"
  | "usb"
  | "installer-disk"
  | "disk-partition"
  | "external-drive"
  | "system-drive"
  | "network-drive"

  // Document & File Types
  | "file-generic"
  | "file-text"
  | "file-pdf"
  | "file-image"
  | "file-video"
  | "file-audio"
  | "file-archive"
  | "file-sheet"
  | "file-presentation"
  | "file-code"
  | "file-disk-image"
  | "file-executable"
  | "file-config"

  // System Controls & Settings Indicators
  | "wifi"
  | "wifi-off"
  | "bluetooth"
  | "display"
  | "sound"
  | "volume"
  | "volume-mute"
  | "brightness"
  | "battery"
  | "power"
  | "storage"
  | "security"
  | "privacy"
  | "users"
  | "notifications"
  | "appearance"
  | "accessibility"
  | "date-time"
  | "language"
  | "updates"
  | "keyboard"
  | "mouse"
  | "trackpad"
  | "control-center"
  | "spotlight"
  | "airplay"
  | "screen-mirror"

  // Actions & Navigation
  | "lock"
  | "unlock"
  | "shutdown"
  | "restart"
  | "sleep"
  | "help"
  | "info"
  | "warning"
  | "error"
  | "success"
  | "back"
  | "forward"
  | "refresh"
  | "close"
  | "minimize"
  | "maximize"
  | "fullscreen"
  | "share"
  | "upload"
  | "download"
  | "search"
  | "sort"
  | "list-view"
  | "grid-view"
  | "column-view"
  | "tags"
  | "favorites"
  | "new-folder"
  | "new-file"
  | "copy"
  | "cut"
  | "paste"
  | "rename"
  | "delete"
  | "undo"
  | "redo";

export interface G1IconProps {
  name: G1IconName | string;
  size?: number;
  className?: string;
  count?: number;
  variant?: "standard" | "flat" | "elevated";
  title?: string;
}

/* ------------------------------------------------------------------ */
/* 1. App Squircles (Authentic macOS squircle curve & 12 o'clock light) */
/* ------------------------------------------------------------------ */

export function FinderSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="finder-bg-right" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#80ccff" />
          <stop offset="50%" stopColor="#54b8ff" />
          <stop offset="100%" stopColor="#289bf5" />
        </linearGradient>
        <linearGradient id="finder-bg-left" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#1a8cff" />
          <stop offset="50%" stopColor="#0071eb" />
          <stop offset="100%" stopColor="#0057c7" />
        </linearGradient>
        <clipPath id="finder-squircle-clip">
          <rect x="3" y="3" width="58" height="58" rx="14" />
        </clipPath>
      </defs>
      {/* Base right side */}
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#finder-bg-right)" />
      
      {/* Left side face with organic nose curve */}
      <g clipPath="url(#finder-squircle-clip)">
        <path
          d="M3 3 H32 C32 17 31 23 30 26 C29 29.5 35 32 35 34.5 C35 37 32 39 32 61 H3 Z"
          fill="url(#finder-bg-left)"
        />
        {/* Specular top rim shine */}
        <rect x="3" y="3" width="58" height="28" rx="14" fill="white" opacity="0.18" />
      </g>
      
      {/* Squircle border highlight */}
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.4)" strokeWidth="1" />

      {/* Nose division line */}
      <path
        d="M32 3 C32 17 31 23 30 26 C29 29.5 35 32 35 34.5 C35 37 32 39 32 61"
        stroke="#0b1e36"
        strokeWidth="2.2"
        strokeLinecap="round"
        strokeLinejoin="round"
        fill="none"
      />

      {/* Eyebrows */}
      <path
        d="M16 20 C18 18 23 18 25.5 20.5"
        stroke="#0b1e36"
        strokeWidth="2.2"
        strokeLinecap="round"
        fill="none"
      />
      <path
        d="M38.5 20.5 C41 18 46 18 48 20"
        stroke="#0b1e36"
        strokeWidth="2.2"
        strokeLinecap="round"
        fill="none"
      />

      {/* Eyes (capsule/ovals) */}
      <ellipse cx="20.5" cy="27" rx="2.2" ry="3.6" fill="#0b1e36" />
      <ellipse cx="43.5" cy="27" rx="2.2" ry="3.6" fill="#0b1e36" />

      {/* Wide friendly smiling mouth */}
      <path
        d="M18 40.5 C23 47 41 47 46 40.5"
        stroke="#0b1e36"
        strokeWidth="2.8"
        strokeLinecap="round"
        fill="none"
      />
    </svg>
  );
}

export function SafariSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <radialGradient id="safari-dial" cx="50%" cy="40%" r="60%">
          <stop offset="0%" stopColor="#28b0ff" />
          <stop offset="45%" stopColor="#007dfc" />
          <stop offset="100%" stopColor="#0057d8" />
        </radialGradient>
      </defs>
      {/* White squircle */}
      <rect x="3" y="3" width="58" height="58" rx="14" fill="#ffffff" stroke="#e0e4ec" strokeWidth="0.8" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.4" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.8)" strokeWidth="1" />

      {/* Blue compass dial */}
      <circle cx="32" cy="32" r="22.5" fill="url(#safari-dial)" stroke="#004bc2" strokeWidth="0.8" />

      {/* Radial compass tick marks */}
      <g stroke="rgba(255,255,255,0.75)" strokeWidth="1" strokeLinecap="round">
        {[...Array(24)].map((_, i) => {
          const angle = (i * 15 * Math.PI) / 180;
          const isMajor = i % 2 === 0;
          const r1 = isMajor ? 18.5 : 19.5;
          const r2 = 21;
          const x1 = 32 + r1 * Math.sin(angle);
          const y1 = 32 - r1 * Math.cos(angle);
          const x2 = 32 + r2 * Math.sin(angle);
          const y2 = 32 - r2 * Math.cos(angle);
          return (
            <line
              key={i}
              x1={x1}
              y1={y1}
              x2={x2}
              y2={y2}
              strokeWidth={isMajor ? 1.4 : 0.8}
              stroke={isMajor ? "rgba(255,255,255,0.95)" : "rgba(255,255,255,0.6)"}
            />
          );
        })}
      </g>

      {/* Faceted compass needle tilted at 45° */}
      <g transform="rotate(45 32 32)" filter="drop-shadow(0 2px 3.5px rgba(0,0,0,0.32))">
        {/* Red top point */}
        <polygon points="32,10 27.5,32 32,30" fill="#ff453a" />
        <polygon points="32,10 36.5,32 32,30" fill="#d70015" />
        {/* White bottom point */}
        <polygon points="32,54 27.5,32 32,34" fill="#ffffff" />
        <polygon points="32,54 36.5,32 32,34" fill="#cbd5e1" />
        {/* Center pivot pin */}
        <circle cx="32" cy="32" r="3.2" fill="#ffffff" />
        <circle cx="32" cy="32" r="1.6" fill="#334155" />
      </g>
    </svg>
  );
}

export function SettingsSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="settings-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#787980" />
          <stop offset="50%" stopColor="#585960" />
          <stop offset="100%" stopColor="#404147" />
        </linearGradient>
        <radialGradient id="gear-metal" cx="50%" cy="35%" r="65%">
          <stop offset="0%" stopColor="#ffffff" />
          <stop offset="45%" stopColor="#e4e4e7" />
          <stop offset="85%" stopColor="#a1a1aa" />
          <stop offset="100%" stopColor="#71717a" />
        </radialGradient>
        <radialGradient id="gear-well" cx="50%" cy="40%" r="60%">
          <stop offset="0%" stopColor="#2c2d33" />
          <stop offset="100%" stopColor="#18181c" />
        </radialGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#settings-bg)" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.16" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.3)" strokeWidth="1" />

      {/* Recessed dark well */}
      <circle cx="32" cy="32" r="18" fill="url(#gear-well)" stroke="#121316" strokeWidth="0.8" />
      <circle cx="32" cy="32" r="18" fill="none" stroke="rgba(0,0,0,0.5)" strokeWidth="1.2" />

      {/* Multi-toothed metallic gear */}
      <g transform="translate(32,32)" filter="drop-shadow(0 2px 3px rgba(0,0,0,0.45))">
        {[0, 36, 72, 108, 144, 180, 216, 252, 288, 324].map((angle) => (
          <rect
            key={angle}
            x="-3"
            y="-16.5"
            width="6"
            height="5.5"
            rx="1.2"
            fill="url(#gear-metal)"
            stroke="#a1a1aa"
            strokeWidth="0.5"
            transform={`rotate(${angle})`}
          />
        ))}
        {/* Main gear wheel body */}
        <circle cx="0" cy="0" r="13" fill="url(#gear-metal)" stroke="#a1a1aa" strokeWidth="0.8" />
        {/* Inner concentric bevel */}
        <circle cx="0" cy="0" r="8" fill="none" stroke="#71717a" strokeWidth="0.8" />
        {/* Axle center hole */}
        <circle cx="0" cy="0" r="4.8" fill="#1e1f24" stroke="#121316" strokeWidth="1" />
      </g>
    </svg>
  );
}

export function TerminalSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="term-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#22242a" />
          <stop offset="100%" stopColor="#0c0d10" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#term-bg)" stroke="#383a42" strokeWidth="0.8" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.06" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.15)" strokeWidth="1" />

      {/* Crisp pure white prompt '> _' */}
      <g filter="drop-shadow(0 1px 2px rgba(0,0,0,0.5))">
        {/* '>' Angle bracket */}
        <path
          d="M17 25 L26 32 L17 39"
          stroke="#ffffff"
          strokeWidth="3.2"
          strokeLinecap="round"
          strokeLinejoin="round"
          fill="none"
        />
        {/* '_' Underscore cursor */}
        <rect x="30" y="36.5" width="13.5" height="3.2" rx="1" fill="#ffffff" />
      </g>
    </svg>
  );
}

export function MusicSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="music-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#ff3b5c" />
          <stop offset="50%" stopColor="#f51e47" />
          <stop offset="100%" stopColor="#d60029" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#music-bg)" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.22" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.4)" strokeWidth="1" />

      {/* Double eighth notes with beam */}
      <g fill="#ffffff" filter="drop-shadow(0 2.5px 3.5px rgba(0,0,0,0.28))">
        <ellipse cx="23" cy="41" rx="5" ry="4" transform="rotate(-18 23 41)" />
        <ellipse cx="40" cy="36" rx="5" ry="4" transform="rotate(-18 40 36)" />
        <rect x="25.5" y="20" width="3.4" height="21" rx="1.2" />
        <rect x="42.5" y="15" width="3.4" height="21" rx="1.2" />
        <polygon points="25.5,20 45.9,15 45.9,20.5 25.5,25.5" />
      </g>
    </svg>
  );
}

export function PhotosSquircle({ size = 50 }: { size?: number }) {
  const petals = [
    { color: "#ffd600", rot: 0 },
    { color: "#ff9500", rot: 45 },
    { color: "#ff3b30", rot: 90 },
    { color: "#ff2d55", rot: 135 },
    { color: "#af52de", rot: 180 },
    { color: "#007aff", rot: 225 },
    { color: "#5ac8fa", rot: 270 },
    { color: "#34c759", rot: 315 },
  ];

  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <rect x="3" y="3" width="58" height="58" rx="14" fill="#ffffff" stroke="#e0e4ec" strokeWidth="0.8" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.4" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.9)" strokeWidth="1" />

      {/* 8-petal rainbow flower */}
      <g transform="translate(32,32)">
        {petals.map((p, i) => (
          <ellipse
            key={i}
            cx="0"
            cy="-11"
            rx="4.8"
            ry="9.6"
            fill={p.color}
            opacity="0.88"
            transform={`rotate(${p.rot})`}
            style={{ mixBlendMode: "multiply" }}
          />
        ))}
        {/* Center white circle */}
        <circle cx="0" cy="0" r="4.2" fill="#ffffff" />
      </g>
    </svg>
  );
}

export function PlayerSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="player-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#8e54e9" />
          <stop offset="50%" stopColor="#6d39df" />
          <stop offset="100%" stopColor="#4f20c4" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#player-bg)" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.2" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.35)" strokeWidth="1" />

      {/* Rounded white play triangle */}
      <g filter="drop-shadow(0 2px 4px rgba(0,0,0,0.3))">
        <path
          d="M26 21 C26 19.5 27.6 18.5 29 19.3 L43 27.3 C44.3 28.1 44.3 30 43 30.8 L29 38.8 C27.6 39.6 26 38.6 26 37.1 Z"
          fill="#ffffff"
        />
      </g>
    </svg>
  );
}

export function LaunchpadSquircle({ size = 50 }: { size?: number }) {
  const tiles = [
    { x: 15, y: 15, c1: "#ff6b4a" },
    { x: 27.5, y: 15, c1: "#38bdf8" },
    { x: 40, y: 15, c1: "#34d399" },
    { x: 15, y: 27.5, c1: "#f43f5e" },
    { x: 27.5, y: 27.5, c1: "#00d2ff" },
    { x: 40, y: 27.5, c1: "#6366f1" },
    { x: 15, y: 40, c1: "#a855f7" },
    { x: 27.5, y: 40, c1: "#4ade80" },
    { x: 40, y: 40, c1: "#fbbf24" },
  ];

  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="launch-bg-frost" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#f3f4fa" />
          <stop offset="100%" stopColor="#e2e4f2" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#launch-bg-frost)" stroke="#d2d6ea" strokeWidth="0.8" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.4" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.9)" strokeWidth="1" />

      {/* 3x3 colorful app tiles */}
      {tiles.map((t, idx) => (
        <g key={idx} filter="drop-shadow(0 1px 1.5px rgba(0,0,0,0.18))">
          <rect
            x={t.x}
            y={t.y}
            width="9"
            height="9"
            rx="2.8"
            fill={t.c1}
          />
        </g>
      ))}
    </svg>
  );
}

export function CalculatorSquircle({ size = 50 }: { size?: number }) {
  // 3x3 circular keypad buttons: 2 columns silver/gray, 1 column orange
  const buttons = [
    { cx: 21, cy: 21, color: "gray" },
    { cx: 32, cy: 21, color: "gray" },
    { cx: 43, cy: 21, color: "orange" },
    { cx: 21, cy: 32, color: "gray" },
    { cx: 32, cy: 32, color: "gray" },
    { cx: 43, cy: 32, color: "orange" },
    { cx: 21, cy: 43, color: "gray" },
    { cx: 32, cy: 43, color: "gray" },
    { cx: 43, cy: 43, color: "orange" },
  ];

  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="calc-keypad-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#2c2e33" />
          <stop offset="100%" stopColor="#151619" />
        </linearGradient>
        <linearGradient id="calc-btn-gray" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#e4e4e7" />
          <stop offset="100%" stopColor="#a1a1aa" />
        </linearGradient>
        <linearGradient id="calc-btn-orange" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#ffaa2a" />
          <stop offset="100%" stopColor="#f07000" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#calc-keypad-bg)" stroke="#3e4048" strokeWidth="0.8" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.08" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.18)" strokeWidth="1" />

      {/* 3x3 circular buttons */}
      {buttons.map((b, idx) => (
        <circle
          key={idx}
          cx={b.cx}
          cy={b.cy}
          r="4.6"
          fill={b.color === "orange" ? "url(#calc-btn-orange)" : "url(#calc-btn-gray)"}
          stroke={b.color === "orange" ? "#c45000" : "#71717a"}
          strokeWidth="0.5"
          filter="drop-shadow(0 1px 1.5px rgba(0,0,0,0.35))"
        />
      ))}
    </svg>
  );
}

export function TextEditSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="notes-tab-grad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#fbbf24" />
          <stop offset="100%" stopColor="#f59e0b" />
        </linearGradient>
        <linearGradient id="notes-pad-grad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#fffdf0" />
          <stop offset="100%" stopColor="#fdf7d5" />
        </linearGradient>
        <clipPath id="notes-squircle-clip">
          <rect x="3" y="3" width="58" height="58" rx="14" />
        </clipPath>
      </defs>

      {/* Base warm cream pad body */}
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#notes-pad-grad)" stroke="#ecd385" strokeWidth="0.8" />

      <g clipPath="url(#notes-squircle-clip)">
        {/* Yellow top binding tab */}
        <rect x="3" y="3" width="58" height="17" fill="url(#notes-tab-grad)" />
        <line x1="3" y1="20" x2="61" y2="20" stroke="#d97706" strokeWidth="0.8" opacity="0.6" />

        {/* Horizontal ruled lines */}
        <g stroke="#e2c98b" strokeWidth="1.2" opacity="0.8">
          <line x1="12" y1="28" x2="52" y2="28" />
          <line x1="12" y1="36" x2="52" y2="36" />
          <line x1="12" y1="44" x2="52" y2="44" />
          <line x1="12" y1="52" x2="38" y2="52" />
        </g>

        {/* Top shine */}
        <rect x="3" y="3" width="58" height="28" rx="14" fill="white" opacity="0.22" />
      </g>

      {/* Outer border highlight */}
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.7)" strokeWidth="1" />
    </svg>
  );
}

export function TrashGlassBasket({ size = 50, count = 0 }: { size?: number; count?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-lg">
      <defs>
        {/* Translucent frosted glass gradient with soft lilac/cyan sheen */}
        <linearGradient id="trash-glass-wall" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0%" stopColor="rgba(235, 238, 255, 0.65)" />
          <stop offset="25%" stopColor="rgba(215, 225, 255, 0.42)" />
          <stop offset="50%" stopColor="rgba(200, 215, 250, 0.28)" />
          <stop offset="75%" stopColor="rgba(215, 225, 255, 0.42)" />
          <stop offset="100%" stopColor="rgba(235, 238, 255, 0.65)" />
        </linearGradient>

        <linearGradient id="trash-rim-shine" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#ffffff" />
          <stop offset="100%" stopColor="rgba(210, 220, 255, 0.7)" />
        </linearGradient>
      </defs>

      {/* Contact shadow under base */}
      <ellipse cx="32" cy="56" rx="14" ry="3.5" fill="rgba(0, 0, 0, 0.24)" filter="blur(1.5px)" />

      {/* Back rim of glass basket */}
      <ellipse cx="32" cy="20" rx="17" ry="5.5" fill="rgba(190, 205, 245, 0.35)" stroke="rgba(255, 255, 255, 0.6)" strokeWidth="1.2" />

      {/* Crumpled paper meshes visible inside and protruding out top */}
      <g filter="drop-shadow(0 1.5px 2px rgba(0,0,0,0.25))">
        {/* Left paper ball */}
        <polygon points="20,18 24,13 29,15 28,21 21,22" fill="#ffffff" />
        <polygon points="24,13 29,15 28,18 23,17" fill="#e2e8f0" />
        <polygon points="20,18 23,17 21,22" fill="#cbd5e1" />

        {/* Center top paper ball */}
        <polygon points="27,15 33,10 39,13 36,20 29,19" fill="#ffffff" />
        <polygon points="33,10 39,13 36,16 31,14" fill="#cbd5e1" />
        <polygon points="27,15 31,14 36,16 36,20 29,19" fill="#f1f5f9" />

        {/* Right paper ball */}
        <polygon points="35,17 41,12 46,16 43,22 37,20" fill="#ffffff" />
        <polygon points="41,12 46,16 41,19 37,17" fill="#e2e8f0" />
        <polygon points="35,17 37,17 41,19 43,22 37,20" fill="#cbd5e1" />

        {/* Crumpled paper bulk inside the glass */}
        <path
          d="M23 23 L28 28 L35 24 L41 29 L38 36 L26 34 Z"
          fill="rgba(255,255,255,0.7)"
        />
        <path
          d="M26 34 L32 38 L38 36 L30 33 Z"
          fill="rgba(203,213,225,0.55)"
        />
      </g>

      {/* Tapered conical frosted glass body */}
      <path
        d="M15 20 L19.5 52 C19.5 54.5 25 56 32 56 C39 56 44.5 54.5 44.5 52 L49 20 C49 20 44 25.5 32 25.5 C20 25.5 15 20 15 20 Z"
        fill="url(#trash-glass-wall)"
      />

      {/* Front rim highlight of glass basket */}
      <path
        d="M15 20 C15 23 22.5 25.5 32 25.5 C41.5 25.5 49 23 49 20"
        stroke="url(#trash-rim-shine)"
        strokeWidth="2"
        strokeLinecap="round"
        fill="none"
      />

      {/* Specular vertical glass reflections down the sides */}
      <path
        d="M17.5 22 L21.5 50"
        stroke="rgba(255, 255, 255, 0.85)"
        strokeWidth="2.2"
        strokeLinecap="round"
      />
      <path
        d="M46.5 22 L42.5 50"
        stroke="rgba(255, 255, 255, 0.65)"
        strokeWidth="1.6"
        strokeLinecap="round"
      />

      {/* Glass base bottom rim ellipse highlight */}
      <path
        d="M19.5 52 C19.5 54.5 25 56 32 56 C39 56 44.5 54.5 44.5 52"
        stroke="rgba(255, 255, 255, 0.75)"
        strokeWidth="1.2"
        fill="none"
      />
    </svg>
  );
}

export function CalendarSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none">
      <rect x="3" y="3" width="58" height="58" rx="14" fill="#ffffff" stroke="#e2e8f0" strokeWidth="0.8" />
      <rect x="3" y="3" width="58" height="18" rx="14" fill="#ef4444" />
      <rect x="3" y="14" width="58" height="7" fill="#ef4444" />
      <text x="32" y="15" textAnchor="middle" fontFamily="system-ui, sans-serif" fontSize="10" fontWeight="bold" fill="#ffffff">
        OCT
      </text>
      <text x="32" y="47" textAnchor="middle" fontFamily="system-ui, sans-serif" fontSize="24" fontWeight="300" fill="#1e293b">
        24
      </text>
    </svg>
  );
}

export function ClockSquircle({ size = 50 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none">
      <defs>
        <linearGradient id="clock-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#18181b" />
          <stop offset="100%" stopColor="#09090b" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#clock-bg)" stroke="#27272a" strokeWidth="0.8" />
      <circle cx="32" cy="32" r="23" fill="#18181b" stroke="#3f3f46" strokeWidth="1" />
      {/* Hour markers */}
      <line x1="32" y1="12" x2="32" y2="15" stroke="#a1a1aa" strokeWidth="1.5" strokeLinecap="round" />
      <line x1="32" y1="49" x2="32" y2="52" stroke="#a1a1aa" strokeWidth="1.5" strokeLinecap="round" />
      <line x1="12" y1="32" x2="15" y2="32" stroke="#a1a1aa" strokeWidth="1.5" strokeLinecap="round" />
      <line x1="49" y1="32" x2="52" y2="32" stroke="#a1a1aa" strokeWidth="1.5" strokeLinecap="round" />
      {/* Clock hands */}
      <line x1="32" y1="32" x2="32" y2="20" stroke="#ffffff" strokeWidth="2.5" strokeLinecap="round" />
      <line x1="32" y1="32" x2="43" y2="32" stroke="#ffffff" strokeWidth="2" strokeLinecap="round" />
      <line x1="32" y1="36" x2="32" y2="16" stroke="#f59e0b" strokeWidth="1" strokeLinecap="round" />
      <circle cx="32" cy="32" r="2" fill="#f59e0b" />
    </svg>
  );
}

export function NotesSquircle({ size = 50 }: { size?: number }) {
  return <TextEditSquircle size={size} />;
}

/* ------------------------------------------------------------------ */
/* 2. Folders (macOS 3D two-tone sky folders)                          */
/* ------------------------------------------------------------------ */

export function G1FolderIcon({ size = 48, glyph }: { size?: number; glyph?: React.ReactNode }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-sm">
      <defs>
        <linearGradient id="f-back" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#38bdf8" />
          <stop offset="100%" stopColor="#0284c7" />
        </linearGradient>
        <linearGradient id="f-front" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#7dd3fc" />
          <stop offset="35%" stopColor="#38bdf8" />
          <stop offset="100%" stopColor="#0369a1" />
        </linearGradient>
      </defs>
      {/* Back tab */}
      <path
        d="M8 18 C8 15 10 13 13 13 L24 13 C27 13 28 15 30 17 L33 20 L51 20 C54 20 56 22 56 25 L56 46 C56 49 54 51 51 51 L13 51 C10 51 8 49 8 46 Z"
        fill="url(#f-back)"
      />
      {/* Paper insert peak */}
      <rect x="14" y="18" width="36" height="12" rx="2" fill="white" opacity="0.85" />
      {/* Front flap */}
      <path
        d="M8 25 L56 25 C58 25 59 26.5 58.5 28.5 L55 48 C54.5 50.5 52.5 52 50 52 L14 52 C11.5 52 9.5 50.5 9 48 L5.5 28.5 C5 26.5 6 25 8 25 Z"
        fill="url(#f-front)"
      />
      {/* Embossed glyph if provided */}
      {glyph && (
        <foreignObject x="22" y="29" width="20" height="20">
          <div className="flex h-full w-full items-center justify-center text-white/95 drop-shadow-sm">
            {glyph}
          </div>
        </foreignObject>
      )}
    </svg>
  );
}

/* ------------------------------------------------------------------ */
/* 3. Storage & Drives (Internal SSD, Live USB, Partition)             */
/* ------------------------------------------------------------------ */

export function MacintoshHDSquircle({ size = 48 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="mac-hd-body" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#f1f5f9" />
          <stop offset="40%" stopColor="#e2e8f0" />
          <stop offset="100%" stopColor="#94a3b8" />
        </linearGradient>
      </defs>
      <rect x="8" y="10" width="48" height="44" rx="8" fill="url(#mac-hd-body)" stroke="#64748b" strokeWidth="1" />
      <rect x="12" y="14" width="40" height="26" rx="4" fill="#334155" />
      <rect x="18" y="24" width="28" height="3" rx="1.5" fill="#0f172a" />
      <circle cx="18" cy="46" r="2.2" fill="#22c55e" />
      <circle cx="24" cy="46" r="2.2" fill="#3b82f6" />
      <rect x="32" y="44" width="18" height="3.5" rx="1.5" fill="#64748b" />
    </svg>
  );
}

export function LiveUsbSquircle({ size = 48 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="usb-case" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#fbbf24" />
          <stop offset="50%" stopColor="#f59e0b" />
          <stop offset="100%" stopColor="#d97706" />
        </linearGradient>
      </defs>
      <rect x="23" y="8" width="18" height="12" rx="2" fill="#cbd5e1" stroke="#94a3b8" strokeWidth="1" />
      <rect x="27" y="11" width="3" height="4" fill="#64748b" />
      <rect x="34" y="11" width="3" height="4" fill="#64748b" />
      <rect x="17" y="17" width="30" height="38" rx="6" fill="url(#usb-case)" stroke="#b45309" strokeWidth="1" />
      <rect x="20" y="22" width="24" height="24" rx="3" fill="#ffffff" opacity="0.9" />
      <g stroke="#d97706" strokeWidth="2" strokeLinecap="round">
        <line x1="32" y1="28" x2="32" y2="40" />
        <line x1="32" y1="31" x2="27" y2="35" />
        <line x1="32" y1="34" x2="37" y2="37" />
        <circle cx="32" cy="27" r="1.5" fill="#d97706" />
        <rect x="25.5" y="34" width="2.5" height="2.5" fill="#d97706" />
        <circle cx="37" cy="37" r="1.2" fill="#d97706" />
      </g>
    </svg>
  );
}

export function InstallerDiskSquircle({ size = 48 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-md">
      <defs>
        <linearGradient id="dl-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#30beff" />
          <stop offset="40%" stopColor="#1295fc" />
          <stop offset="100%" stopColor="#006ee6" />
        </linearGradient>
        <linearGradient id="dl-well" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#004ca8" />
          <stop offset="100%" stopColor="#003580" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="58" height="58" rx="14" fill="url(#dl-bg)" />
      <rect x="3.5" y="3.5" width="57" height="28" rx="13.5" fill="white" opacity="0.22" />
      <rect x="3.5" y="3.5" width="57" height="57" rx="13.5" fill="none" stroke="rgba(255,255,255,0.45)" strokeWidth="1" />

      {/* Recessed circular well */}
      <circle cx="32" cy="32" r="16.5" fill="url(#dl-well)" stroke="#002b66" strokeWidth="0.8" />
      {/* Inner top shadow of the well */}
      <path
        d="M15.5 32 A16.5 16.5 0 0 1 48.5 32"
        stroke="rgba(0,0,0,0.35)"
        strokeWidth="2.5"
        fill="none"
      />

      {/* Bold 3D white downward arrow */}
      <g filter="drop-shadow(0 1.5px 2px rgba(0,0,0,0.3))">
        <path
          d="M29 21 H35 V29 H42 L32 41 L22 29 H29 Z"
          fill="#ffffff"
          stroke="#ffffff"
          strokeWidth="1"
          strokeLinejoin="round"
          strokeLinecap="round"
        />
      </g>
    </svg>
  );
}

/* ------------------------------------------------------------------ */
/* 4. Document & File Types (Folded paper with color-coded badge)       */
/* ------------------------------------------------------------------ */

export function G1DocFile({
  size = 48,
  badge = "TXT",
  color = "#3b82f6",
}: {
  size?: number;
  badge?: string;
  color?: string;
}) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="select-none drop-shadow-sm">
      <defs>
        <linearGradient id={`doc-sheet-${badge}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#ffffff" />
          <stop offset="100%" stopColor="#f1f5f9" />
        </linearGradient>
      </defs>
      <path
        d="M14 8 L38 8 L50 20 L50 56 C50 58 48 60 46 60 L14 60 C12 60 10 58 10 56 L10 12 C10 10 12 8 14 8 Z"
        fill={`url(#doc-sheet-${badge})`}
        stroke="#cbd5e1"
        strokeWidth="1"
      />
      <path d="M38 8 L38 20 L50 20 Z" fill="#e2e8f0" stroke="#cbd5e1" strokeWidth="0.8" />
      <rect x="10" y="32" width="40" height="14" fill={color} />
      <text
        x="30"
        y="42.5"
        textAnchor="middle"
        fontFamily="system-ui, sans-serif"
        fontSize="8.5"
        fontWeight="bold"
        fill="#ffffff"
        letterSpacing="0.05em"
      >
        {badge}
      </text>
    </svg>
  );
}

/* ------------------------------------------------------------------ */
/* 5. G1OS Centralized Icon Resolver Component                         */
/* ------------------------------------------------------------------ */

export function G1Icon({
  name,
  size = 24,
  className = "",
  count = 0,
  title,
}: G1IconProps) {
  // Normalize URI schema if string starts with g1os-icon://
  const key = name.replace(/^g1os-icon:\/\//, "").toLowerCase();

  switch (key) {
    // ---------------- Dock & Applications ----------------
    case "finder":
      return <FinderSquircle size={size} />;
    case "browser":
    case "safari":
      return <SafariSquircle size={size} />;
    case "settings":
      return <SettingsSquircle size={size} />;
    case "terminal":
      return <TerminalSquircle size={size} />;
    case "music":
      return <MusicSquircle size={size} />;
    case "photos":
      return <PhotosSquircle size={size} />;
    case "player":
    case "quickplayer":
      return <PlayerSquircle size={size} />;
    case "calc":
    case "calculator":
      return <CalculatorSquircle size={size} />;
    case "textedit":
      return <TextEditSquircle size={size} />;
    case "calendar":
      return <CalendarSquircle size={size} />;
    case "clock":
      return <ClockSquircle size={size} />;
    case "notes":
      return <NotesSquircle size={size} />;
    case "launchpad":
      return <LaunchpadSquircle size={size} />;
    case "installer":
    case "downloads":
      return <InstallerDiskSquircle size={size} />;
    case "sysinfo":
      return (
        <div
          className={`grid place-items-center rounded-[22.5%] bg-gradient-to-b from-sky-400 to-indigo-600 text-white shadow-sm ${className}`}
          style={{ width: size, height: size }}
        >
          <Info size={Math.round(size * 0.54)} strokeWidth={2.2} />
        </div>
      );
    case "trash":
      return <TrashGlassBasket size={size} count={count} />;

    // ---------------- Drives & Storage ----------------
    case "macintosh-hd":
    case "hd":
    case "internal-drive":
    case "system-drive":
      return <MacintoshHDSquircle size={size} />;
    case "usb-drive":
    case "usb":
    case "external-drive":
      return <LiveUsbSquircle size={size} />;
    case "installer-disk":
      return <InstallerDiskSquircle size={size} />;
    case "disk-partition":
    case "storage":
      return <HardDrive size={size} className={className} />;
    case "network-drive":
      return <Globe size={size} className={className} />;

    // ---------------- Folders ----------------
    case "folder":
      return <G1FolderIcon size={size} />;
    case "folder-open":
      return <G1FolderIcon size={size} glyph={<div className="font-mono text-[10px]">▼</div>} />;
    case "folder-documents":
    case "documents":
      return <G1FolderIcon size={size} glyph={<FileText size={14} />} />;
    case "folder-downloads":
      return (
        <G1FolderIcon
          size={size}
          glyph={<div className="font-mono text-[11px] font-bold">↓</div>}
        />
      );
    case "folder-desktop":
    case "desktop":
      return <G1FolderIcon size={size} glyph={<Tv size={13} />} />;
    case "folder-pictures":
    case "pictures":
      return <G1FolderIcon size={size} glyph={<Sparkles size={13} />} />;
    case "folder-music":
      return <G1FolderIcon size={size} glyph={<div className="text-[12px]">♫</div>} />;
    case "folder-movies":
    case "movies":
    case "videos":
      return <G1FolderIcon size={size} glyph={<div className="text-[11px]">▶</div>} />;
    case "folder-apps":
    case "applications":
      return <G1FolderIcon size={size} glyph={<div className="font-sans text-[12px] font-black">A</div>} />;
    case "folder-shared":
    case "shared":
      return <G1FolderIcon size={size} glyph={<Users size={13} />} />;
    case "folder-network":
      return <G1FolderIcon size={size} glyph={<Globe size={13} />} />;
    case "folder-cloud":
      return <G1FolderIcon size={size} glyph={<div className="text-[12px]">☁</div>} />;

    // ---------------- File Types ----------------
    case "file-pdf":
    case "pdf":
      return <G1DocFile size={size} badge="PDF" color="#ef4444" />;
    case "file-text":
    case "text":
      return <G1DocFile size={size} badge="TXT" color="#3b82f6" />;
    case "file-image":
    case "image":
      return <G1DocFile size={size} badge="IMG" color="#06b6d4" />;
    case "file-video":
    case "video":
      return <G1DocFile size={size} badge="MOV" color="#8b5cf6" />;
    case "file-audio":
    case "audio":
      return <G1DocFile size={size} badge="AUD" color="#f43f5e" />;
    case "file-archive":
    case "archive":
      return <G1DocFile size={size} badge="ZIP" color="#f59e0b" />;
    case "file-sheet":
    case "sheet":
      return <G1DocFile size={size} badge="XLS" color="#10b981" />;
    case "file-presentation":
    case "keynote":
      return <G1DocFile size={size} badge="KEY" color="#0284c7" />;
    case "file-code":
    case "code":
      return <G1DocFile size={size} badge="C11" color="#6366f1" />;
    case "file-disk-image":
      return <G1DocFile size={size} badge="DMG" color="#0284c7" />;
    case "file-executable":
      return <G1DocFile size={size} badge="APP" color="#10b981" />;
    case "file-config":
      return <G1DocFile size={size} badge="CFG" color="#64748b" />;
    case "file-generic":
      return <G1DocFile size={size} badge="FILE" color="#94a3b8" />;

    // ---------------- System Controls & Indicators ----------------
    case "wifi":
      return <Wifi size={size} className={className} strokeWidth={2.2} />;
    case "wifi-off":
      return <WifiOff size={size} className={className} strokeWidth={2.2} />;
    case "bluetooth":
      return <Bluetooth size={size} className={className} strokeWidth={2.2} />;
    case "sound":
    case "volume":
      return <Volume2 size={size} className={className} strokeWidth={2.2} />;
    case "volume-mute":
      return <VolumeX size={size} className={className} strokeWidth={2.2} />;
    case "brightness":
    case "display":
      return <Sun size={size} className={className} strokeWidth={2.2} />;
    case "battery":
      return <BatteryCharging size={size} className={className} strokeWidth={2.2} />;
    case "control-center":
      return <Sliders size={size} className={className} strokeWidth={2.2} />;
    case "spotlight":
    case "search":
      return <Search size={size} className={className} strokeWidth={2.2} />;
    case "security":
      return <ShieldCheck size={size} className={className} strokeWidth={2.2} />;
    case "privacy":
      return <Key size={size} className={className} strokeWidth={2.2} />;
    case "user":
    case "users":
      return <User size={size} className={className} strokeWidth={2.2} />;
    case "notifications":
      return <Bell size={size} className={className} strokeWidth={2.2} />;
    case "keyboard":
      return <Keyboard size={size} className={className} strokeWidth={2.2} />;
    case "mouse":
      return <Mouse size={size} className={className} strokeWidth={2.2} />;
    case "trackpad":
      return <Layers size={size} className={className} strokeWidth={2.2} />;
    case "airplay":
      return <Airplay size={size} className={className} strokeWidth={2.2} />;
    case "screen-mirror":
      return <Cast size={size} className={className} strokeWidth={2.2} />;
    case "language":
      return <Globe size={size} className={className} strokeWidth={2.2} />;
    case "updates":
      return <Download size={size} className={className} strokeWidth={2.2} />;

    // ---------------- Actions & States ----------------
    case "power":
    case "shutdown":
      return <Power size={size} className={className} strokeWidth={2.2} />;
    case "restart":
      return <RotateCcw size={size} className={className} strokeWidth={2.2} />;
    case "sleep":
      return <Moon size={size} className={className} strokeWidth={2.2} />;
    case "logout":
      return <LogOut size={size} className={className} strokeWidth={2.2} />;
    case "lock":
      return <Lock size={size} className={className} strokeWidth={2.2} />;
    case "unlock":
      return <Unlock size={size} className={className} strokeWidth={2.2} />;
    case "info":
      return <Info size={size} className={className} strokeWidth={2.2} />;
    case "help":
      return <HelpCircle size={size} className={className} strokeWidth={2.2} />;
    case "warning":
      return <AlertTriangle size={size} className={className} strokeWidth={2.2} />;
    case "error":
      return <XCircle size={size} className={className} strokeWidth={2.2} />;
    case "success":
      return <CheckCircle2 size={size} className={className} strokeWidth={2.2} />;
    case "back":
      return <ChevronLeft size={size} className={className} strokeWidth={2.2} />;
    case "forward":
      return <ChevronRight size={size} className={className} strokeWidth={2.2} />;
    case "refresh":
      return <RefreshCw size={size} className={className} strokeWidth={2.2} />;
    case "close":
      return <X size={size} className={className} strokeWidth={2.2} />;
    case "minimize":
      return <Minus size={size} className={className} strokeWidth={2.2} />;
    case "maximize":
    case "fullscreen":
      return <Maximize2 size={size} className={className} strokeWidth={2.2} />;
    case "share":
      return <Share2 size={size} className={className} strokeWidth={2.2} />;
    case "upload":
      return <Upload size={size} className={className} strokeWidth={2.2} />;
    case "download":
      return <Download size={size} className={className} strokeWidth={2.2} />;
    case "sort":
      return <ArrowUpDown size={size} className={className} strokeWidth={2.2} />;
    case "list-view":
      return <List size={size} className={className} strokeWidth={2.2} />;
    case "grid-view":
      return <LayoutGrid size={size} className={className} strokeWidth={2.2} />;
    case "column-view":
      return <Columns size={size} className={className} strokeWidth={2.2} />;
    case "tags":
      return <Tag size={size} className={className} strokeWidth={2.2} />;
    case "favorites":
      return <Star size={size} className={className} strokeWidth={2.2} />;
    case "new-folder":
      return <FolderPlus size={size} className={className} strokeWidth={2.2} />;
    case "new-file":
      return <FilePlus size={size} className={className} strokeWidth={2.2} />;
    case "copy":
      return <Copy size={size} className={className} strokeWidth={2.2} />;
    case "cut":
      return <Scissors size={size} className={className} strokeWidth={2.2} />;
    case "paste":
      return <Clipboard size={size} className={className} strokeWidth={2.2} />;
    case "rename":
      return <Edit3 size={size} className={className} strokeWidth={2.2} />;
    case "delete":
      return <Trash2 size={size} className={className} strokeWidth={2.2} />;
    case "undo":
      return <Undo2 size={size} className={className} strokeWidth={2.2} />;
    case "redo":
      return <Redo2 size={size} className={className} strokeWidth={2.2} />;

    default:
      return <FileText size={size} className={className} strokeWidth={2} />;
  }
}

/* ------------------------------------------------------------------ */
/* 6. Settings Panel Badges (macOS colorful squircle badges)           */
/* ------------------------------------------------------------------ */

export function G1SettingsBadge({
  id,
  size = 22,
}: {
  id: string;
  size?: number;
}) {
  const BADGES: Record<
    string,
    { grad: string; icon: React.ReactNode; textCol?: string }
  > = {
    perf: {
      grad: "from-blue-500 to-indigo-600",
      icon: <Sliders size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    appearance: {
      grad: "from-fuchsia-500 via-rose-500 to-orange-400",
      icon: <Sparkles size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    dock: {
      grad: "from-cyan-400 to-blue-600",
      icon: <LayoutGrid size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    sound: {
      grad: "from-red-500 to-rose-600",
      icon: <Volume2 size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    wifi: {
      grad: "from-blue-500 to-sky-600",
      icon: <Wifi size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    network: {
      grad: "from-sky-400 to-blue-600",
      icon: <Globe size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    storage: {
      grad: "from-zinc-400 to-zinc-600",
      icon: <HardDrive size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    keyboard: {
      grad: "from-zinc-500 to-zinc-700",
      icon: <Keyboard size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    access: {
      grad: "from-blue-600 to-indigo-700",
      icon: <Eye size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    power: {
      grad: "from-emerald-400 to-teal-600",
      icon: <BatteryCharging size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    updates: {
      grad: "from-amber-400 to-orange-500",
      icon: <Download size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    users: {
      grad: "from-blue-400 to-indigo-600",
      icon: <User size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    security: {
      grad: "from-emerald-500 to-green-700",
      icon: <ShieldCheck size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
    about: {
      grad: "from-sky-400 to-blue-600",
      icon: <Info size={Math.round(size * 0.58)} strokeWidth={2.4} />,
    },
  };

  const item = BADGES[id] || {
    grad: "from-zinc-400 to-zinc-600",
    icon: <Sliders size={Math.round(size * 0.58)} strokeWidth={2.4} />,
  };

  return (
    <div
      className={`grid place-items-center rounded-[6px] bg-gradient-to-b ${item.grad} text-white shadow-sm flex-none`}
      style={{ width: size, height: size }}
    >
      {item.icon}
    </div>
  );
}
