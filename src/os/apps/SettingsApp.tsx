import { useState } from "react";
import {
  Palette, Dock as DockIcon, Gauge, Volume2, Wifi, Globe, HardDrive,
  DownloadCloud, CircleUserRound, ShieldCheck, Info, Search, BatteryCharging,
  Keyboard, Cpu,
} from "lucide-react";
import { useOS } from "../os";

const PANELS = [
  { id: "appearance", name: "Appearance", icon: Palette },
  { id: "dock", name: "Desktop & Dock", icon: DockIcon },
  { id: "perf", name: "Performance", icon: Gauge },
  { id: "sound", name: "Sound", icon: Volume2 },
  { id: "wifi", name: "Wi-Fi", icon: Wifi },
  { id: "network", name: "Network", icon: Globe },
  { id: "keyboard", name: "Keyboard", icon: Keyboard },
  { id: "storage", name: "Storage", icon: HardDrive },
  { id: "power", name: "Energy", icon: BatteryCharging },
  { id: "updates", name: "Software Update", icon: DownloadCloud },
  { id: "users", name: "Users & Login", icon: CircleUserRound },
  { id: "security", name: "Security", icon: ShieldCheck },
  { id: "about", name: "About This Mac", icon: Info },
] as const;

const ACCENTS = ["#0a84ff", "#30b0c7", "#bf5af2", "#ff9f0a", "#32d74b"];
const WALLS = [
  { name: "Sonoma Drift", src: "./wall.jpg" },
  { name: "Lagoon", css: "linear-gradient(135deg,#134e5e,#71b280)" },
  { name: "Ember Dusk", css: "linear-gradient(150deg,#232526,#ff8f5e 130%)" },
];

export default function SettingsApp() {
  const [panel, setPanel] = useState<string>("perf");
  const [q, setQ] = useState("");

  const visible = PANELS.filter((p) => p.name.toLowerCase().includes(q.toLowerCase()));
  const cur = PANELS.find((p) => p.id === panel)!;

  return (
    <div className="flex h-full text-[13px]">
      <aside className="glass flex w-[200px] flex-none flex-col border-r border-black/10 bg-black/[0.04] p-2">
        <div className="mb-2 flex items-center gap-1.5 rounded-lg bg-black/[0.07] px-2 py-[5px]">
          <Search size={13} className="text-black/40" />
          <input
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="Search"
            className="w-full bg-transparent text-[12.5px] outline-none placeholder:text-black/35"
          />
        </div>
        <div className="min-h-0 flex-1 space-y-px overflow-auto">
          {visible.map(({ id, name, icon: Icon }) => (
            <button
              key={id}
              onClick={() => setPanel(id)}
              className={`flex w-full items-center gap-2.5 rounded-[7px] px-2 py-[5.5px] text-left ${panel === id ? "bg-[var(--acc)] text-white" : "hover:bg-black/[0.06]"}`}
            >
              <span className={`grid h-[22px] w-[22px] place-items-center rounded-[6px] ${panel === id ? "bg-white/25" : "bg-gradient-to-b from-zinc-300 to-zinc-400 text-white"}`}>
                <Icon size={13} strokeWidth={2.2} className={panel === id ? "text-white" : ""} />
              </span>
              {name}
            </button>
          ))}
        </div>
        <div className="mt-2 border-t border-black/10 px-2 pt-2 text-[10.5px] leading-relaxed text-black/40">
          Panels mount on demand — only “{cur.name}” is resident in memory.
        </div>
      </aside>

      <div className="min-w-0 flex-1 overflow-auto bg-[rgba(252,252,253,0.78)] p-6">
        <h2 className="mb-4 text-[20px] font-bold tracking-tight">{cur.name}</h2>
        {panel === "perf" && <PerfPanel />}
        {panel === "appearance" && <AppearancePanel />}
        {panel === "dock" && <DockPanel />}
        {panel === "sound" && <SoundPanel />}
        {panel === "wifi" && <WifiPanel />}
        {panel === "network" && <NetworkPanel />}
        {panel === "storage" && <StoragePanel />}
        {panel === "power" && <PowerPanel />}
        {panel === "updates" && <UpdatesPanel />}
        {panel === "users" && <UsersPanel />}
        {panel === "security" && <SecurityPanel />}
        {panel === "keyboard" && <KeyboardPanel />}
        {panel === "about" && <AboutPanel />}
      </div>
    </div>
  );
}

function Card({ title, sub, children }: { title: string; sub?: string; children: React.ReactNode }) {
  return (
    <div className="mb-3 rounded-xl border border-black/[0.08] bg-white/70 p-4 shadow-[0_1px_2px_rgba(0,0,0,0.05)]">
      <div className="text-[13px] font-semibold">{title}</div>
      {sub && <div className="mb-2 mt-0.5 text-[11.5px] leading-snug text-black/50">{sub}</div>}
      <div className="mt-2">{children}</div>
    </div>
  );
}

