import { useEffect, useRef, useState } from "react";
import Desktop from "./os/Desktop";
import Architecture from "./site/Architecture";
import Performance from "./site/Performance";
import Hardware from "./site/Hardware";
import BuildKit from "./site/BuildKit";
import { LogoMark } from "./os/MenuBar";
import { ChevronDown, ArrowRight, User, Power, RotateCcw, Moon, Sparkles } from "lucide-react";

const BOOT_LINES = [
  "Apple EFI 1.10 · 64-bit fallback loader verified",
  "probing RV730 framebuffer · 1920×1080 native modeset",
  "brcmfmac: BCM43224 AirPort Extreme loaded",
  "applesmc: 3 fans, 9 thermal zones armed (quiet mode)",
  "vdpau: G3DVL (r600) — 1080p hardware decode online",
  "ext4: root=UUID=78FA-C9B2 mounted clean (0.02s)",
  "G1OS compositor ready in 38 MB",
];

export default function App() {
  const [bootPhase, setBootPhase] = useState<"jeevan" | "g1os" | "desktop">("jeevan");
  const [bootLineIdx, setBootLineIdx] = useState(0);
  const skipped = useRef(false);

  // Boot sequence timer
  useEffect(() => {
    // 1. Show JeevanOS for 2 seconds, then smoothly shrink and morph to G1OS
    const t1 = setTimeout(() => {
      if (!skipped.current) setBootPhase("g1os");
    }, 2000);

    // 2. Cycle hardware lines
    const lineTimer = setInterval(() => {
      setBootLineIdx((i) => (i + 1) % BOOT_LINES.length);
    }, 450);

    // 3. Move directly to G1OS desktop after 3.6s
    const t2 = setTimeout(() => {
      if (!skipped.current) setBootPhase("desktop");
    }, 3600);

    const handleSkip = () => {
      if (!skipped.current) {
        skipped.current = true;
        setBootPhase("desktop");
      }
    };

    window.addEventListener("keydown", handleSkip);
    window.addEventListener("pointerdown", handleSkip);

    return () => {
      clearTimeout(t1);
      clearTimeout(t2);
      clearInterval(lineTimer);
      window.removeEventListener("keydown", handleSkip);
      window.removeEventListener("pointerdown", handleSkip);
    };
  }, []);

  const isG1OS = bootPhase === "g1os";

  return (
    <div id="top" className="min-h-screen bg-ink text-white">
      {/* ---------- HERO : the OS itself ---------- */}
      <header className="relative h-[100svh] w-full overflow-hidden">
        {/* Desktop Container (always active and ready) */}
        <div className="absolute inset-0">
          <Desktop />
        </div>

        {/* Scroll cue */}
        <a
          href="#manifesto"
          className="absolute bottom-4 left-5 z-[570] flex items-center gap-2 rounded-full bg-black/40 px-3.5 py-1.5 text-[11px] font-medium text-white/80 backdrop-blur-md transition-all duration-700 hover:bg-black/60 hover:text-white"
        >
          <span className="relative flex h-2 w-2">
            <span className="absolute h-full w-full animate-ping rounded-full bg-emerald-400/70" />
            <span className="h-2 w-2 rounded-full bg-emerald-400" />
          </span>
          Live demo — G1OS on iMac 2010
          <ChevronDown size={12} className="animate-bounce" />
        </a>

        {/* ---------------- 1. BOOT SEQUENCE: JeevanOS -> G1OS ---------------- */}
        {(bootPhase === "jeevan" || bootPhase === "g1os") && (
          <div className="absolute inset-0 z-[700] grid place-items-center bg-[#050608] select-none">
            <div className="flex w-[340px] flex-col items-center text-center animate-in fade-in duration-500">
              <div className="relative mb-6">
                <LogoMark size={78} />
                <span className="absolute -bottom-1 -right-1 flex h-4 w-4">
                  <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-blue-400 opacity-75" />
                  <span className="relative inline-flex rounded-full h-4 w-4 bg-blue-500" />
                </span>
              </div>

              {/* Seamless Shrink & Morph Brand Transition: JeevanOS -> G1OS */}
              <div className="relative h-16 w-full flex items-center justify-center">
                {/* 1. JeevanOS: Stays full for 2.0s, then shrinks inwards tightly to 0.35 with letter compression */}
                <div
                  className="absolute inset-0 flex items-center justify-center"
                  style={{
                    opacity: isG1OS ? 0 : 1,
                    transform: isG1OS ? "scale(0.35) translateY(-2px)" : "scale(1) translateY(0)",
                    letterSpacing: isG1OS ? "-0.08em" : "-0.01em",
                    filter: isG1OS ? "blur(5px)" : "blur(0px)",
                    transition:
                      "transform 1000ms cubic-bezier(0.25, 1, 0.4, 1), opacity 850ms ease-out, filter 900ms ease-out, letter-spacing 1000ms cubic-bezier(0.25, 1, 0.4, 1)",
                    pointerEvents: "none",
                  }}
                >
                  <h1 className="text-[32px] font-bold tracking-tight text-white/95 drop-shadow-[0_2px_12px_rgba(255,255,255,0.15)]">
                    JeevanOS
                  </h1>
                </div>

                {/* 2. G1OS: Emerges right from the shrinking epicenter, expands to scale 1.0 with luminous glow */}
                <div
                  className="absolute inset-0 flex flex-col items-center justify-center"
                  style={{
                    opacity: isG1OS ? 1 : 0,
                    transform: isG1OS ? "scale(1) translateY(0)" : "scale(0.5) translateY(4px)",
                    filter: isG1OS ? "blur(0px)" : "blur(6px)",
                    transition:
                      "transform 1000ms cubic-bezier(0.2, 0.9, 0.3, 1), opacity 800ms ease-out, filter 800ms ease-out",
                    pointerEvents: "none",
                  }}
                >
                  <h1 className="text-[32px] font-black tracking-wide text-transparent bg-clip-text bg-gradient-to-r from-blue-400 via-sky-200 to-indigo-300 drop-shadow-[0_0_24px_rgba(56,189,248,0.45)]">
                    G1OS
                  </h1>
                  <p
                    className="text-[12px] font-medium tracking-wide text-blue-300/90 transition-all duration-700"
                    style={{
                      opacity: isG1OS ? 1 : 0,
                      transform: isG1OS ? "translateY(0)" : "translateY(6px)",
                      transitionDelay: isG1OS ? "280ms" : "0ms",
                    }}
                  >
                    “Giving life to older machines.”
                  </p>
                </div>
              </div>

              <div className="mt-2 font-mono text-[10px] uppercase tracking-[0.35em] text-white/30">
                iMac Mid-2010 · A1311 / A1312
              </div>

              {/* Smooth Progress Bar */}
              <div className="mt-8 h-[3.5px] w-full overflow-hidden rounded-full bg-white/10 p-0.5">
                <div
                  className="h-full rounded-full bg-gradient-to-r from-blue-500 to-sky-300 transition-all duration-1200 ease-out"
                  style={{
                    width: isG1OS ? "96%" : "28%",
                  }}
                />
              </div>

              {/* Live Hardware Probes status text */}
              <div className="mt-3.5 h-4 overflow-hidden font-mono text-[10.5px] text-emerald-400/90">
                {isG1OS ? (
                  <>
                    <span className="caret mr-1 inline-block h-[9px] w-[4px] translate-y-[1px] bg-emerald-400" />
                    {BOOT_LINES[bootLineIdx]}
                  </>
                ) : (
                  <span className="text-white/40">Initializing Apple EFI firmware...</span>
                )}
              </div>

              <div className="mt-8 font-mono text-[10px] text-white/20">press any key or click to skip</div>
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
            <span className="font-mono text-[11px] uppercase tracking-[0.3em] text-peri/90">G1OS 1.0</span>
            <span className="h-px w-16 bg-gradient-to-r from-peri/60 to-transparent" />
          </div>
          <h1 className="track-mega max-w-5xl text-[13vw] font-black leading-[0.95] text-white sm:text-7xl md:text-[92px]">
            Giving life to <span className="bg-gradient-to-r from-peri via-white/90 to-ember bg-clip-text text-transparent">older machines</span>.
          </h1>
          <p className="mt-8 max-w-2xl text-[16px] leading-relaxed text-white/55 md:text-lg">
            Not a generic Linux desktop wearing a costume. G1OS is a purpose-built, classic Mac-inspired operating system —
            engineered so a 2010 iMac boots in <b className="text-white">11 seconds</b>, idles on{" "}
            <b className="text-white">384 MB</b>, and streams 1080p video on its GPU with complete fluidity.
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
