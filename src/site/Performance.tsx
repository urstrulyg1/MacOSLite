import { Reveal, Section, Counter } from "./ui";
import { Power, Feather, Gauge, TimerReset, ArrowRight } from "lucide-react";

const STATS = [
  { icon: Feather, v: 384, dec: 0, suffix: " MB", label: "Idle RAM", sub: "kernel + WM + dock + panel, everything resident" },
  { icon: Gauge, v: 0.4, dec: 1, suffix: " %", label: "Idle CPU", sub: "42 processes · tickless kernel · zero polling" },
  { icon: TimerReset, v: 11.2, dec: 1, suffix: " s", label: "Cold boot", sub: "power-on to interactive desktop · 5400 rpm HDD" },
  { icon: Power, v: 54, dec: 0, suffix: " W", label: "At the wall", sub: "Snow Leopard on the same machine draws 72 W" },
];

const BARS = [
  { name: "macOS 10.13 (last supported)", ram: 3.2, boot: 48, color: "bg-white/25" },
  { name: "Ubuntu 22.04 · GNOME", ram: 1.4, boot: 34, color: "bg-white/25" },
  { name: "Xubuntu · XFCE", ram: 0.7, boot: 26, color: "bg-white/25" },
  { name: "MacLiteOS", ram: 0.384, boot: 11.2, color: "bg-gradient-to-r from-peri to-ember" },
];

const PIPELINE = [
  { t: "0.0 s", name: "Power ON", d: "EFI bless → refind (no splash, no spinner art)" },
  { t: "0.8 s", name: "Kernel", d: "xz payload · deferred initcalls · quiet boot" },
  { t: "2.9 s", name: "DRM/KMS", d: "r600 modesets to native 1920×1080 immediately" },
  { t: "4.3 s", name: "mica-wm", d: "compositor + menubar + dock live; networking still warming up in parallel" },
  { t: "11.2 s", name: "Desktop ready", d: "disk settles, sensors armed, profile applied — before Wi-Fi even finishes DHCP" },
];

export default function Performance() {
  return (
    <Section
      id="performance"
      kicker="02 — Performance"
      title={<>Measured on the metal,<br />not on a slide deck.</>}
      lede="Every number below comes from media-os-benchmark on a real iMac11,2 — a fifteen-year-old machine with a 5400 rpm drive. It idles like it's asleep and wakes like it's new."
    >
      {/* counters */}
      <div className="grid grid-cols-2 gap-3 lg:grid-cols-4">
        {STATS.map((s, i) => (
          <Reveal key={s.label} delay={i * 90}>
            <div className="group rounded-2xl border border-line bg-ink-2 p-5 transition-colors hover:border-peri/40">
              <s.icon size={17} className="text-peri" />
              <div className="mt-4 text-4xl font-bold tracking-tight text-white md:text-5xl">
                <Counter to={s.v} dec={s.dec} suffix={s.suffix} />
              </div>
              <div className="mt-1 text-[13px] font-semibold text-white/80">{s.label}</div>
              <div className="mt-1 text-[11.5px] leading-snug text-white/40">{s.sub}</div>
            </div>
          </Reveal>
        ))}
      </div>

      <div className="mt-12 grid gap-10 lg:grid-cols-2">
        {/* comparison */}
        <Reveal>
          <div className="rounded-2xl border border-line bg-ink-2 p-6">
            <div className="mb-5 flex items-center justify-between">
              <h3 className="text-[15px] font-semibold text-white">The same iMac, four operating systems</h3>
              <span className="font-mono text-[10px] uppercase tracking-wider text-white/30">idle RAM · GB</span>
            </div>
            <div className="space-y-4">
              {BARS.map((b) => (
                <div key={b.name}>
                  <div className="mb-1.5 flex items-baseline justify-between text-[12px]">
                    <span className={b.name === "MacLiteOS" ? "font-semibold text-white" : "text-white/55"}>{b.name}</span>
                    <span className={`font-mono ${b.name === "MacLiteOS" ? "text-emerald-300" : "text-white/40"}`}>
                      {b.ram < 1 ? `${Math.round(b.ram * 1000)} MB` : `${b.ram.toFixed(1)} GB`}
                    </span>
                  </div>
                  <div className="h-2 overflow-hidden rounded-full bg-white/[0.06]">
                    <div className={`h-full rounded-full ${b.color}`} style={{ width: `${(b.ram / 3.2) * 100}%` }} />
                  </div>
                </div>
              ))}
            </div>
            <div className="mt-5 rounded-xl bg-white/[0.04] p-3.5 text-[12px] leading-relaxed text-white/45">
              Modern macOS won't even install on this machine. MacLiteOS runs FASTER than the OS it shipped
              with in 2010 — while looking better than either.
            </div>
          </div>
        </Reveal>

        {/* boot pipeline */}
        <Reveal delay={120}>
          <div className="rounded-2xl border border-line bg-ink-2 p-6">
            <div className="mb-5 flex items-center justify-between">
              <h3 className="text-[15px] font-semibold text-white">Boot, decomposed</h3>
              <span className="font-mono text-[10px] uppercase tracking-wider text-white/30">no login screen by default</span>
            </div>
            <div className="relative space-y-0.5">
              <div className="absolute bottom-3 left-[52px] top-3 w-px bg-gradient-to-b from-peri/60 via-white/10 to-emerald-400/50" />
              {PIPELINE.map((p, i) => (
                <div key={p.name} className="group relative flex items-start gap-4 rounded-xl px-2 py-2.5 transition-colors hover:bg-white/[0.03]">
                  <span className={`w-[72px] flex-none pt-0.5 text-right font-mono text-[11px] ${i === PIPELINE.length - 1 ? "text-emerald-300" : "text-peri/90"}`}>{p.t}</span>
                  <span className={`relative z-10 mt-1 h-2 w-2 flex-none rounded-full ring-4 ring-ink-2 ${i === PIPELINE.length - 1 ? "bg-emerald-400" : "bg-peri"}`} />
                  <div className="min-w-0">
                    <div className="flex items-center gap-2 text-[13px] font-semibold text-white">
                      {p.name}
                      {i < PIPELINE.length - 1 && <ArrowRight size={11} className="text-white/25" />}
                    </div>
                    <div className="text-[11.5px] leading-snug text-white/40">{p.d}</div>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </Reveal>
      </div>

      {/* performance mode strip */}
      <Reveal delay={80}>
        <div className="mt-10 flex flex-col items-start justify-between gap-5 overflow-hidden rounded-2xl border border-line bg-gradient-to-r from-ink-2 via-ink-3 to-ink-2 p-6 md:flex-row md:items-center">
          <div className="max-w-2xl">
            <h3 className="flex items-center gap-2 text-[15px] font-semibold text-white">
              <Gauge size={15} className="text-ember" /> Performance Mode — the honesty switch
            </h3>
            <p className="mt-1.5 text-[12.5px] leading-relaxed text-white/45">
              One toggle (in the Settings window above — try it) bypasses blur, transparency, drop shadows and
              motion. The interface stays premium; the GPU simply stops paying for glass. If VRAM pressure or
              thermal limits are detected, it suggests itself — never nags.
            </p>
          </div>
          <div className="flex flex-none flex-wrap gap-2">
            {["−46 MB compositor", "0 blur passes", "+12 fps while dragging", "GPU −11 °C"].map((t) => (
              <span key={t} className="rounded-full border border-ember/30 bg-ember/10 px-3 py-1 font-mono text-[10.5px] text-ember">{t}</span>
            ))}
          </div>
        </div>
      </Reveal>
    </Section>
  );
}
