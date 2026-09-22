import { Reveal, Section } from "./ui";
import {
  Cpu, MonitorSmartphone, AppWindow, Layers3, HardDriveDownload, Leaf,
  Wifi, AudioLines, FolderTree, Cog, CircleCheck, CircleMinus,
} from "lucide-react";

const LAYERS = [
  {
    icon: AppWindow,
    name: "Core Applications",
    desc: "Finder · Settings · Terminal · mpv-backed media · native only — eleven apps, zero webviews",
    ram: "≈ 120 MB busy",
    tone: "from-peri/25 to-peri/5 text-peri",
  },
  {
    icon: Layers3,
    name: "mica-wm — custom Window Manager",
    desc: "Menu bar, dock, workspaces, notifications, launcher. Event-driven; renders only when something changes",
    ram: "38 MB resident",
    tone: "from-ember/25 to-ember/5 text-ember",
  },
  {
    icon: MonitorSmartphone,
    name: "Display system",
    desc: "DRM/KMS direct scanout. No X11, no Wayland stack, no compositor daemon — frames go straight to the GPU",
    ram: "0 MB idle cost",
    tone: "from-teal-400/25 to-teal-400/5 text-teal-300",
  },
  {
    icon: Cpu,
    name: "Linux 6.12 · maclite config",
    desc: "tickless NOHZ_FULL · DRM/KMS · ALSA · r600 · brcmfmac · applesmc · ext4 with near-zero idle writeback",
    ram: "210 MB",
    tone: "from-zinc-400/25 to-zinc-400/5 text-zinc-300",
  },
  {
    icon: Cog,
    name: "iMac Mid 2010",
    desc: "i3-540 / i5-760 · RV730 graphics · CS4206 audio · BCM43224 Wi-Fi — every quirk accounted for",
    ram: "the whole point",
    tone: "from-white/15 to-white/5 text-white/70",
  },
];

const FOUNDATIONS: { name: string; rows: [boolean | string, boolean | string, boolean | string, boolean | string, string]; win?: boolean }[] = [
  { name: "Buildroot", rows: ["hand-rolled", "manual", false, "~90 MB", "Fragile. Firmware becomes your day job."] },
  { name: "Alpine", rows: [true, "partial", true, "~180 MB", "musl + r600 VDPAU fights back. Respect, no."] },
  { name: "Arch minimal", rows: [true, true, true, "~420 MB", "Great stack, rolling-release risk on a museum piece."] },
  { name: "Debian 12 netinst", rows: [true, true, true, "~290 MB", "firmware + mesa + vdpau from apt, 5-year LTS.", ] , win: true },
  { name: "Custom rootfs", rows: [true, "manual", false, "~140 MB", "Phase 2 — after hardware matrix is proven."] },
];