function Row({ label, hint, children }: { label: string; hint?: string; children?: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between gap-4 py-2">
      <div>
        <div className="text-[13px]">{label}</div>
        {hint && <div className="text-[11px] text-black/45">{hint}</div>}
      </div>
      {children}
    </div>
  );
}

function Toggle({ on, onChange }: { on: boolean; onChange: (v: boolean) => void }) {
  return <div className={`os-switch ${on ? "on" : ""}`} onClick={() => onChange(!on)} />;
}

function Slider({ v, min, max, onChange }: { v: number; min: number; max: number; onChange: (n: number) => void }) {
  return (
    <input
      type="range" min={min} max={max} value={v}
      onChange={(e) => onChange(+e.target.value)}
      className="os-range w-40"
      style={{ ["--v" as any]: `${((v - min) / (max - min)) * 100}%` }}
    />
  );
}

/* ---------------- panels ---------------- */

function PerfPanel() {
  const os = useOS();
  return (
    <>
      <Card title="Performance Mode" sub="Strips transparency, blur, shadows and animation from the desktop. Everything below is measured, not guessed.">
        <Row label="Performance Mode" hint={os.perfMode ? "Active — compositor bypass enabled" : "Off"}>
          <Toggle on={os.perfMode} onChange={os.setPerfMode} />
        </Row>
        <div className="mt-1 grid grid-cols-3 gap-2 pt-2 text-center">
          {[["46 MB", "compositor RAM"], ["0 ms", "blur passes"], ["60 fps", "window drag"]].map(([v, l]) => (
            <div key={l} className="rounded-lg bg-black/[0.04] p-2">
              <div className="text-[16px] font-bold tabular-nums" style={{ color: "var(--acc)" }}>{v}</div>
              <div className="text-[10.5px] text-black/45">{l}</div>
            </div>
          ))}
        </div>
      </Card>
      <Card title="Automatic Profile" sub="Chosen at first boot from GPU + sensor probes.">
        <div className="flex gap-2">
          {["Compatibility", "Balanced", "Multimedia", "Performance"].map((p) => (
            <span key={p} className={`rounded-md border px-2.5 py-1 text-[11.5px] ${p === "Multimedia" ? "border-[var(--acc)] bg-[var(--acc)] text-white" : "border-black/15 text-black/55"}`}>
              {p}
            </span>
          ))}
        </div>
        <div className="mt-2.5 text-[11.5px] text-black/50">
          Detected <b className="text-black/75">RV730 (Radeon HD 4670)</b> with working VDPAU → <b>Multimedia</b> profile selected automatically.
        </div>
      </Card>
    </>
  );
}

function AppearancePanel() {
  const os = useOS();
  return (
    <>
      <Card title="Accent" sub="Applied system-wide as a single CSS variable — repaints cost nothing.">
        <div className="flex gap-3 pt-1">
          {ACCENTS.map((a) => (
            <button
              key={a}
              onClick={() => { os.setAccent(a); document.documentElement.style.setProperty("--acc", a); }}
              className="h-6 w-6 rounded-full transition-transform hover:scale-110"
              style={{ background: a, boxShadow: os.accent === a ? `0 0 0 2px #fff, 0 0 0 4px ${a}` : "inset 0 0 0 0.5px rgba(0,0,0,0.2)" }}
            />
          ))}
        </div>
      </Card>
      <Card title="Wallpaper">
        <div className="flex gap-3">
          {WALLS.map((w, i) => (
            <button key={w.name} onClick={() => os.setWall(i)} className="group">
              <div
                className={`h-16 w-28 rounded-lg bg-cover bg-center transition-shadow ${os.wall === i ? "ring-2 ring-[var(--acc)] ring-offset-2" : ""}`}
                style={w.src ? { backgroundImage: `url(${w.src})` } : { background: (w as any).css }}
              />
              <div className="mt-1 text-center text-[11px] text-black/55 group-hover:text-black/90">{w.name}</div>
            </button>
          ))}
        </div>
      </Card>
    </>
  );
}

