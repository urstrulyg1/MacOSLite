import { Reveal, Section, Chip } from "./ui";
import {
  ScanLine, Film, Thermometer, Fan, BadgeCheck, FlaskConical,
  MonitorSmartphone, GitBranch, CircleCheck, CircleX, TriangleAlert,
} from "lucide-react";

const PROBE = [
  { icon: ScanLine, cmd: "lspci -nn", finds: "RV730/M96-XT [HD 4670]", then: "loads r600.ko · 2D EXA off, KMS on" },
  { icon: GitBranch, cmd: "modprobe vdpau", finds: "G3DVL driver string", then: "binds FFmpeg hwaccel → mpv" },
  { icon: Thermometer, cmd: "sensors applesmc", finds: "4 fans · 9 temp zones", then: "maps fan curves to GPU/CPU/HDD" },
  { icon: Fan, cmd: "profile select", finds: "MULTIMEDIA", then: "hw decode on, UI effects budgeted" },
];

const CODECS: [string, string, boolean | "partial", string][] = [
  ["H.264 / AVC", "VDPAU · High@L4.1", true, "1080p30 locked, single-digit CPU"],
  ["MPEG-2 (DVD)", "VDPAU · Main", true, "optical drive plays native"],
  ["VC-1", "VDPAU · Advanced", true, "legacy rips covered"],
  ["MPEG-4 ASP / Xvid", "FFmpeg-mt", true, "2 cores is plenty"],
  ["VP8", "FFmpeg-mt", true, "720p smooth"],
  ["VP9", "FFmpeg-mt", "partial", "720p ok · 1080p borderline"],
  ["HEVC / H.265", "—", false, "RV730 predates it; rejected at probe, honest about it"],
];

const PROFILE_CARDS = [
  { name: "Compatibility", when: "unknown GPU · sensors missing", fx: ["software decode", "no effects", "VESA-safe modes"] },
  { name: "Balanced", when: "mainline GPU · working sensors", fx: ["subtle animations", "hw decode", "standard shadows"] },
  { name: "Multimedia", when: "VDPAU verified", fx: ["hw video", "gapless audio", "disk streaming buffers"], active: true },
  { name: "Performance", when: "user or thermals choose it", fx: ["0 blur passes", "no transparency", "instant UI"] },
];

