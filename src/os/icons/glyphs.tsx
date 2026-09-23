/**
 * Named chrome glyphs for toolbars and menus.
 * These are the G1OS symbol set — not a third-party icon pack.
 * The names exist so existing screens can ask for a glyph without
 * inventing a second drawing style.
 */

import { Symbol } from "./symbols";

type GlyphProps = {
  size?: number;
  className?: string;
  strokeWidth?: number;
  fill?: string;
  title?: string;
};

function glyph(name: string) {
  const G = ({ size = 16, className = "", strokeWidth }: GlyphProps) => (
    <Symbol name={name} size={size} className={className} strokeWidth={strokeWidth} />
  );
  G.displayName = name;
  return G;
}

export const Search = glyph("search");
export const MousePointerClick = glyph("mouse");
export const Layers = glyph("grid");
export const HardDrive = glyph("hard-drive");
export const Trash2 = glyph("trash");
export const AlertTriangle = glyph("warning");
export const RotateCw = glyph("refresh");
export const Power = glyph("power");
export const RotateCcw = glyph("undo");
export const LogOut = glyph("logout");
export const FolderPlus = glyph("folder-plus");
export const Info = glyph("info");
export const Palette = glyph("palette");
export const Check = glyph("check");
export const Wifi = glyph("wifi");
export const Volume2 = glyph("volume");
export const Bell = glyph("bell");
export const BatteryCharging = glyph("battery-charge");
export const MonitorSmartphone = glyph("display");
export const ChevronRight = glyph("chevron-right");
export const ChevronLeft = glyph("chevron-left");
export const ChevronDown = glyph("chevron-down");
export const ChevronUp = glyph("chevron-up");
export const Fan = glyph("fan");
export const Thermometer = glyph("thermometer");
export const Sliders = glyph("sliders");
export const Sun = glyph("sun");
export const Moon = glyph("moon");
export const Zap = glyph("zap");
export const Bluetooth = glyph("bluetooth");
export const Dock = glyph("grid");
export const Gauge = glyph("gauge");
export const Globe = glyph("globe");
export const DownloadCloud = glyph("download");
export const CircleUserRound = glyph("user");
export const ShieldCheck = glyph("shield");
export const Keyboard = glyph("keyboard");
export const Cpu = glyph("cpu");
export const Eye = glyph("eye");
export const Sparkles = glyph("sparkles");
export const Play = glyph("play");
export const Pause = glyph("pause");
export const SkipBack = glyph("skip-back");
export const SkipForward = glyph("skip-forward");
export const MemoryStick = glyph("ram");
export const AudioLines = glyph("audio");
export const MonitorPlay = glyph("film");
export const ZoomIn = glyph("plus");
export const ZoomOut = glyph("minus");
export const Maximize = glyph("maximize");
export const Maximize2 = glyph("maximize");
export const Usb = glyph("hard-drive");
export const CheckCircle2 = glyph("success");
export const XCircle = glyph("error");
export const Terminal = glyph("info");
export const Lock = glyph("lock");
export const ArrowLeft = glyph("back");
export const ArrowRight = glyph("forward");
export const Share = glyph("share");
export const Plus = glyph("plus");
export const Bookmark = glyph("bookmark");
export const Tv = glyph("display");
export const Film = glyph("film");
export const Music = glyph("music-note");
export const ExternalLink = glyph("share");
export const LayoutGrid = glyph("grid-view");
export const List = glyph("list-view");
export const Columns = glyph("column-view");
export const Eject = glyph("eject");
export const Clock = glyph("clock");
export const FileText = glyph("new-file");
export const Download = glyph("download");
export const Music2 = glyph("music-note");
export const Image = glyph("camera");
export const Network = glyph("network");
export const Folder = glyph("folder-plus");
export const FileArchive = glyph("download");
export const Table2 = glyph("list");
export const Presentation = glyph("display");

export type { GlyphProps };
