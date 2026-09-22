import { useMemo, useState } from "react";
import {
  Clock, FileText, Download, Film, Music2, Image as ImageIcon, HardDrive,
  Usb, Eject, Network, ChevronLeft, ChevronRight, LayoutGrid, List, Search,
  Folder as FolderIcon, FileArchive, Table2, Presentation, CircleUserRound,
} from "lucide-react";
import { FS, useOS, type FSItem } from "../os";

const FAVS: { name: string; icon: any }[] = [
  { name: "Recents", icon: Clock },
  { name: "Documents", icon: FileText },
  { name: "Downloads", icon: Download },
  { name: "Movies", icon: Film },
  { name: "Music", icon: Music2 },
  { name: "Pictures", icon: ImageIcon },
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
    const list = FS[loc] ?? [];
    if (!q.trim()) return list;
    return Object.values(FS).flat().filter((f) => f.name.toLowerCase().includes(q.toLowerCase()));
  }, [loc, q]);

  const openItem = (f: FSItem) => {
    if (f.kind === "folder") return;
    if (f.kind === "video") os.openApp("player", f.name, f.name);
    else if (f.kind === "audio") os.openApp("music", f.name, f.name);
    else if (f.kind === "image") os.openApp("photos", f.name, f.name);
    else if (f.kind === "text") os.openApp("textedit", f.name, f.name);
    else os.notify("Finder", f.name, "Previews render on demand — no background indexer required.");
  };

  return (
    <div className="flex h-full text-[13px]">
      {/* sidebar */}
      <aside className="glass flex w-[176px] flex-none flex-col gap-3 overflow-auto border-r border-black/10 bg-black/[0.04] p-2 pt-3">
        <div>
          <div className="px-2 pb-1 text-[11px] font-semibold text-black/40">Favorites</div>
          {FAVS.map(({ name, icon: Icon }) => (
            <button
              key={name}
              onClick={() => { os.setFinderLoc(name); setQ(""); }}
              className={`flex w-full items-center gap-2 rounded-[6px] px-2 py-[4.5px] text-left ${loc === name && !q ? "bg-black/10 font-medium" : "hover:bg-black/5"}`}
            >
              <Icon size={15} className="text-[var(--acc)]" strokeWidth={2.2} />
              {name}
            </button>
          ))}
        </div>
        <div>
          <div className="px-2 pb-1 text-[11px] font-semibold text-black/40">Locations</div>
          <button className="flex w-full items-center gap-2 rounded-[6px] px-2 py-[4.5px] hover:bg-black/5">
            <HardDrive size={15} className="text-black/50" /> iMac HD
          </button>
          {usbMounted && (
            <button
              className="group flex w-full items-center gap-2 rounded-[6px] px-2 py-[4.5px] hover:bg-black/5"
              onDoubleClick={() => os.notify("Finder", "KINGSTON 32 GB", "exFAT volume scanned in 0.4 s. Thumbnails generated on demand.")}
            >
              <Usb size={15} className="text-black/50" />
              <span className="flex-1 text-left">KINGSTON</span>
              <Eject
                size={13}
                className="text-black/30 opacity-0 transition-opacity group-hover:opacity-100 hover:text-black/70"
                onClick={(e) => {
                  e.stopPropagation();
                  setUsbMounted(false);
                  os.notify("Finder", "KINGSTON ejected", "Buffers flushed — 0 writes pending. Safe to remove.");
                }}
              />
            </button>
          )}
          <button className="flex w-full items-center gap-2 rounded-[6px] px-2 py-[4.5px] hover:bg-black/5">
            <Network size={15} className="text-black/50" /> Network
          </button>
        </div>
        <div className="mt-auto rounded-lg bg-black/5 p-2.5 text-[11px] leading-relaxed text-black/50">
          <b className="text-black/70">No indexing daemon.</b> 412.3 GB free. Search scans on demand only.
        </div>
      </aside>

      {/* main */}
      <div className="flex min-w-0 flex-1 flex-col bg-[rgba(252,252,253,0.72)]">
        {/* toolbar */}
        <div className="flex h-[46px] flex-none items-center gap-2 border-b border-black/10 px-3">
          <div className="flex gap-1 text-black/40">
            <button className="rounded-md p-1 hover:bg-black/5"><ChevronLeft size={17} /></button>
            <button className="rounded-md p-1 hover:bg-black/5"><ChevronRight size={17} /></button>
          </div>
          <div className="ml-1 text-[15px] font-bold tracking-tight">{q ? `Search: “${q}”` : loc}</div>
          <div className="ml-auto flex items-center gap-1.5">
            <div className="flex rounded-lg bg-black/[0.06] p-[3px]">
              {([["icon", LayoutGrid], ["list", List]] as const).map(([v, Icon]) => (
                <button
                  key={v}
                  onClick={() => os.setFinderView(v)}
                  className={`rounded-[6px] p-1.5 ${os.finderView === v ? "bg-white shadow-sm" : "text-black/45"}`}
                >
                  <Icon size={14} />
                </button>
              ))}
            </div>
            <div className="flex items-center gap-1.5 rounded-lg bg-black/[0.06] px-2.5 py-[5px]">
              <Search size={13} className="text-black/40" />
              <input
                value={q}
                onChange={(e) => setQ(e.target.value)}
                placeholder="Search"
                className="w-[110px] bg-transparent text-[12.5px] outline-none placeholder:text-black/35"
              />
            </div>
          </div>
        </div>

        {/* content */}
        <div className="min-h-0 flex-1 overflow-auto p-3" onClick={() => setSel(null)}>
          {os.finderView === "icon" ? (
            <div className="grid grid-cols-[repeat(auto-fill,92px)] justify-start gap-1">
              {items.map((f) => (
                <IconCell key={f.name} f={f} sel={sel === f.name} onSel={() => setSel(f.name)} onOpen={() => openItem(f)} />
              ))}
              {items.length === 0 && <Empty q={q} />}
            </div>
          ) : (
            <table className="w-full border-collapse">
              <thead>
                <tr className="text-left text-[11px] text-black/40">
                  <th className="border-b border-black/10 px-2 py-1 font-medium">Name</th>
                  <th className="w-[110px] border-b border-black/10 px-2 py-1 font-medium">Size</th>
                  <th className="w-[170px] border-b border-black/10 px-2 py-1 font-medium">Kind</th>
                </tr>
              </thead>
              <tbody>
                {items.map((f) => {
                  const Icon = f.kind === "folder" ? FolderIcon : KIND_ICON[f.kind];
                  return (
                    <tr
                      key={f.name}
                      onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                      onDoubleClick={() => openItem(f)}
                      className={`cursor-default ${sel === f.name ? "bg-[var(--acc)] text-white" : "hover:bg-black/[0.04]"}`}
                    >
                      <td className="rounded-l-md px-2 py-[5px]">
                        <span className="flex items-center gap-2">
                          {f.kind === "folder" ? (
                            <FolderIcon size={16} className={sel === f.name ? "text-white" : "text-sky-500"} fill="currentColor" strokeWidth={0} />
                          ) : (
                            <Icon size={16} className={sel === f.name ? "text-white" : "text-black/45"} />
                          )}
                          <span className="truncate">{f.name}</span>
                        </span>
                      </td>
                      <td className={`px-2 py-[5px] text-[12px] ${sel === f.name ? "text-white/85" : "text-black/50"}`}>{f.size}</td>
                      <td className={`rounded-r-md px-2 py-[5px] text-[12px] ${sel === f.name ? "text-white/85" : "text-black/50"}`}>
                        {f.meta ?? `${f.kind} document`}
                      </td>
                    </tr>
                  );
                })}
                {items.length === 0 && <tr><td colSpan={3}><Empty q={q} /></td></tr>}
              </tbody>
            </table>
          )}
        </div>

        {/* status bar */}
        <div className="flex h-[26px] flex-none items-center justify-between border-t border-black/10 px-3 text-[11px] text-black/45">
          <span>{items.length} item{items.length === 1 ? "" : "s"}{sel ? `, “${sel}” selected` : ""}</span>
          <span className="flex items-center gap-1.5"><CircleUserRound size={12} /> you · 412.3 GB available</span>
        </div>
      </div>
    </div>
  );
}