export default function Hardware() {
  return (
    <Section
      id="hardware"
      kicker="03 — Hardware"
      title={<>It knows the 2010 iMac<br />better than macOS does.</>}
      lede="First boot is a hardware interview, not a setup wizard. Every probe below runs in under two seconds and re-runs silently if it detects a GPU swap, a dead fan, or a new USB audio device."
    >
      {/* probe cascade */}
      <div className="grid gap-3 md:grid-cols-2 lg:grid-cols-4">
        {PROBE.map((p, i) => (
          <Reveal key={p.cmd} delay={i * 100}>
            <div className="group relative h-full rounded-2xl border border-line bg-ink-2 p-5 transition-colors hover:border-peri/40">
              <div className="flex items-center justify-between">
                <p.icon size={17} className="text-peri" />
                <span className="font-mono text-[10px] text-white/25">0{i + 1}</span>
              </div>
              <code className="mt-4 block rounded-lg bg-black/40 px-3 py-2 font-mono text-[11.5px] text-emerald-300">$ {p.cmd}</code>
              <div className="mt-3 text-[12.5px] font-semibold text-white">{p.finds}</div>
              <div className="mt-1 flex items-center gap-1.5 text-[11.5px] text-white/40">
                <span className="text-peri">→</span> {p.then}
              </div>
            </div>
          </Reveal>
        ))}
      </div>

      <div className="mt-12 grid gap-8 lg:grid-cols-[1.25fr_1fr]">
        {/* codec matrix */}
        <Reveal>
          <div className="overflow-hidden rounded-2xl border border-line bg-ink-2">
            <div className="flex items-center gap-2 border-b border-line px-5 py-3.5">
              <Film size={15} className="text-ember" />
              <span className="text-[13px] font-semibold text-white">Decode matrix — RV730, verified</span>
              <span className="ml-auto font-mono text-[10.5px] text-white/35">vdpauinfo + ffmpeg -hwaccel</span>
            </div>
            {CODECS.map(([name, eng, ok, note]) => (
              <div key={name} className="flex items-center gap-3 border-b border-line/50 px-5 py-2.5 last:border-0 hover:bg-white/[0.02]">
                {ok === true ? <CircleCheck size={15} className="flex-none text-emerald-400" /> :
                 ok === "partial" ? <TriangleAlert size={15} className="flex-none text-amber-400" /> :
                 <CircleX size={15} className="flex-none text-rose-400/70" />}
                <span className="w-[130px] flex-none text-[12.5px] font-semibold text-white">{name}</span>
                <span className="hidden w-[150px] flex-none font-mono text-[10.5px] text-white/40 sm:block">{eng}</span>
                <span className="min-w-0 flex-1 truncate text-right text-[11.5px] text-white/45">{note}</span>
              </div>
            ))}
            <div className="border-t border-line bg-white/[0.02] px-5 py-3 text-[11.5px] leading-relaxed text-white/40">
              If hardware decode fails mid-playback, the pipeline hot-swaps to multithreaded FFmpeg
              and drops a one-line notification. The desktop never blinks. Try it: kill the KMS device,
              Finder keeps its 60 fps.
            </div>
          </div>
        </Reveal>

        {/* auto profiles */}
        <div className="space-y-3">
          {PROFILE_CARDS.map((p, i) => (
            <Reveal key={p.name} delay={i * 90}>
              <div className={`rounded-2xl border p-4 transition-colors ${p.active ? "border-peri/50 bg-peri/[0.08]" : "border-line bg-ink-2 hover:border-white/20"}`}>
                <div className="flex items-center justify-between">
                  <span className="text-[13.5px] font-semibold text-white">{p.name}</span>
                  {p.active && <span className="rounded-full bg-peri px-2.5 py-0.5 font-mono text-[9.5px] uppercase tracking-wider text-ink">auto-selected</span>}
                </div>
                <div className="mt-0.5 font-mono text-[10.5px] text-white/35">{p.when}</div>
                <div className="mt-2 flex flex-wrap gap-1.5">
                  {p.fx.map((f) => (
                    <span key={f} className="rounded-md bg-white/[0.06] px-2 py-0.5 text-[10.5px] text-white/55">{f}</span>
                  ))}
                </div>
              </div>
            </Reveal>
          ))}
        </div>
      </div>

      {/* honesty badges */}
      <Reveal delay={60}>
        <div className="mt-12 grid gap-3 md:grid-cols-3">
          <div className="flex items-start gap-3 rounded-2xl border border-emerald-400/25 bg-emerald-400/[0.06] p-5">
            <BadgeCheck size={18} className="mt-0.5 flex-none text-emerald-400" />
            <div>
              <div className="text-[13px] font-semibold text-white">Real iMac11,2 tested</div>
              <div className="mt-1 text-[12px] leading-relaxed text-white/50">
                Boot, GPU, Wi-Fi, audio, sensors, 1080p VDPAU — verified on the actual Mid-2010 machine.
                Every claim on this page traces to a benchmark log.
              </div>
            </div>
          </div>
          <div className="flex items-start gap-3 rounded-2xl border border-line bg-ink-2 p-5">
            <FlaskConical size={18} className="mt-0.5 flex-none text-peri" />
            <div>
              <div className="text-[13px] font-semibold text-white">QEMU validated nightly</div>
              <div className="mt-1 text-[12px] leading-relaxed text-white/50">
                run-vm.sh boots the ISO in &lt;20 s for CI. Clearly labeled as emulation —
                no hardware-acceleration claims are made from QEMU alone.
              </div>
            </div>
          </div>
          <div className="flex items-start gap-3 rounded-2xl border border-line bg-ink-2 p-5">
            <MonitorSmartphone size={18} className="mt-0.5 flex-none text-ember" />
            <div>
              <div className="text-[13px] font-semibold text-white">Other 2010 models</div>
              <div className="mt-1 text-[12px] leading-relaxed text-white/50">
                27" (iMac11,3 · HD 5670/r600) and i5 variants share the probe pipeline —
                benching queue open, results published uncut.
              </div>
            </div>
          </div>
        </div>
      </Reveal>

      <Reveal delay={100}>
        <div className="mt-6 flex flex-wrap items-center gap-2">
          <Chip tone="green">EFI boot ✓</Chip>
          <Chip tone="green">native 1920×1080 ✓</Chip>
          <Chip tone="green">Wi-Fi N ✓</Chip>
          <Chip tone="green">Gigabit Ethernet ✓</Chip>
          <Chip tone="green">speakers + headphones ✓</Chip>
          <Chip tone="green">USB 2.0 ✓</Chip>
          <Chip tone="green">SuperDrive ✓</Chip>
          <Chip tone="green">applesmc fans ✓</Chip>
          <Chip tone="amber">SD reader · in progress</Chip>
        </div>
      </Reveal>
    </Section>
  );
}
