import { useMemo, useState } from "react";
import {
  Clock, FileText, Download, Film, Music2, Image as ImageIcon, HardDrive,
  Usb, Eject, Network, ChevronLeft, ChevronRight, LayoutGrid, List, Search,
  Folder as FolderIcon, FileArchive, Table2, Presentation, Trash2,
} from "lucide-react";
import { useOS, type FSItem } from "../os";
import { G1Icon } from "../icons/IconSystem";

const FAVS: { name: string; iconName: string }[] = [
  { name: "Recents", iconName: "clock" },
  { name: "Documents", iconName: "folder-documents" },
  { name: "Downloads", iconName: "folder-downloads" },
  { name: "Movies", iconName: "folder-movies" },
  { name: "Music", iconName: "folder-music" },
  { name: "Pictures", iconName: "folder-pictures" },
  { name: "Trash", iconName: "trash" },
];

const KIND_ICON: Record<string, any> = {
  text: FileText, pdf: FileText, image: ImageIcon, video: Film, audio: Music2,
  archive: FileArchive, sheet: Table2, keynote: Presentation, folder: FolderIcon,
};
const KIND_TILE: Record<string, string> = {
  text: "from-slate-100 to-slate-200 text-slate-500",
  pdf: "from-rose-100 to-rose-200 text-rose-500",
  image: "from-teal-100 to-teal-200 text-teal-600",
  video: "from-violet-100 to-violet-200 text-violet-600",
  audio: "from-red-100 to-red-200 text-red-500",
  archive: "from-amber-100 to-amber-200 text-amber-600",
  sheet: "from-emerald-100 to-emerald-200 text-emerald-600",
  keynote: "from-sky-100 to-sky-200 text-sky-600",
  folder: "from-sky-300 to-blue-500 text-white",
};

