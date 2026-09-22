import React, { useEffect, useRef, useState } from "react";
import {
  Wifi, Volume2, Search, Bell, BatteryCharging, MonitorSmartphone,
  ChevronRight, Fan, Thermometer,
} from "lucide-react";
import { APPS, useOS, type AppId } from "./os";

/* ---- original logomark: layered "feather" strokes in a squircle ---- */
export function LogoMark({ size = 18, dark = false }: { size?: number; dark?: boolean }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" aria-label="MacLiteOS">
      <rect x="4" y="4" width="56" height="56" rx="16" fill={dark ? "#0b0b10" : "rgba(255,255,255,0.92)"} />
      <path d="M19 44c0-10 6-18 14-24 2 8-1 18-8 24" fill="none" stroke="#0a84ff" strokeWidth="5" strokeLinecap="round" />
      <path d="M29 46c1-7 6-12 12-15 0 7-4 13-10 15" fill="none" stroke="#ff8f5e" strokeWidth="5" strokeLinecap="round" />
    </svg>
  );
}

interface Item { label?: string; kbd?: string; dim?: boolean; run?: () => void; sep?: boolean; }

function MenuList({ items, anchor }: { items: Item[]; anchor: React.ReactNode }) {
  const { menuOpen, setMenuOpen } = useOS();
  const id = useRef(Math.random().toString(36).slice(2)).current;
  const open = menuOpen === id;
  return (
    <div className="relative">
      <button
        onMouseDown={(e) => e.stopPropagation()}
        onClick={() => setMenuOpen(open ? null : id)}
        onMouseEnter={() => {
          if (menuOpen && !["wifi", "vol", "sensors", "bell"].includes(menuOpen)) setMenuOpen(id);
        }}
        className={`px-2.5 h-[26px] flex items-center rounded-[4px] text-[13px] ${open ? "bg-black/10" : ""}`}
        style={{ lineHeight: 1 }}
      >
        {anchor}
      </button>
      {open && (
        <div className="glass panel-in soft-shadow absolute left-0 top-[28px] min-w-[220px] rounded-lg bg-[rgba(242,242,247,0.82)] p-1 shadow-[0_10px_36px_rgba(10,15,40,0.3),0_0_0_0.5px_rgba(0,0,0,0.15)] z-[999]">
          {items.map((it, i) =>
            it.sep ? (
              <div key={i} className="my-1 h-px bg-black/10" />
            ) : (
              <button
                key={i}
                disabled={it.dim}
                onClick={() => { it.run?.(); setMenuOpen(null); }}
                className={`flex w-full items-center justify-between rounded-[5px] px-2.5 py-[3.5px] text-left text-[13px] ${it.dim ? "text-black/30" : "text-black/85 hover:bg-[var(--acc)] hover:text-white"}`}
              >
                <span>{it.label}</span>
                {it.kbd && <span className="text-[11px] opacity-50">{it.kbd}</span>}
              </button>
            )
          )}
        </div>
      )}
    </div>
  );
}

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