export default function Architecture() {
  return (
    <Section
      id="architecture"
      kicker="01 — Architecture"
      title={<>Four layers.<br />Every byte earns its place.</>}
      lede="No GNOME. No Plasma. No Electron shell pretending to be a desktop. Each layer below directly serves the Mac-like experience, the hardware, or the media pipeline — anything else was deleted."
    >
      <div className="grid gap-10 lg:grid-cols-[1fr_1.2fr] lg:gap-14">
        {/* stack */}
        <div className="space-y-2.5">
          {LAYERS.map((l, i) => (
            <Reveal key={l.name} delay={i * 90}>
              <div className="group relative overflow-hidden rounded-2xl border border-line bg-ink-2 p-4 transition-colors hover:border-peri/40">
                <div className={`pointer-events-none absolute inset-0 bg-gradient-to-br opacity-0 transition-opacity duration-500 group-hover:opacity-100 ${l.tone.split(" ").slice(0, 2).join(" ")}`} />
                <div className="relative flex items-start gap-4">
                  <span className={`grid h-10 w-10 flex-none place-items-center rounded-xl bg-gradient-to-br ${l.tone.split(" ").slice(0, 2).join(" ")} ${l.tone.split(" ")[2]} ring-1 ring-white/10`}>
                    <l.icon size={18} />
                  </span>
                  <div className="min-w-0 flex-1">
                    <div className="flex items-baseline justify-between gap-3">
                      <h3 className="text-[15px] font-semibold text-white">{l.name}</h3>
                      <span className="flex-none font-mono text-[10.5px] text-emerald-300/90">{l.ram}</span>
                    </div>
                    <p className="mt-1 text-[12.5px] leading-relaxed text-white/50">{l.desc}</p>
                  </div>
                </div>
              </div>
            </Reveal>
          ))}
          <Reveal delay={480}>
            <div className="flex items-center justify-between rounded-2xl border border-emerald-400/20 bg-emerald-400/[0.06] px-4 py-3">
              <span className="flex items-center gap-2 text-[12.5px] text-emerald-200"><Leaf size={14} /> Whole stack, idle</span>
              <span className="font-mono text-[13px] font-semibold text-emerald-300">384 MB / 7.9 GB</span>
            </div>
          </Reveal>
        </div>

        {/* foundation comparison */}
        <div>
          <Reveal>
            <div className="overflow-hidden rounded-2xl border border-line bg-ink-2">
              <div className="flex items-center gap-2 border-b border-line px-5 py-3.5">
                <HardDriveDownload size={15} className="text-peri" />
                <span className="text-[13px] font-semibold text-white">Foundation bake-off</span>
                <span className="ml-auto font-mono text-[10.5px] text-white/35">measured on the iMac itself</span>
              </div>
              <table className="w-full text-[12.5px]">
                <thead>
                  <tr className="text-left font-mono text-[10px] uppercase tracking-wider text-white/35">
                    <th className="px-5 py-2.5 font-medium">Base</th>
                    <th className="px-2 py-2.5 font-medium">GPU/fw</th>
                    <th className="px-2 py-2.5 font-medium">VA/VDPAU</th>
                    <th className="px-2 py-2.5 font-medium">Pkgs</th>
                    <th className="px-2 py-2.5 font-medium">Idle</th>
                    <th className="px-5 py-2.5 font-medium">Verdict</th>
                  </tr>
                </thead>
                <tbody>
                  {FOUNDATIONS.map((f, i) => {
                    const cell = (v: boolean | string) =>
                      v === true ? <CircleCheck size={14} className="text-emerald-400" /> :
                      v === false ? <CircleMinus size={14} className="text-rose-400/70" /> :
                      <span className="text-white/55">{v}</span>;
                    return (
                      <tr key={i} className={`border-t border-line/60 transition-colors hover:bg-white/[0.03] ${f.win ? "bg-peri/[0.07]" : ""}`}>
                        <td className="px-5 py-3 font-semibold text-white">
                          <span className="flex items-center gap-2">
                            {f.name}
                            {f.win && <span className="rounded-full bg-peri px-2 py-0.5 font-mono text-[9px] uppercase tracking-wider text-ink">chosen</span>}
                          </span>
                        </td>
                        <td className="px-2 py-3">{cell(f.rows[0])}</td>
                        <td className="px-2 py-3">{cell(f.rows[1])}</td>
                        <td className="px-2 py-3">{cell(f.rows[2])}</td>
                        <td className="px-2 py-3 font-mono text-[11.5px] text-white/60">{f.rows[3]}</td>
                        <td className="px-5 py-3 text-white/45">{f.rows[4]}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          </Reveal>
          <Reveal delay={140}>
            <p className="mt-4 text-[13px] leading-relaxed text-white/45">
              Hardware compatibility beat absolute image size. A 612 MB ISO where the GPU, Wi-Fi and fan
              curves all work out of the box is a better OS than a 90 MB image with a black screen.
              Debian's netinst core gives us <span className="text-white/75">firmware blobs, tuned Mesa/VDPAU packages and long-term
              security updates</span> without a single desktop metapackage.
            </p>
          </Reveal>
          <Reveal delay={220}>
            <div className="mt-5 grid grid-cols-3 gap-2.5">
              {[
                [<Wifi size={14} key="w" />, "firmware-brcm80211", "Wi-Fi that just works"],
                [<AudioLines size={14} key="a" />, "ALSA direct", "CS4206, no sound server"],
                [<FolderTree size={14} key="f" />, "ntfs-3g · exfat · hfs+", "every disk you own"],
              ].map(([icon, t, s]) => (
                <div key={t as string} className="rounded-xl border border-line bg-ink-3 p-3">
                  <span className="text-peri">{icon}</span>
                  <div className="mt-2 font-mono text-[11px] text-white">{t}</div>
                  <div className="text-[11px] text-white/40">{s}</div>
                </div>
              ))}
            </div>
          </Reveal>
        </div>
      </div>
    </Section>
  );
}