export default function FinderApp() {
  const os = useOS();
  const [q, setQ] = useState("");
  const [sel, setSel] = useState<string | null>(null);
  const [usbMounted, setUsbMounted] = useState(true);
  const loc = os.finderLoc;

  const items = useMemo(() => {
    const list = os.fs[loc] ?? [];
    if (!q.trim()) return list;
    return Object.values(os.fs).flat().filter((f) => f.name.toLowerCase().includes(q.toLowerCase()));
  }, [os.fs, loc, q]);

  const openItem = (f: FSItem) => {
    if (f.kind === "folder") return;
    if (f.kind === "video") os.openApp("player", f.name, f.name);
    else if (f.kind === "audio") os.openApp("music", f.name, f.name);
    else if (f.kind === "image") os.openApp("photos", f.name, f.name);
    else if (f.kind === "text") os.openApp("textedit", f.name, f.name);
    else os.notify("Finder", f.name, "Opening in native viewer.");
  };

  return (
    <div className="flex h-full text-[13px] bg-[#f8f9fc] select-none text-zinc-900">
      {/* sidebar */}
      <aside className="glass flex w-[184px] flex-none flex-col gap-3 overflow-auto border-r border-black/10 bg-black/[0.04] p-2 pt-3">
        <div>
          <div className="px-2 pb-1 text-[11px] font-semibold text-black/45 uppercase tracking-wider">Favorites</div>
          {FAVS.map(({ name, iconName }) => (
            <button
              key={name}
              onClick={() => { os.setFinderLoc(name); setQ(""); }}
              className={`flex w-full items-center gap-2.5 rounded-[6px] px-2.5 py-[5px] text-left transition-colors cursor-pointer ${
                loc === name && !q ? "bg-black/10 font-semibold text-black" : "hover:bg-black/5 text-black/75"
              }`}
            >
              <G1Icon name={iconName} size={16} className={name === "Trash" ? "text-zinc-600" : "text-blue-600"} />
              <span>{name}</span>
              {name === "Trash" && (os.fs.Trash || []).length > 0 && (
                <span className="ml-auto rounded-full bg-black/10 px-1.5 py-0.2 text-[10px] font-bold text-black/60">
                  {(os.fs.Trash || []).length}
                </span>
              )}
            </button>
          ))}
        </div>
        <div>
          <div className="px-2 pb-1 text-[11px] font-semibold text-black/45 uppercase tracking-wider">Locations</div>
          <button
            onClick={() => { os.setFinderLoc("Documents"); setQ(""); }}
            className="flex w-full items-center gap-2.5 rounded-[6px] px-2.5 py-[5px] hover:bg-black/5 text-black/75 cursor-pointer"
          >
            <G1Icon name="macintosh-hd" size={16} /> Macintosh HD
          </button>
          {usbMounted && (
            <button
              className="group flex w-full items-center gap-2.5 rounded-[6px] px-2.5 py-[5px] hover:bg-black/5 text-black/75 cursor-pointer"
              onClick={() => { os.setFinderLoc("Downloads"); setQ(""); }}
            >
              <G1Icon name="usb-drive" size={16} />
              <span className="flex-1 text-left truncate">G1OS Live USB</span>
              <Eject
                size={13}
                className="text-black/40 opacity-0 transition-opacity group-hover:opacity-100 hover:text-black/80"
                onClick={(e) => {
                  e.stopPropagation();
                  setUsbMounted(false);
                  os.notify("Finder", "G1OS USB Ejected", "Buffers synchronized. Safe to disconnect drive.");
                }}
              />
            </button>
          )}
          <button className="flex w-full items-center gap-2.5 rounded-[6px] px-2.5 py-[5px] hover:bg-black/5 text-black/50 cursor-not-allowed">
            <G1Icon name="network-drive" size={16} className="text-black/40" /> Network
          </button>
        </div>
        <div className="mt-auto rounded-lg bg-black/5 p-2.5 text-[11px] leading-relaxed text-black/50">
          <b className="text-black/70">G1OS Fast Storage</b> · 405.2 GB free space available.
        </div>
      </aside>

      {/* main view */}
      <div className="flex min-w-0 flex-1 flex-col bg-[rgba(252,252,253,0.85)]">
        {/* toolbar */}
        <div className="flex h-[46px] flex-none items-center gap-2 border-b border-black/10 px-3">
          <div className="flex gap-1 text-black/50">
            <button className="rounded-md p-1 hover:bg-black/5 cursor-pointer"><ChevronLeft size={17} /></button>
            <button className="rounded-md p-1 hover:bg-black/5 cursor-pointer"><ChevronRight size={17} /></button>
          </div>
          <div className="ml-1 text-[15px] font-bold tracking-tight text-black">{q ? `Search: “${q}”` : loc}</div>

          {loc === "Trash" && (os.fs.Trash || []).length > 0 && (
            <button
              onClick={() => os.setPowerState("trash_dialog")}
              className="ml-3 rounded-lg bg-red-600/10 hover:bg-red-600/20 text-red-600 border border-red-600/20 px-2.5 py-1 text-[11.5px] font-semibold transition-colors cursor-pointer"
            >
              Empty Trash
            </button>
          )}

          <div className="ml-auto flex items-center gap-2">
            <div className="flex rounded-lg bg-black/[0.06] p-[3px]">
              {([["icon", LayoutGrid], ["list", List]] as const).map(([v, Icon]) => (
                <button
                  key={v}
                  onClick={() => os.setFinderView(v)}
                  className={`rounded-[6px] p-1.5 cursor-pointer ${os.finderView === v ? "bg-white shadow-sm text-black" : "text-black/45"}`}
                >
                  <Icon size={14} />
                </button>
              ))}
            </div>

            <div className="flex w-44 items-center gap-1.5 rounded-lg bg-black/[0.06] px-2.5 py-1 text-[12px]">
              <Search size={13} className="text-black/40" />
              <input
                value={q}
                onChange={(e) => setQ(e.target.value)}
                placeholder="Search"
                className="w-full bg-transparent outline-none placeholder:text-black/35 text-black"
              />
            </div>
          </div>
        </div>

        {/* content */}
        <div className="flex-1 overflow-auto p-4" onClick={() => setSel(null)}>
          <div key={loc + os.finderView} className="animate-in fade-in duration-180">
            {items.length === 0 ? (
              <div className="grid h-full place-items-center text-center text-black/40 text-[13px] py-16">
                {loc === "Trash" ? "Trash is empty" : "No items found in this directory"}
              </div>
            ) : os.finderView === "icon" ? (
              <div className="grid grid-cols-4 sm:grid-cols-5 md:grid-cols-6 gap-3">
                {items.map((f) => {
                  const Icon = KIND_ICON[f.kind] || FileText;
                  const tile = KIND_TILE[f.kind] || "from-slate-100 to-slate-200 text-slate-500";
                  const isSelected = sel === f.name;
                  return (
                    <button
                      key={f.name}
                      onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                      onDoubleClick={() => openItem(f)}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        if (loc !== "Trash") os.moveToTrash(f);
                      }}
                      className={`flex flex-col items-center gap-1.5 rounded-xl p-2.5 text-center transition-all duration-120 active:scale-95 cursor-pointer ${
                        isSelected ? "bg-[var(--acc)] text-white shadow-sm" : "hover:bg-black/5 text-black/85"
                      }`}
                    >
                      <div className="transition-transform duration-120 group-hover:scale-105 drop-shadow-sm flex items-center justify-center h-12 w-12">
                        <G1Icon name={f.kind === "folder" ? "folder" : `file-${f.kind}`} size={46} />
                      </div>
                      <span className="text-[12px] font-medium leading-tight max-w-[90px] truncate" title={f.name}>
                        {f.name}
                      </span>
                      <span className={`text-[10px] ${isSelected ? "text-white/70" : "text-black/45"}`}>{f.size}</span>
                    </button>
                  );
                })}
              </div>
            ) : (
              <div className="space-y-0.5">
                {items.map((f) => {
                  const isSelected = sel === f.name;
                return (
                  <div
                    key={f.name}
                    onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                    onDoubleClick={() => openItem(f)}
                    onContextMenu={(e) => {
                      e.preventDefault();
                      if (loc !== "Trash") os.moveToTrash(f);
                    }}
                    className={`flex items-center justify-between rounded-lg px-3 py-1.5 text-[12.5px] transition-colors cursor-pointer ${
                      isSelected ? "bg-[var(--acc)] text-white shadow-sm font-medium" : "hover:bg-black/5 text-black/85"
                    }`}
                  >
                    <div className="flex items-center gap-2.5 truncate">
                      <G1Icon name={f.kind === "folder" ? "folder" : `file-${f.kind}`} size={18} className={isSelected ? "text-white" : "text-blue-600"} />
                      <span className="truncate">{f.name}</span>
                    </div>
                    <div className="flex items-center gap-6 text-[11.5px]">
                      <span className={isSelected ? "text-white/80" : "text-black/50"}>{f.meta || f.kind}</span>
                      <span className={`w-16 text-right font-mono ${isSelected ? "text-white/90" : "text-black/60"}`}>{f.size}</span>
                    </div>
                  </div>
                );
              })}
            </div>
          )}
          </div>
        </div>
      </div>
    </div>
  );
}
