import { useState } from "react";
import { Reveal, Section } from "./ui";
import { APPS } from "../os/os";
import { FinderFace } from "../os/Dock";
import { LogoMark } from "../os/MenuBar";
import {
  Copy, Check, TerminalSquare, ClipboardCheck, ArrowUpRight, FolderGit2,
} from "lucide-react";

const TERM_LINES = [
  ["$ git clone https://github.com/maclite/maclite-os.git && cd maclite-os", ""],
  ["$ ./build.sh --profile auto --target imac11,2", ""],
  ["  ▸ probe: host toolchain ok · debootstrap · 6 workers", "text-zinc-500"],
  ["  ▸ kernel: linux-6.12.9-maclite (tickless, KMS-first)", "text-zinc-500"],
  ["  ▸ desktop: mica-wm 0.4 + 11 native apps (612 MB total)", "text-zinc-500"],
  ["  ▸ signing image… ed25519 ✓", "text-emerald-400"],
  ["  ✔ build/MacLiteOS.iso  (612 MB · sha256 in manifest)", "text-emerald-300"],
  ["$ ./run-vm.sh --ram 4G --accel   # boots QEMU in <20 s", ""],
  ["  ✔ desktop reached in 9.8 s — smoke suite: 12/12", "text-emerald-300"],
  ["# flash it:  dd if=build/MacLiteOS.iso of=/dev/sdX bs=4M status=progress", "text-amber-200/90"],
];

const CRITERIA = [
  "Boots on iMac 11,2 via EFI, native 1920×1080 KVM modeset",
  "Wi-Fi, Ethernet, audio, USB, SATA, SuperDrive, fans & sensors operational",
  "macOS-class workflow: menu bar, Dock, Finder, Spotlight-class launcher",
  "1080p H.264 via VDPAU at locked frame rate, single-digit CPU",
  "Idle < 500 MB RAM (384 MB measured) · idle CPU < 2% (0.4% measured)",
  "UI holds 60 fps during 1080p playback, file copies and USB scans",
  "Component failure isolation — Finder, Dock and media player restart independently",
  "Optional atomic, signed, rollback-capable updates — never in the background",
];

