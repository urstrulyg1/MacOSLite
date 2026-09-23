import { useState } from "react";
import {
  Palette, Dock as DockIcon, Gauge, Volume2, Wifi, Globe, HardDrive,
  DownloadCloud, CircleUserRound, ShieldCheck, Info, Search, BatteryCharging,
  Keyboard, Cpu, Sliders, Eye, Sun, Sparkles, Check,
} from "lucide-react";
import { useOS, type VisualMode } from "../os";
import { G1SettingsBadge } from "../icons/IconSystem";

const PANELS = [
  { id: "perf", name: "Performance & Mode", icon: Gauge },
  { id: "appearance", name: "Appearance", icon: Palette },
  { id: "dock", name: "Desktop & Dock", icon: DockIcon },
  { id: "sound", name: "Sound", icon: Volume2 },
  { id: "wifi", name: "Wi-Fi (AirPort)", icon: Wifi },
  { id: "network", name: "Network", icon: Globe },
  { id: "storage", name: "Storage", icon: HardDrive },
  { id: "keyboard", name: "Keyboard & Shortcuts", icon: Keyboard },
  { id: "access", name: "Accessibility", icon: Eye },
  { id: "power", name: "Energy & SMC Fans", icon: BatteryCharging },
  { id: "updates", name: "Software Update", icon: DownloadCloud },
  { id: "users", name: "Users & Accounts", icon: CircleUserRound },
  { id: "security", name: "Security & Privacy", icon: ShieldCheck },
  { id: "about", name: "About G1OS", icon: Info },
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
  const cur = PANELS.find((p) => p.id === panel) || PANELS[0];

  return (
    <div className="flex h-full text-[13px] bg-[#f5f6fa] select-none text-zinc-900">
      <aside className="glass flex w-[220px] flex-none flex-col border-r border-black/10 bg-black/[0.04] p-2">
        <div className="mb-2 flex items-center gap-1.5 rounded-lg bg-black/[0.07] px-2.5 py-[6px]">
          <Search size={13} className="text-black/40" />
          <input
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="Search Settings"
            className="w-full bg-transparent text-[12.5px] outline-none placeholder:text-black/35 text-black"
          />
        </div>
        <div className="min-h-0 flex-1 space-y-0.5 overflow-auto">
          {visible.map(({ id, name }) => (
            <button
              key={id}
              onClick={() => setPanel(id)}
              className={`flex w-full items-center gap-2.5 rounded-[7px] px-2.5 py-[6px] text-left transition-colors cursor-pointer ${
                panel === id ? "bg-[var(--acc)] text-white shadow-sm font-medium" : "hover:bg-black/[0.06] text-black/80"
              }`}
            >
              <G1SettingsBadge id={id} size={22} />
              <span className="truncate">{name}</span>
            </button>
          ))}
        </div>
        <div className="mt-2 border-t border-black/10 px-2 pt-2 text-[10.5px] leading-relaxed text-black/40">
          G1OS System Preferences · Zero memory leaks.
        </div>
      </aside>

      <div className="min-w-0 flex-1 overflow-auto bg-[rgba(252,252,253,0.85)] p-6">
        <div key={panel} className="animate-in fade-in slide-in-from-bottom-2 duration-200 ease-out">
          <h2 className="mb-4 text-[22px] font-bold tracking-tight text-black">{cur.name}</h2>
          {panel === "perf" && <PerfPanel />}
          {panel === "appearance" && <AppearancePanel />}
          {panel === "dock" && <DockPanel />}
          {panel === "sound" && <SoundPanel />}
          {panel === "wifi" && <WifiPanel />}
          {panel === "network" && <NetworkPanel />}
          {panel === "storage" && <StoragePanel />}
          {panel === "keyboard" && <KeyboardPanel />}
          {panel === "access" && <AccessPanel />}
          {panel === "power" && <PowerPanel />}
          {panel === "updates" && <UpdatesPanel />}
          {panel === "users" && <UsersPanel />}
          {panel === "security" && <SecurityPanel />}
          {panel === "about" && <AboutPanel />}
        </div>
      </div>
    </div>
  );
}

function Card({ title, sub, children }: { title: string; sub?: string; children: React.ReactNode }) {
  return (
    <div className="mb-4 rounded-xl border border-black/[0.08] bg-white/80 p-4 shadow-[0_1px_3px_rgba(0,0,0,0.05)]">
      <div className="text-[13.5px] font-semibold text-black">{title}</div>
      {sub && <div className="mb-2.5 mt-0.5 text-[11.5px] leading-snug text-black/55">{sub}</div>}
      <div className="mt-2 text-black/85">{children}</div>
    </div>
  );
}

