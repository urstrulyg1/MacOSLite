import { useEffect, useMemo, useRef, useState } from "react";
import { useOS, type FSItem, type FinderView } from "../os";
import { G1Icon } from "../icons/IconSystem";
import { Symbol } from "../icons/symbols";

const FAVS: { name: string; iconName: string }[] = [
  { name: "Recents", iconName: "clock" },
  { name: "Desktop", iconName: "folder-desktop" },
  { name: "Documents", iconName: "folder-documents" },
  { name: "Downloads", iconName: "folder-downloads" },
  { name: "Movies", iconName: "folder-movies" },
  { name: "Music", iconName: "folder-music" },
  { name: "Pictures", iconName: "folder-pictures" },
  { name: "Trash", iconName: "trash" },
];

const VIEWS: { id: FinderView; icon: string; label: string }[] = [
  { id: "icon", icon: "grid-view", label: "Icon view" },
  { id: "list", icon: "list-view", label: "List view" },
  { id: "column", icon: "column-view", label: "Column view" },
];

export default function FinderApp() {
  const os = useOS();
  const [q, setQ] = useState("");
  const [sel, setSel] = useState<string | null>(null);
  const [renaming, setRenaming] = useState<string | null>(null);
  const [draft, setDraft] = useState("");
  const [usbMounted, setUsbMounted] = useState(true);
  const [dir, setDir] = useState<1 | -1>(1);
  const hist = useRef<string[]>([os.finderLoc]);
  const histAt = useRef(0);
  const skipHist = useRef(false);
  const loc = os.finderLoc;

  useEffect(() => {
    if (skipHist.current) {
      skipHist.current = false;
      return;
    }
    if (hist.current[histAt.current] === loc) return;
    hist.current = hist.current.slice(0, histAt.current + 1).concat(loc);
    histAt.current = hist.current.length - 1;
    setDir(1);
    setSel(null);
    setQ("");
  }, [loc]);

  const go = (delta: number) => {
    const next = histAt.current + delta;
    if (next < 0 || next >= hist.current.length) return;
    histAt.current = next;
    skipHist.current = true;
    setDir(delta < 0 ? -1 : 1);
    os.setFinderLoc(hist.current[next]);
  };

  const items = useMemo(() => {
    if (q.trim()) {
      return Object.entries(os.fs).flatMap(([place, list]) =>
        list.filter((f) => f.name.toLowerCase().includes(q.toLowerCase())).map((f) => ({ ...f, place })),
      );
    }
    return (os.fs[loc] ?? []).map((f) => ({ ...f, place: loc }));
  }, [os.fs, loc, q]);

  const openItem = (f: FSItem & { place?: string }) => {
    if (f.kind === "folder") {
      if (os.fs[f.name]) os.setFinderLoc(f.name);
      else os.notify("Finder", f.name, "Folder is empty.", "folder");
      return;
    }
    if (f.kind === "video") os.openApp("player", f.name, f.name);
    else if (f.kind === "audio") os.openApp("music", f.name, f.name);
    else if (f.kind === "image") os.openApp("photos", f.name, f.name);
    else if (f.kind === "text" || f.kind === "pdf") os.openApp("textedit", f.name, f.name);
    else os.notify("Finder", f.name, "Opening in the native viewer.", `file-${f.kind}`);
  };

  const commitRename = () => {
    if (renaming) os.renameFsItem(loc, renaming, draft);
    setRenaming(null);
  };

  const selected = items.find((f) => f.name === sel) ?? null;

  return (
    <div className="finder flex h-full select-none text-[13px]">
      <aside className="finder-side">
        <div className="finder-label">Favorites</div>
        {FAVS.map(({ name, iconName }) => (
          <button
            key={name}
            onClick={() => os.setFinderLoc(name)}
            className={`finder-row ${loc === name && !q ? "on" : ""}`}
          >
            <G1Icon name={iconName} size={16} />
            <span>{name}</span>
            {name === "Trash" && (os.fs.Trash || []).length > 0 && (
              <span className="finder-count">{os.fs.Trash.length}</span>
            )}
          </button>
        ))}
        <div className="finder-label mt-3">Locations</div>
        <button className="finder-row" onClick={() => os.setFinderLoc("Documents")}>
          <G1Icon name="macintosh-hd" size={16} /> Macintosh HD
        </button>
        {usbMounted && (
          <button className="finder-row group" onClick={() => os.setFinderLoc("Downloads")}>
            <G1Icon name="usb-drive" size={16} />
            <span className="min-w-0 flex-1 truncate text-left">G1OS USB</span>
            <span
              className="opacity-0 group-hover:opacity-100"
              onClick={(e) => {
                e.stopPropagation();
                setUsbMounted(false);
                os.notify("Finder", "G1OS USB ejected", "Buffers flushed. Safe to disconnect.", "usb");
              }}
            >
              <Symbol name="eject" size={13} />
            </span>
          </button>
        )}
        <button className="finder-row dim" disabled>
          <G1Icon name="network-drive" size={16} /> Network
        </button>
      </aside>

      <div className="flex min-w-0 flex-1 flex-col">
        <div className="finder-toolbar">
          <button className="icon-btn" aria-label="Back" disabled={histAt.current <= 0} onClick={() => go(-1)}>
            <Symbol name="chevron-left" size={16} />
          </button>
          <button className="icon-btn" aria-label="Forward" disabled={histAt.current >= hist.current.length - 1} onClick={() => go(1)}>
            <Symbol name="chevron-right" size={16} />
          </button>
          <div className="ml-1 text-[15px] font-semibold tracking-tight">{q ? `Search · “${q}”` : loc}</div>
          {loc === "Trash" && (os.fs.Trash || []).length > 0 && (
            <button className="finder-empty" onClick={() => os.setPowerState("trash_dialog")}>Empty Trash</button>
          )}
          <div className="ml-auto flex items-center gap-2">
            <button
              className="icon-btn"
              title="New Folder"
              onClick={() => {
                const name = uniqueName(os.fs[loc] || [], "Untitled Folder");
                os.addFsItem(loc, { name, kind: "folder", size: "—" });
                setSel(name);
                setRenaming(name);
                setDraft(name);
              }}
            >
              <Symbol name="folder-plus" size={15} />
            </button>
            <div className="finder-seg">
              {VIEWS.map((v) => (
                <button
                  key={v.id}
                  title={v.label}
                  className={os.finderView === v.id ? "on" : ""}
                  onClick={() => os.setFinderView(v.id)}
                >
                  <Symbol name={v.icon} size={14} />
                </button>
              ))}
            </div>
            <label className="finder-search">
              <Symbol name="search" size={13} />
              <input value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search" />
            </label>
          </div>
        </div>

        <div className="relative min-h-0 flex-1 overflow-hidden" onClick={() => setSel(null)}>
          <div key={loc + os.finderView + (q ? "q" : "")} className={`finder-pane ${dir < 0 ? "from-left" : "from-right"}`}>
            {items.length === 0 ? (
              <div className="grid h-full place-items-center py-16 text-[13px] opacity-50">
                {loc === "Trash" ? "Trash is empty" : "This folder is empty"}
              </div>
            ) : os.finderView === "column" ? (
              <div className="finder-columns">
                <div className="finder-col">
                  {FAVS.map((f) => (
                    <button key={f.name} className={`finder-col-item ${loc === f.name ? "on" : ""}`} onClick={() => os.setFinderLoc(f.name)}>
                      <G1Icon name={f.iconName} size={16} />
                      <span className="truncate">{f.name}</span>
                      <Symbol name="chevron-right" size={12} />
                    </button>
                  ))}
                </div>
                <div className="finder-col">
                  {items.map((f) => (
                    <button
                      key={f.name}
                      className={`finder-col-item ${sel === f.name ? "on" : ""}`}
                      onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                      onDoubleClick={() => openItem(f)}
                      onContextMenu={(e) => { e.preventDefault(); if (loc !== "Trash") os.moveToTrash(f, f.place); }}
                    >
                      <G1Icon name={f.kind === "folder" ? "folder" : `file-${f.kind}`} size={16} />
                      <span className="truncate">{f.name}</span>
                    </button>
                  ))}
                </div>
                <div className="finder-preview">
                  {selected ? (
                    <>
                      <G1Icon name={selected.kind === "folder" ? "folder" : `file-${selected.kind}`} size={72} />
                      <div className="mt-3 text-[13px] font-semibold">{selected.name}</div>
                      <div className="mt-1 text-[11.5px] opacity-55">{selected.meta || selected.kind} · {selected.size}</div>
                    </>
                  ) : (
                    <div className="text-[12px] opacity-45">Select an item</div>
                  )}
                </div>
              </div>
            ) : os.finderView === "list" ? (
              <div className="px-2 py-1">
                {items.map((f) => (
                  <div
                    key={f.place + f.name}
                    className={`finder-list ${sel === f.name ? "on" : ""}`}
                    onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                    onDoubleClick={() => openItem(f)}
                    onContextMenu={(e) => { e.preventDefault(); if (loc !== "Trash") os.moveToTrash(f, f.place); }}
                  >
                    <G1Icon name={f.kind === "folder" ? "folder" : `file-${f.kind}`} size={18} />
                    {renaming === f.name ? (
                      <input
                        autoFocus
                        className="finder-rename"
                        value={draft}
                        onChange={(e) => setDraft(e.target.value)}
                        onBlur={commitRename}
                        onKeyDown={(e) => { if (e.key === "Enter") commitRename(); if (e.key === "Escape") setRenaming(null); }}
                        onClick={(e) => e.stopPropagation()}
                      />
                    ) : (
                      <span className="truncate">{f.name}</span>
                    )}
                    <span className="ml-auto w-28 truncate text-right text-[11.5px] opacity-55">{f.meta || f.kind}</span>
                    <span className="w-16 text-right font-mono text-[11.5px] opacity-70">{f.size}</span>
                  </div>
                ))}
              </div>
            ) : (
              <div className="grid grid-cols-4 gap-1 p-3 sm:grid-cols-5 md:grid-cols-6">
                {items.map((f) => (
                  <button
                    key={f.place + f.name}
                    className={`finder-icon ${sel === f.name ? "on" : ""} ${f.born ? "born" : ""}`}
                    onClick={(e) => { e.stopPropagation(); setSel(f.name); }}
                    onDoubleClick={() => openItem(f)}
                    onContextMenu={(e) => { e.preventDefault(); if (loc !== "Trash") os.moveToTrash(f, f.place); }}
                  >
                    <G1Icon name={f.kind === "folder" ? "folder" : `file-${f.kind}`} size={48} state={sel === f.name ? "selected" : "idle"} />
                    {renaming === f.name ? (
                      <input
                        autoFocus
                        className="finder-rename"
                        value={draft}
                        onChange={(e) => setDraft(e.target.value)}
                        onBlur={commitRename}
                        onKeyDown={(e) => { if (e.key === "Enter") commitRename(); if (e.key === "Escape") setRenaming(null); }}
                        onClick={(e) => e.stopPropagation()}
                      />
                    ) : (
                      <span>{f.name}</span>
                    )}
                  </button>
                ))}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function uniqueName(list: FSItem[], base: string) {
  const names = new Set(list.map((f) => f.name));
  if (!names.has(base)) return base;
  let i = 2;
  while (names.has(`${base} ${i}`)) i++;
  return `${base} ${i}`;
}