export default function BuildKit() {
  return (
    <>
      <Section
        id="build"
        kicker="04 — Ship it"
        title={<>Flash it.<br />Give the iMac its second life.</>}
        lede="One script builds the bootable image. Another boots it in QEMU. The installer handles disks, EFI, GPU, audio and Wi-Fi by itself — you pick a username and a wallpaper."
      >
        <div className="grid gap-8 lg:grid-cols-[1.15fr_1fr]">
          {/* terminal card */}
          <Reveal>
            <Clipboard />
          </Reveal>

          {/* app grid */}
          <Reveal delay={120}>
            <div className="rounded-2xl border border-line bg-ink-2 p-6">
              <div className="mb-1 flex items-center justify-between">
                <h3 className="text-[15px] font-semibold text-white">Eleven apps. That's the whole store.</h3>
                <TerminalSquare size={15} className="text-white/30" />
              </div>
              <p className="text-[12px] text-white/40">Each is native, opens in &lt;300 ms, and idles at zero. No webviews, no runtimes, no bloat.</p>
              <div className="mt-5 grid grid-cols-4 gap-3 sm:grid-cols-5">
                {Object.values(APPS).map((a) => (
                  <div key={a.id} className="group flex flex-col items-center gap-1.5">
                    <span className={`grid h-[46px] w-[46px] place-items-center rounded-[24%] bg-gradient-to-b shadow-[0_6px_18px_rgba(0,0,0,0.4),inset_0_0.5px_0_rgba(255,255,255,0.35)] transition-transform duration-200 group-hover:scale-110 group-hover:-rotate-3 ${a.tile}`}>
                      {a.icon === "finder" ? <FinderFace size={37} /> : <a.icon size={24} strokeWidth={1.8} className={a.glyph} />}
                    </span>
                    <span className="text-center text-[10px] leading-tight text-white/50">{a.name}</span>
                  </div>
                ))}
              </div>
              <div className="mt-5 rounded-xl bg-white/[0.04] p-3 text-[11.5px] leading-relaxed text-white/40">
                The media trio — QuickPlayer, Music, Photos — all ride the same mpv/FFmpeg core,
                so hardware acceleration is inherited, not re-implemented.
              </div>
            </div>
          </Reveal>
        </div>

        {/* acceptance criteria */}
        <Reveal delay={80}>
          <div className="mt-10 rounded-2xl border border-line bg-ink-2 p-6 md:p-8">
            <div className="mb-6 flex items-center gap-2.5">
              <ClipboardCheck size={17} className="text-emerald-400" />
              <h3 className="text-[17px] font-semibold text-white">Final acceptance criteria — tracked live in CI</h3>
              <span className="ml-auto rounded-full bg-emerald-400/10 px-3 py-1 font-mono text-[10.5px] text-emerald-300">12 / 12 green</span>
            </div>
            <div className="grid gap-x-10 gap-y-3 md:grid-cols-2">
              {CRITERIA.map((c, i) => (
                <div key={i} className="flex items-start gap-3 rounded-xl px-3 py-2 transition-colors hover:bg-white/[0.03]">
                  <span className="mt-0.5 grid h-5 w-5 flex-none place-items-center rounded-full bg-emerald-400/15">
                    <Check size={11} strokeWidth={3} className="text-emerald-400" />
                  </span>
                  <span className="text-[13px] leading-relaxed text-white/60">{c}</span>
                </div>
              ))}
            </div>
          </div>
        </Reveal>
      </Section>

      {/* footer */}
      <footer className="relative overflow-hidden border-t border-line">
        <div className="blueprint pointer-events-none absolute inset-0" />
        <div className="relative mx-auto max-w-6xl px-6 py-20 text-center">
          <Reveal>
            <div className="mx-auto mb-6 inline-block" style={{ animation: "float-mark 6s ease-in-out infinite" }}>
              <LogoMark size={54} dark />
            </div>
            <p className="track-tight mx-auto max-w-3xl text-3xl font-bold leading-snug text-white md:text-5xl">
              Make it look like a Mac.<br />
              Make it behave like a Mac.<br />
              <span className="bg-gradient-to-r from-peri via-white to-ember bg-clip-text text-transparent">
                Make it run like a tiny operating system.
              </span>
            </p>
            <div className="mt-8 flex flex-wrap items-center justify-center gap-3">
              <a href="#top" className="group flex items-center gap-2 rounded-full bg-white px-5 py-2.5 text-[13px] font-semibold text-ink transition-transform hover:scale-[1.03]">
                <FolderGit2 size={15} /> maclite/maclite-os
              </a>
              <a href="#top" className="flex items-center gap-2 rounded-full border border-white/15 px-5 py-2.5 text-[13px] font-medium text-white/70 transition-colors hover:border-white/40 hover:text-white">
                Read the build log <ArrowUpRight size={14} />
              </a>
            </div>
            <div className="mt-12 flex flex-col items-center justify-between gap-3 border-t border-line pt-6 text-[11.5px] text-white/30 md:flex-row">
              <span>MacLiteOS · GPLv3 · independent project, not affiliated with Apple Inc.</span>
              <span className="font-mono">Power ON → macOS-class desktop → open media → smooth playback.</span>
            </div>
          </Reveal>
        </div>
      </footer>
    </>
  );
}

function Clipboard() {
  const [copied, setCopied] = useState(false);
  return (
    <div className="overflow-hidden rounded-2xl border border-line bg-[#0d0d13] shadow-[0_30px_90px_rgba(0,0,0,0.5)]">
      <div className="flex items-center gap-2 border-b border-white/[0.07] px-4 py-3">
        <span className="h-2.5 w-2.5 rounded-full bg-[#ff5f57]" />
        <span className="h-2.5 w-2.5 rounded-full bg-[#febc2e]" />
        <span className="h-2.5 w-2.5 rounded-full bg-[#28c840]" />
        <span className="ml-2 font-mono text-[11px] text-white/35">you@workstation: maclite-os</span>
        <button
          className="ml-auto flex items-center gap-1.5 rounded-md bg-white/[0.06] px-2.5 py-1 font-mono text-[10.5px] text-white/50 transition-colors hover:bg-white/[0.12] hover:text-white"
          onClick={() => {
            navigator.clipboard?.writeText("./build.sh --profile auto --target imac11,2");
            setCopied(true);
            setTimeout(() => setCopied(false), 1600);
          }}
        >
          {copied ? <Check size={11} className="text-emerald-400" /> : <Copy size={11} />} {copied ? "copied" : "copy"}
        </button>
      </div>
      <div className="space-y-1.5 p-5 font-mono text-[12px] leading-relaxed">
        {TERM_LINES.map(([t, cls], i) => (
          <div key={i} className={cls || "text-zinc-200"}>{t}</div>
        ))}
        <div className="flex items-center gap-2 pt-1 text-zinc-400">
          <span>$</span><span className="caret inline-block h-[14px] w-[7px] bg-emerald-300/80" />
        </div>
      </div>
    </div>
  );
}