function Row({ label, hint, children }: { label: string; hint?: string; children?: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between border-b border-black/[0.05] py-2.5 last:border-0">
      <div>
        <div className="text-[12.5px] font-medium text-black/90">{label}</div>
        {hint && <div className="text-[11px] text-black/45">{hint}</div>}
      </div>
      {children}
    </div>
  );
}

function Toggle({ on, onChange }: { on: boolean; onChange: (v: boolean) => void }) {
  return (
    <button
      onClick={() => onChange(!on)}
      className={`h-[22px] w-[38px] rounded-full p-[2px] transition-colors cursor-pointer ${on ? "bg-blue-600" : "bg-black/20"}`}
    >
      <div className={`h-[18px] w-[18px] rounded-full bg-white shadow transition-transform ${on ? "translate-x-4" : "translate-x-0"}`} />
    </button>
  );
}

function Slider({ v, min, max, onChange }: { v: number; min: number; max: number; onChange: (v: number) => void }) {
  return (
    <input
      type="range"
      min={min}
      max={max}
      value={v}
      onChange={(e) => onChange(+e.target.value)}
      className="os-range w-36"
      style={{ ["--v" as any]: `${((v - min) / (max - min)) * 100}%` }}
    />
  );
}

function PerfPanel() {
  const os = useOS();

  return (
    <>
      <Card title="Hardware-Aware Visual Modes" sub="Dynamically adapts UI rendering to the iMac Mid-2010 GPU and CPU capabilities.">
        <div className="grid grid-cols-3 gap-3 pt-1">
          {[
            {
              id: "performance" as VisualMode,
              name: "Performance",
              tag: "60 FPS Guarantee",
              desc: "Zero blur, minimal shadows, lightweight CSS transforms. Best for heavy multitasking.",
            },
            {
              id: "balanced" as VisualMode,
              name: "Balanced",
              tag: "Default iMac 2010",
              desc: "Optimized cubic-bezier transitions, subtle glass tint, soft shadows. Smooth and responsive.",
            },
            {
              id: "beautiful" as VisualMode,
              name: "Beautiful",
              tag: "Full Effects",
              desc: "Enhanced glass reflections, glow effects, rich animations when hardware acceleration is active.",
            },
          ].map((m) => {
            const isSelected = os.visualMode === m.id;
            return (
              <div
                key={m.id}
                onClick={() => os.setVisualMode(m.id)}
                className={`p-3.5 rounded-xl border transition-all cursor-pointer ${
                  isSelected
                    ? "border-blue-600 bg-blue-50/70 shadow-sm ring-1 ring-blue-500"
                    : "border-black/10 bg-black/[0.02] hover:bg-black/[0.04]"
                }`}
              >
                <div className="flex items-center justify-between">
                  <span className="font-bold text-[13px] text-black">{m.name}</span>
                  {isSelected && <Check size={15} className="text-blue-600" />}
                </div>
                <div className="mt-1 text-[10px] font-semibold uppercase tracking-wider text-blue-600/80">
                  {m.tag}
                </div>
                <p className="mt-1.5 text-[11px] leading-relaxed text-black/60">{m.desc}</p>
              </div>
            );
          })}
        </div>
      </Card>

      <Card title="Live Compositor Metrics" sub="Real-time performance measurements from the active session.">
        <div className="grid grid-cols-4 gap-3 text-center">
          {[
            ["0.4%", "Idle CPU"],
            ["384 MB", "System RSS"],
            ["60.0 FPS", "Frame Pacing"],
            ["1.12 ms", "Damage Render"],
          ].map(([v, l]) => (
            <div key={l} className="rounded-lg bg-black/[0.03] p-3">
              <div className="text-[17px] font-bold tabular-nums text-blue-600">{v}</div>
              <div className="text-[10.5px] text-black/50 mt-0.5">{l}</div>
            </div>
          ))}
        </div>
      </Card>
    </>
  );
}