export default function MenuBar() {
  const os = useOS();
  const [clock, setClock] = useState(new Date());

  useEffect(() => {
    const t = setInterval(() => setClock(new Date()), 5000);
    return () => clearInterval(t);
  }, []);

  const app = APPS[os.activeApp];
  const focused = os.wins.filter((w) => !w.min && w.ws === os.ws).sort((a, b) => b.z - a.z)[0];

  const openById = (id: AppId) => () => os.openApp(id);

  const appMenus: Item[][] = [
    [
      { label: `About ${app.name}`, run: openById("sysinfo") },
      { sep: true },
      { label: "Settings…", kbd: "⌘,", run: openById("settings") },
      { sep: true },
      { label: `Quit ${app.name}`, kbd: "⌘Q", run: () => focused && os.closeWin(focused.id) },
    ],
    [
      { label: "New Window", kbd: "⌘N", run: () => os.openApp(os.activeApp, undefined) },
      { label: "Open…", kbd: "⌘O", run: openById("finder") },
      { sep: true },
      { label: "Close Window", kbd: "⌘W", run: () => focused && os.closeWin(focused.id) },
    ],
    [
      { label: "Undo", kbd: "⌘Z", dim: true },
      { label: "Redo", kbd: "⇧⌘Z", dim: true },
      { sep: true },
      { label: "Cut", kbd: "⌘X", dim: true },
      { label: "Copy", kbd: "⌘C", dim: true },
      { label: "Paste", kbd: "⌘V", dim: true },
    ],
    [
      { label: "as Icons", kbd: "⌘1", run: () => os.setFinderView("icon") },
      { label: "as List", kbd: "⌘2", run: () => os.setFinderView("list") },
      { sep: true },
      { label: "Enter Full Screen", kbd: "⌃⌘F", run: () => focused && os.toggleFull(focused.id) },
    ],
    [
      { label: "Minimize", kbd: "⌘M", run: () => focused && os.setMin(focused.id, true) },
      { label: "Zoom", run: () => focused && os.toggleFull(focused.id) },
      { sep: true },
      { label: "Switch Workspace 1", run: () => os.setWs(0) },
      { label: "Switch Workspace 2", run: () => os.setWs(1) },
      { label: "Switch Workspace 3", run: () => os.setWs(2) },
      { sep: true },
      { label: "Bring All to Front", run: openById("finder") },
    ],
    [
      { label: `${app.name} Help`, run: () => os.notify("Help", "Light as air", "Every feature ships with offline, searchable documentation.") },
      { label: "Keyboard Shortcuts", run: () => os.notify("Shortcuts", "⌘ Space · ⌘ Tab", "Spotlight, app switcher and window keys are all remappable.") },
    ],
  ];

  const sysMenu: Item[] = [
    { label: "About This Mac", run: openById("sysinfo") },
    { sep: true },
    { label: "System Settings…", run: openById("settings") },
    { label: "App Store…", dim: true },
    { sep: true },
    { label: "Force Quit…", kbd: "⌥⌘⎋", run: () => focused && os.closeWin(focused.id) },
    { sep: true },
    { label: "Sleep", dim: true },
    { label: "Restart…", dim: true },
    { label: "Shut Down…", dim: true },
  ];

  return (
    <div
      className="glass absolute inset-x-0 top-0 z-[500] flex h-7 items-center justify-between bg-[rgba(246,246,250,0.62)] px-2 text-[13px] text-black/85 shadow-[0_0.5px_0_rgba(0,0,0,0.12)]"
      onMouseDown={() => os.setMenuOpen(null)}
    >
      {/* left */}
      <div className="flex items-center gap-0.5">
        <MenuList items={sysMenu} anchor={<span className="-my-0.5 flex items-center"><LogoMark size={16} /></span>} />
        <MenuList items={appMenus[0]} anchor={<b className="font-bold tracking-[-0.01em]">{app.name}</b>} />
        {["File", "Edit", "View", "Window", "Help"].map((m, i) => (
          <MenuList key={m} items={appMenus[i + 1]} anchor={m} />
        ))}
      </div>

      {/* right */}
      <div className="flex items-center gap-1" onMouseDown={(e) => e.stopPropagation()}>
        {/* workspaces */}
        <div className="mr-1 flex items-center gap-1 pr-1.5">
          {[0, 1, 2].map((n) => (
            <button
              key={n}
              title={`Desktop ${n + 1}`}
              onClick={() => os.setWs(n)}
              className="grid place-items-center"
            >
              <span
                className={`block rounded-full transition-all ${os.ws === n ? "h-[7px] w-[7px] bg-[var(--acc)]" : "h-[5px] w-[5px] bg-black/30 hover:bg-black/50"}`}
              />
            </button>
          ))}
        </div>

        <TrayBtn id="wifi" icon={<Wifi size={14} strokeWidth={2.2} />}>
          <div className="w-[240px] p-3">
            <div className="flex items-center justify-between">
              <span className="text-[13px] font-semibold">Wi-Fi</span>
              <div className="os-switch on scale-[0.8]" />
            </div>
            <div className="mt-2 rounded-md bg-black/5 p-2">
              <div className="flex items-center justify-between text-[13px]">
                <span className="font-medium">Starlight 5G</span>
                <Wifi size={13} />
              </div>
              <div className="mt-0.5 text-[11px] text-black/50">Connected · BCM43224 · 130 Mb/s</div>
            </div>
            {["CoffeeHouse Guest", "TP-LINK_E22F"].map((n) => (
              <div key={n} className="mt-1 flex items-center justify-between rounded-md px-2 py-1 text-[13px] text-black/60 hover:bg-black/5">
                {n} <Wifi size={12} className="opacity-40" />
              </div>
            ))}
          </div>
        </TrayBtn>

        <TrayBtn id="vol" icon={<Volume2 size={15} strokeWidth={2.2} />}>
          <div className="w-[220px] p-3">
            <div className="text-[13px] font-semibold">Sound</div>
            <div className="mt-2 flex items-center gap-2">
              <Volume2 size={14} className="text-black/50" />
              <input
                type="range" min={0} max={100} value={os.soundVol}
                onChange={(e) => os.setSoundVol(+e.target.value)}
                className="os-range w-full"
                style={{ ["--v" as any]: `${os.soundVol}%` }}
              />
            </div>
            <div className="mt-2 flex items-center justify-between rounded-md bg-black/5 px-2 py-1.5 text-[12px]">
              <span>Cirrus CS4206 · Internal Speakers</span>
            </div>
          </div>
        </TrayBtn>

        <TrayBtn id="sensors" icon={<Fan size={14} strokeWidth={2.2} />}>
          <div className="w-[230px] p-3 text-[12px]">
            <div className="text-[13px] font-semibold">Sensors · applesmc</div>
            <div className="mt-2 space-y-1.5 text-black/70">
              <div className="flex justify-between"><span className="flex items-center gap-1.5"><Thermometer size={12} /> CPU</span><b>41 °C</b></div>
              <div className="flex justify-between"><span className="flex items-center gap-1.5"><Thermometer size={12} /> GPU · r600</span><b>48 °C</b></div>
              <div className="flex justify-between"><span className="flex items-center gap-1.5"><Fan size={12} /> ODD fan</span><b>1199 rpm</b></div>
              <div className="flex justify-between"><span className="flex items-center gap-1.5"><Fan size={12} /> HDD fan</span><b>1100 rpm</b></div>
            </div>
          </div>
        </TrayBtn>

        <button
          className="rounded-[4px] p-1 hover:bg-black/10"
          title="Spotlight (⌘ Space)"
          onClick={() => os.setSpotlight(true)}
        >
          <Search size={14} strokeWidth={2.4} />
        </button>

        <div className="px-1.5 text-[12.5px] tabular-nums text-black/80">
          {DAYS[clock.getDay()]} {MONTHS[clock.getMonth()]} {clock.getDate()}&nbsp;&nbsp;
          {clock.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}
        </div>

        <TrayBtn id="bell" icon={<Bell size={14} strokeWidth={2.2} />} wide>
          <div className="w-[300px] p-3">
            <div className="flex items-center justify-between">
              <span className="text-[13px] font-semibold">Notification Center</span>
              <button onClick={os.clearNotes} className="text-[11px] text-black/45 hover:text-black/80">Clear All</button>
            </div>
            <div className="mt-2 max-h-[290px] space-y-1.5 overflow-auto">
              {os.notes.length === 0 && (
                <div className="rounded-md bg-black/5 p-3 text-center text-[12px] text-black/45">No new notifications</div>
              )}
              {os.notes.slice().reverse().map((n) => (
                <div key={n.id} className="rounded-lg bg-white/70 p-2.5 shadow-[0_0.5px_2px_rgba(0,0,0,0.15)]">
                  <div className="flex items-center justify-between text-[10.5px] uppercase tracking-wide text-black/40">
                    <span>{n.app}</span><span>{n.time}</span>
                  </div>
                  <div className="mt-0.5 text-[13px] font-semibold leading-tight">{n.title}</div>
                  <div className="text-[12px] leading-snug text-black/60">{n.body}</div>
                </div>
              ))}
            </div>
            <div className="mt-2.5 flex items-center justify-between border-t border-black/10 pt-2.5 text-[12px] text-black/55">
              <span className="flex items-center gap-1.5"><BatteryCharging size={13} /> Power Nap off</span>
              <span className="flex items-center gap-1.5"><MonitorSmartphone size={13} /> Desktop {os.ws + 1} of 3 <ChevronRight size={12} /></span>
            </div>
          </div>
        </TrayBtn>
      </div>
    </div>
  );
}

function TrayBtn({ id, icon, children, wide }: { id: string; icon: React.ReactNode; children: React.ReactNode; wide?: boolean }) {
  const { menuOpen, setMenuOpen } = useOS();
  const open = menuOpen === id;
  return (
    <div className="relative">
      <button
        onClick={() => setMenuOpen(open ? null : id)}
        className={`grid h-[26px] w-[26px] place-items-center rounded-[4px] text-black/75 ${open ? "bg-black/10" : "hover:bg-black/5"}`}
      >
        {icon}
      </button>
      {open && (
        <div className={`glass panel-in absolute right-0 top-[30px] z-[999] rounded-xl bg-[rgba(246,246,250,0.86)] text-black shadow-[0_14px_44px_rgba(10,15,40,0.32),0_0_0_0.5px_rgba(0,0,0,0.15)] ${wide ? "" : ""}`}>
          {children}
        </div>
      )}
    </div>
  );
}