function DockPanel() {
  const os = useOS();
  const d = os.dock;
  return (
    <Card title="Dock" sub="Event-driven. Zero polling — idle cost is literally 0.00% CPU.">
      <Row label={`Size — ${d.size}px`}><Slider v={d.size} min={36} max={76} onChange={(n) => os.setDock({ size: n })} /></Row>
      <Row label="Magnification"><Toggle on={d.mag} onChange={(v) => os.setDock({ mag: v })} /></Row>
      {d.mag && <Row label={`Strength — ${Math.round(d.magScale * 100)}%`}><Slider v={Math.round(d.magScale * 100)} min={10} max={100} onChange={(n) => os.setDock({ magScale: n / 100 })} /></Row>}
      <Row label="Automatically hide and show"><Toggle on={d.autohide} onChange={(v) => os.setDock({ autohide: v })} /></Row>
      <Row label="Position on screen">
        <div className="flex rounded-lg bg-black/[0.06] p-[3px]">
          {(["left", "bottom", "right"] as const).map((p) => (
            <button
              key={p}
              onClick={() => os.setDock({ pos: p })}
              className={`rounded-[6px] px-3 py-1 text-[12px] capitalize ${d.pos === p ? "bg-white font-medium shadow-sm" : "text-black/50"}`}
            >
              {p}
            </button>
          ))}
        </div>
      </Row>
    </Card>
  );
}

function SoundPanel() {
  const os = useOS();
  return (
    <Card title="Output" sub="Cirrus Logic CS4206 via ALSA — no sound server running.">
      <Row label="Internal Speakers" hint="Line-level, calibrated">
        <span className="rounded-md bg-[var(--acc)] px-2 py-0.5 text-[11px] font-medium text-white">Active</span>
      </Row>
      <Row label="Headphone Port" hint="Auto-switch on insert" />
      <Row label={`Output volume — ${os.soundVol}%`}>
        <Slider v={os.soundVol} min={0} max={100} onChange={os.setSoundVol} />
      </Row>
    </Card>
  );
}

function WifiPanel() {
  return (
    <Card title="Wi-Fi" sub="Broadcom BCM43224 · brcmfmac · firmware-brcm80211">
      <Row label="Starlight 5G" hint="Connected · WPA2 · −52 dBm">
        <span className="flex items-center gap-1.5 text-[12px] text-emerald-600"><span className="h-2 w-2 rounded-full bg-emerald-500" />Connected</span>
      </Row>
      <Row label="CoffeeHouse Guest" hint="WPA2 · −74 dBm"><span className="text-[12px] text-black/40">Join</span></Row>
      <Row label="TP-LINK_E22F" hint="Open · −81 dBm"><span className="text-[12px] text-black/40">Join</span></Row>
    </Card>
  );
}

function NetworkPanel() {
  return (
    <Card title="Ethernet" sub="Broadcom NetXtreme BCM5764M · tg3">
      <Row label="Status"><span className="flex items-center gap-1.5 text-[12px] text-emerald-600"><span className="h-2 w-2 rounded-full bg-emerald-500" />Connected · 1 Gb/s</span></Row>
      <Row label="IPv4 Address"><span className="font-mono text-[12px]">192.168.1.42</span></Row>
      <Row label="Latency to gateway" hint="ICMP × 8"><span className="font-mono text-[12px]">0.31 ms</span></Row>
      <div className="mt-1 rounded-lg bg-black/[0.04] p-2.5 text-[11.5px] text-black/50">
        Networking initializes <b className="text-black/75">asynchronously</b> — DHCP never stalls boot. Desktop was interactive 4.2 s before this interface configured.
      </div>
    </Card>
  );
}

function StoragePanel() {
  const segs = [
    ["System", 9.4, "#0a84ff"], ["Media", 61.2, "#bf5af2"], ["Documents", 4.1, "#30b0c7"],
    ["Applications", 2.9, "#ff9f0a"], ["Free", 412.3, "rgba(0,0,0,0.1)"],
  ] as const;
  const total = segs.reduce((a, [, v]) => a + (v as number), 0);
  return (
    <Card title="iMac HD — 500 GB SATA" sub="TRIM-capable · noatime · commit=60 (near-zero idle writes)">
      <div className="flex h-3.5 w-full overflow-hidden rounded-full">
        {segs.map(([n, v, c]) => (
          <div key={n} style={{ width: `${((v as number) / total) * 100}%`, background: c }} title={`${n} ${v} GB`} />
        ))}
      </div>
      <div className="mt-2.5 flex flex-wrap gap-x-4 gap-y-1">
        {segs.map(([n, v, c]) => (
          <span key={n} className="flex items-center gap-1.5 text-[11.5px] text-black/55">
            <span className="h-2.5 w-2.5 rounded-[4px]" style={{ background: c }} />{n} {v} GB
          </span>
        ))}
      </div>
    </Card>
  );
}

function PowerPanel() {
  return (
    <Card title="Energy" sub="iMacs have no battery — so the menu bar shows none. Hardware truthfulness by design.">
      <Row label="SATA link power management"><Toggle on onChange={() => {}} /></Row>
      <Row label="Spin down disk after 10 min idle"><Toggle on onChange={() => {}} /></Row>
      <Row label="Radeon dynamic power (dynpm)"><Toggle on onChange={() => {}} /></Row>
      <div className="mt-1 rounded-lg bg-black/[0.04] p-2.5 text-[11.5px] text-black/50">
        Measured at the wall: <b className="text-black/75">54 W idle</b> — 18 W below Mac OS X Snow Leopard on the same machine.
      </div>
    </Card>
  );
}