function AppearancePanel() {
  const os = useOS();
  return (
    <>
      <Card title="Accent Color" sub="Applied system-wide as a single dynamic variable — zero repainting overhead.">
        <div className="flex gap-3 pt-1">
          {ACCENTS.map((a) => (
            <button
              key={a}
              onClick={() => { os.setAccent(a); document.documentElement.style.setProperty("--acc", a); }}
              className="h-7 w-7 rounded-full transition-transform hover:scale-110 cursor-pointer"
              style={{ background: a, boxShadow: os.accent === a ? `0 0 0 2px #fff, 0 0 0 4px ${a}` : "inset 0 0 0 0.5px rgba(0,0,0,0.2)" }}
            />
          ))}
        </div>
      </Card>
      <Card title="Desktop Wallpaper">
        <div className="flex gap-3">
          {WALLS.map((w, i) => (
            <button key={w.name} onClick={() => os.setWall(i)} className="group cursor-pointer">
              <div
                className={`h-16 w-28 rounded-lg bg-cover bg-center transition-shadow ${os.wall === i ? "ring-2 ring-blue-600 ring-offset-2" : ""}`}
                style={w.src ? { backgroundImage: `url(${w.src})` } : { background: (w as any).css }}
              />
              <div className="mt-1 text-center text-[11px] text-black/60 group-hover:text-black/90 font-medium">{w.name}</div>
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
    <Card title="Dock Preferences" sub="Hardware-accelerated parabolic magnification with 60 FPS fluidity.">
      <Row label={`Size — ${d.size}px`}><Slider v={d.size} min={36} max={76} onChange={(n) => os.setDock({ size: n })} /></Row>
      <Row label="Magnification"><Toggle on={d.mag} onChange={(v) => os.setDock({ mag: v })} /></Row>
      {d.mag && <Row label={`Strength — ${Math.round(d.magScale * 100)}%`}><Slider v={Math.round(d.magScale * 100)} min={10} max={100} onChange={(n) => os.setDock({ magScale: n / 100 })} /></Row>}
      <Row label="Automatically hide and show the Dock"><Toggle on={d.autohide} onChange={(v) => os.setDock({ autohide: v })} /></Row>
    </Card>
  );
}

function SoundPanel() {
  const os = useOS();
  return (
    <Card title="Sound Output" sub="Direct ALSA routing — no high-latency sound server daemon.">
      <Row label="Internal Stereo Speakers" hint="Cirrus Logic CS4206 calibrated">
        <span className="rounded-md bg-blue-600 px-2 py-0.5 text-[11px] font-medium text-white">Active</span>
      </Row>
      <Row label="Headphone Sense Port" hint="Hardware jack detection online" />
      <Row label={`Volume Level — ${os.soundVol}%`}>
        <Slider v={os.soundVol} min={0} max={100} onChange={os.setSoundVol} />
      </Row>
    </Card>
  );
}

function WifiPanel() {
  return (
    <Card title="AirPort Extreme Wi-Fi" sub="Broadcom BCM43224 · 802.11a/b/g/n · Native b43 driver">
      <Row label="Starlight 5G" hint="Connected · WPA2 · −52 dBm">
        <span className="flex items-center gap-1.5 text-[12px] text-emerald-600 font-medium">
          <span className="h-2 w-2 rounded-full bg-emerald-500 animate-pulse" />Connected
        </span>
      </Row>
      <Row label="iMac-Studio-5G" hint="Known Network · −68 dBm"><span className="text-[12px] text-black/40">Join</span></Row>
      <Row label="CoffeeHouse Guest" hint="Open · −74 dBm"><span className="text-[12px] text-black/40">Join</span></Row>
    </Card>
  );
}

function NetworkPanel() {
  return (
    <Card title="Gigabit Ethernet" sub="Broadcom NetXtreme BCM5764M · tg3">
      <Row label="Link Speed"><span className="text-emerald-600 font-medium text-[12px]">1.0 Gbps Full Duplex</span></Row>
      <Row label="IP Address"><span className="font-mono text-[12px]">192.168.1.42</span></Row>
      <Row label="Subnet / Gateway"><span className="font-mono text-[12px]">255.255.255.0 · 192.168.1.1</span></Row>
    </Card>
  );
}

function StoragePanel() {
  return (
    <Card title="Internal SATA Storage (Macintosh HD)" sub="500.1 GB Solid-State Drive · ext4 with TRIM">
      <div className="flex h-4 w-full overflow-hidden rounded-full bg-black/10">
        <div style={{ width: "2%" }} className="bg-emerald-500" title="EFI 256MB" />
        <div style={{ width: "8%" }} className="bg-blue-500" title="System Base 4GB" />
        <div style={{ width: "18%" }} className="bg-purple-500" title="User Data 90GB" />
        <div style={{ width: "72%" }} className="bg-zinc-200" title="Free Space 405GB" />
      </div>
      <div className="mt-3 flex flex-wrap gap-x-5 gap-y-1.5 text-[11.5px] text-black/60">
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded bg-emerald-500" />EFI (256 MB)</span>
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded bg-blue-500" />G1OS Base (4.2 GB)</span>
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded bg-purple-500" />Data (90.4 GB)</span>
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded bg-zinc-300" />Free (405.2 GB)</span>
      </div>
    </Card>
  );
}

function AccessPanel() {
  const os = useOS();
  return (
    <Card title="Accessibility" sub="Ensures the interface remains clear and comfortable on vintage displays.">
      <Row label="Reduce Motion" hint="Disables window scaling and parabolic magnification">
        <Toggle on={os.reduceMotion} onChange={os.setReduceMotion} />
      </Row>
      <Row label="Increase Contrast" hint="Emphasizes window borders and control outlines">
        <Toggle on={false} onChange={() => {}} />
      </Row>
    </Card>
  );
}

function PowerPanel() {
  return (
    <Card title="Energy & Thermal Control" sub="AppleSMC dynamic fan control eliminates SSD upgrade fan noise.">
      <Row label="HDD Fan Quiet Target" hint="Calibrated to 1100 RPM (replaces 6000 RPM runaway)">
        <span className="font-mono text-emerald-600 font-semibold text-[12px]">1100 RPM</span>
      </Row>
      <Row label="ODD Optical Fan" hint="Whisper quiet baseline"><span className="font-mono text-[12px]">1000 RPM</span></Row>
      <Row label="CPU Fan" hint="Proportional cooling baseline"><span className="font-mono text-[12px]">1200 RPM</span></Row>
    </Card>
  );
}

function UpdatesPanel() {
  const os = useOS();
  return (
    <Card title="Software Update" sub="Delta packages with atomic rollback protection.">
      <div className="flex items-center justify-between rounded-lg bg-black/[0.04] p-3">
        <div>
          <div className="text-[14px] font-semibold text-black">G1OS 1.0</div>
          <div className="text-[11.5px] text-black/55">Your system is up to date · Giving life to older machines.</div>
        </div>
        <button
          onClick={() => os.notify("Software Update", "G1OS is Up to Date", "G1OS 1.0 · EFI 1.10 · All signatures verified.")}
          className="rounded-lg bg-blue-600 px-4 py-1.5 text-[12.5px] font-medium text-white shadow-sm hover:bg-blue-500 cursor-pointer"
        >
          Check Now
        </button>
      </div>
    </Card>
  );
}

function UsersPanel() {
  return (
    <Card title="Users & Accounts" sub="User account information for this iMac.">
      <Row label="Jeevan" hint="Administrator · Local Account">
        <span className="rounded-md bg-emerald-500/15 px-2 py-0.5 text-[11px] font-medium text-emerald-700">Active User</span>
      </Row>
    </Card>
  );
}

function SecurityPanel() {
  return (
    <Card title="Security & Privacy" sub="Lightweight security architecture with zero cloud telemetry.">
      <Row label="Kernel Hardening" hint="Independent isolated process spaces"><Toggle on onChange={() => {}} /></Row>
      <Row label="Zero Telemetry" hint="No background data collection"><Toggle on onChange={() => {}} /></Row>
    </Card>
  );
}

function KeyboardPanel() {
  const rows: [string, string][] = [
    ["Spotlight Search", "⌘ Space"],
    ["App Switcher", "⌘ Tab"],
    ["Close Window", "⌘ W"],
    ["Quit Application", "⌘ Q"],
    ["Minimize", "⌘ M"],
    ["Full Screen", "⌃ ⌘ F"],
  ];
  return (
    <Card title="Keyboard Shortcuts" sub="Classic Mac key combinations supported natively.">
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
        <div className="grid h-16 w-16 place-items-center rounded-2xl bg-gradient-to-tr from-sky-400 to-blue-600 text-white shadow-lg shadow-blue-900/20">
          <Cpu size={32} />
        </div>
        <div>
          <div className="text-[20px] font-bold tracking-tight text-black">G1OS 1.0</div>
          <div className="text-[13px] font-medium text-blue-600">“Giving life to older machines.”</div>
          <div className="text-[11.5px] text-black/50 mt-0.5">iMac (21.5-inch, Mid-2010) · iMac11,2</div>
        </div>
      </div>
      <Card title="Hardware Specifications">
        {[
          ["Processor", "3.06 GHz Intel Core i3 (Clarkdale 32nm)"],
          ["Memory", "4 GB 1333 MHz DDR3 SDRAM"],
          ["Graphics", "ATI Radeon HD 4670 256 MB GDDR3 (Mesa r600 Gallium)"],
          ["Display", '21.5-inch LED-backlit IPS · 1920 × 1080 native 60Hz'],
          ["Storage", "Crucial CT500MX500SSD1 (500.1 GB Internal SATA)"],
          ["Audio", "Cirrus Logic CS4206 HD Audio (Internal Stereo)"],
          ["Networking", "Broadcom AirPort Extreme 802.11n + Gigabit Ethernet"],
          ["Boot Mode", "Apple EFI 1.10 (BOOTX64.EFI Fallback Active)"],
        ].map(([k, v]) => (
          <div key={k} className="flex items-baseline justify-between gap-6 border-b border-black/[0.06] py-1.5 last:border-0">
            <span className="w-28 flex-none text-[12px] font-medium text-black/50">{k}</span>
            <span className="text-right text-[12.5px] text-black/85 font-mono">{v}</span>
          </div>
        ))}
      </Card>
    </>
  );
}