function IconCell({ f, sel, onSel, onOpen }: { f: FSItem; sel: boolean; onSel: () => void; onOpen: () => void }) {
  const Icon = f.kind === "folder" ? FolderIcon : KIND_ICON[f.kind];
  return (
    <button
      onClick={(e) => { e.stopPropagation(); onSel(); }}
      onDoubleClick={onOpen}
      className={`flex h-[104px] flex-col items-center justify-start gap-1.5 rounded-lg p-2 ${sel ? "bg-black/10" : "hover:bg-black/[0.05]"}`}
    >
      {f.kind === "folder" ? (
        <FolderIcon size={44} className="text-sky-400 drop-shadow-sm" fill="currentColor" strokeWidth={0} />
      ) : (
        <span className={`grid h-[44px] w-[48px] place-items-center rounded-[9px] bg-gradient-to-b shadow-[0_1px_3px_rgba(0,0,0,0.12)] ${KIND_TILE[f.kind]}`}>
          <Icon size={21} strokeWidth={1.8} />
        </span>
      )}
      <span className={`line-clamp-2 w-full break-words text-center text-[11px] leading-[1.15] ${sel ? "font-medium" : ""}`}>
        {f.name}
      </span>
    </button>
  );
}

const Empty = ({ q }: { q: string }) => (
  <div className="col-span-full py-14 text-center text-[12.5px] text-black/40">
    {q ? `No results for “${q}”. On-demand scan completed in 0.2 s.` : "This folder is empty."}
  </div>
);