function UpdatesPanel() {
  const os = useOS();
  return (
    <Card title="Software Update" sub="Signed delta images · atomic A/B partitions · instant rollback. Checks run only on demand — no background daemon.">
      <div className="flex items-center justify-between rounded-lg bg-black/[0.04] p-3">
        <div>
          <div className="text-[14px] font-semibold">MacLiteOS 0.4.2</div>
          <div className="text-[11.5px] text-black/50">Your system is up to date · checked 12 min ago</div>
        </div>
        <button
          onClick={() => os.notify("Software Update", "MacLiteOS is up to date", "0.4.2 · 612 MB image · signature verified (ed25519).")}
          className="rounded-lg bg-[var(--acc)] px-3.5 py-1.5 text-[12.5px] font-medium text-white shadow-sm active:scale-95"
        >
          Check Now
        </button>
      </div>
      <Row label="Automatically download deltas"><Toggle on={false} onChange={() => {}} /></Row>
      <Row label="Install security responses overnight"><Toggle on onChange={() => {}} /></Row>
    </Card>
  );
}

function UsersPanel() {
  return (
    <Card title="Users & Login" sub="Boots straight to the desktop in 11.2 s. A login screen costs RAM and time — enable one only if you need it.">
      <Row label="you — Owner" hint="uid 1000 · no password (local trust)">
        <span className="rounded-md bg-emerald-500/15 px-2 py-0.5 text-[11px] font-medium text-emerald-700">Current</span>
      </Row>
      <Row label="Automatic login" hint="Desktop at power-on"><Toggle on onChange={() => {}} /></Row>
      <Row label="Require password after sleep"><Toggle on={false} onChange={() => {}} /></Row>
    </Card>
  );
}

function SecurityPanel() {
  return (
    <Card title="Security" sub="Minimal by construction: fewer services means a smaller attack surface.">
      <Row label="Firewall" hint="nftables · 6 rules · no open listeners"><Toggle on onChange={() => {}} /></Row>
      <Row label="Signed system updates" hint="ed25519 · verified before apply"><Toggle on onChange={() => {}} /></Row>
      <Row label="Telemetry" hint="There is none. There never will be."><Toggle on={false} onChange={() => {}} /></Row>
    </Card>
  );
}

function KeyboardPanel() {
  const rows: [string, string][] = [
    ["Spotlight", "⌘ Space"],
    ["App switcher", "⌘ Tab"],
    ["Close window", "⌘ W"],
    ["Quit app", "⌘ Q"],
    ["Minimize", "⌘ M"],
    ["Full screen", "⌃ ⌘ F"],
    ["Workspace 1–3", "⌃ 1 2 3"],
  ];
  return (
    <Card title="Keyboard Shortcuts" sub="Every binding is a plain-text rule — remap anything.">
      {rows.map(([a, k]) => (
        <Row key={a} label={a}>
          <span className="rounded-md bg-black/[0.06] px-2 py-1 font-mono text-[11.5px]">{k}</span>
        </Row>
      ))}
    </Card>
  );
}

function AboutPanel() {
  return (
    <>
      <div className="mb-4 flex items-center gap-4">
        <div className="grid h-16 w-16 place-items-center rounded-2xl bg-gradient-to-b from-zinc-200 to-zinc-300 shadow-inner">
          <Cpu size={30} className="text-zinc-600" />
        </div>
        <div>
          <div className="text-[17px] font-bold tracking-tight">iMac (21.5-inch, Mid 2010)</div>
          <div className="text-[12px] text-black/50">iMac11,2 · Serial W89123A5DB7 · MacLiteOS 0.4.2</div>
        </div>
      </div>
      <Card title="Hardware">
        {[
          ["Chip", "Intel Core i3-540 · 3.06 GHz · 2 cores / 4 threads"],
          ["Memory", "8 GB 1333 MHz DDR3 (2 × 4 GB)"],
          ["Graphics", "ATI Radeon HD 4670 · 256 MB GDDR3 · r600 · VDPAU"],
          ["Display", '21.5" LED-backlit · 1920 × 1080 native'],
          ["Storage", "500 GB SATA HDD · ext4 (noatime)"],
          ["Audio", "Cirrus Logic CS4206 · ALSA direct"],
          ["Wi-Fi", "Broadcom BCM43224 802.11n · brcmfmac"],
        ].map(([k, v]) => (
          <div key={k} className="flex items-baseline justify-between gap-6 border-b border-black/[0.06] py-1.5 last:border-0">
            <span className="w-20 flex-none text-[12px] text-black/45">{k}</span>
            <span className="text-right text-[12.5px]">{v}</span>
          </div>
        ))}
      </Card>
    </>
  );
}
