import { useEffect, useRef, useState } from "react";
import Desktop from "./os/Desktop";
import Architecture from "./site/Architecture";
import Performance from "./site/Performance";
import Hardware from "./site/Hardware";
import BuildKit from "./site/BuildKit";
import { LogoMark } from "./os/MenuBar";
import { ChevronDown } from "lucide-react";

const BOOT_LINES = [
  "probing RV730 framebuffer · 1920×1080 native",
  "brcmfmac: BCM43224 firmware loaded",
  "applesmc: 4 fans, 9 thermal zones armed",
  "ext4: clean, 0.02 s recovery scan",
  "vdpau: G3DVL (r600) — hardware decode online",
  "mica-wm 0.4 — compositor ready in 46 MB",
];

export default function App() {
  const [phase, setPhase] = useState<"boot" | "fade" | "live">("boot");
  const [li, setLi] = useState(0);
  const skipped = useRef(false);

  useEffect(() => {
    if (phase !== "boot") return;
    const liT = setInterval(() => setLi((l) => (l + 1) % BOOT_LINES.length), 380);
    const done = setTimeout(() => { if (!skipped.current) setPhase("fade"); }, 2700);
    const skip = () => { skipped.current = true; clearTimeout(done); setPhase("fade"); };
    window.addEventListener("keydown", skip);
    window.addEventListener("pointerdown", skip);
    return () => {
      clearInterval(liT); clearTimeout(done);
      window.removeEventListener("keydown", skip);
      window.removeEventListener("pointerdown", skip);
    };
  }, [phase]);

  useEffect(() => {
    if (phase === "fade") {
      const t = setTimeout(() => setPhase("live"), 900);
      return () => clearTimeout(t);
    }
  }, [phase]);

  return (
    <div id="top" className="min-h-screen bg-ink text-white">
      {/* ---------- HERO : the OS itself ---------- */}
      <header className="relative h-[100svh] w-full">
        <div className="absolute inset-0">
          {phase !== "boot" && <Desktop />}
        </div>

        {/* scroll cue */}
        <a
          href="#manifesto"
          className={`absolute bottom-5 left-5 z-[570] flex items-center gap-2.5 rounded-full bg-black/35 px-4 py-2 text-[11.5px] font-medium text-white/80 backdrop-blur-md transition-all duration-700 hover:bg-black/55 hover:text-white ${phase === "live" ? "opacity-100" : "pointer-events-none opacity-0"}`}
        >
          <span className="relative flex h-2 w-2">
            <span className="absolute h-full w-full animate-ping rounded-full bg-emerald-400/70" />
            <span className="h-2 w-2 rounded-full bg-emerald-400" />
          </span>
          Live demo — the OS boots in your browser
          <ChevronDown size={13} className="animate-bounce" />
        </a>

        {/* boot overlay */}
        {phase !== "live" && (
          <div
            className={`absolute inset-0 z-[700] grid place-items-center bg-[#040406] transition-opacity duration-700 ${phase === "fade" ? "pointer-events-none opacity-0" : "opacity-100"}`}
          >
            <div className="flex w-[300px] flex-col items-center">
              <LogoMark size={74} />
              <div className="mt-5 text-[19px] font-semibold tracking-[0.02em] text-white/90">MacLiteOS</div>
              <div className="mt-1 font-mono text-[10px] uppercase tracking-[0.35em] text-white/30">iMac · Mid 2010</div>
              <div className="mt-7 h-[3px] w-full overflow-hidden rounded-full bg-white/10">
                <div className="h-full rounded-full bg-white animate-[boot-bar_2.1s_cubic-bezier(0.65,0,0.35,1)_both]" />
              </div>
              <div className="mt-3 h-4 overflow-hidden font-mono text-[10.5px] text-emerald-300/80">
                <span className="caret mr-1 inline-block h-[10px] w-[5px] translate-y-[1px] bg-emerald-300/60" />
                {BOOT_LINES[li]}
              </div>
              <div className="mt-8 font-mono text-[10px] text-white/25">press any key to skip</div>
            </div>
          </div>
        )}
      </header>

      {/* ---------- MANIFESTO ---------- */}
      <section id="manifesto" className="relative overflow-hidden">
        <div className="blueprint pointer-events-none absolute inset-0" />
        <div className="pointer-events-none absolute -top-40 left-1/2 h-[560px] w-[900px] -translate-x-1/2 rounded-full bg-peri/[0.07] blur-[120px]" />
        <div className="relative mx-auto max-w-6xl px-6 py-28 md:py-40">
          <div className="mb-4 flex items-center gap-3">
            <span className="font-mono text-[11px] uppercase tracking-[0.3em] text-peri/90">MacLiteOS 0.4.2</span>
            <span className="h-px w-16 bg-gradient-to-r from-peri/60 to-transparent" />
          </div>
          <h1 className="track-mega max-w-5xl text-[13vw] font-black leading-[0.95] text-white sm:text-7xl md:text-[92px]">
            A second life for the <span className="bg-gradient-to-r from-peri via-white/90 to-ember bg-clip-text text-transparent">2010 iMac</span>.
          </h1>
          <p className="mt-8 max-w-2xl text-[16px] leading-relaxed text-white/55 md:text-lg">
            Not a Linux desktop wearing a costume. A purpose-built operating system that happens to use Linux —
            engineered so a fifteen-year-old machine boots in <b className="text-white">11 seconds</b>, idles on{" "}
            <b className="text-white">384 MB</b>, and plays 1080p video on its GPU like it was born to.
            Everything you just used above is the real design: the Dock, the menu bar, Finder, the perf switch.
          </p>
          <div className="mt-10 grid max-w-3xl grid-cols-2 gap-px overflow-hidden rounded-2xl border border-line bg-line sm:grid-cols-4">
            {[
              ["612 MB", "bootable ISO"],
              ["42", "processes, total"],
              ["0", "background daemons"],
              ["100%", "native code"],
            ].map(([v, l]) => (
              <div key={l} className="bg-ink-2 px-5 py-4">
                <div className="text-xl font-bold text-white">{v}</div>
                <div className="mt-0.5 text-[11px] uppercase tracking-wider text-white/35">{l}</div>
              </div>
            ))}
          </div>
        </div>
      </section>

      <Architecture />
      <div className="h-px bg-gradient-to-r from-transparent via-line to-transparent" />
      <Performance />
      <div className="h-px bg-gradient-to-r from-transparent via-line to-transparent" />
      <Hardware />
      <div className="h-px bg-gradient-to-r from-transparent via-line to-transparent" />
      <BuildKit />
    </div>
  );
}
