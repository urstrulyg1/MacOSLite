import { useEffect, useRef, useState } from "react";
import {
  Play, Pause, SkipBack, SkipForward, Volume2, Cpu, MemoryStick, HardDrive,
  Wifi, AudioLines, MonitorPlay, ShieldCheck, ZoomIn, ZoomOut, Maximize,
} from "lucide-react";
import { VIDEO_SRC } from "../os";

/* ---------------------------------------------------------------- */
export function TextEditApp({ payload }: { payload?: string }) {
  return (
    <div className="flex h-full flex-col bg-white/90">
      <div className="flex h-[34px] flex-none items-center justify-between border-b border-black/10 px-3 text-[11.5px] text-black/45">
        <span>{payload ?? "design-philosophy.md"}</span>
        <span>Plain text · 1.9 MB RSS · autosaved</span>
      </div>
      <textarea
        className="min-h-0 flex-1 resize-none bg-transparent p-4 font-mono text-[12.5px] leading-relaxed text-zinc-800 outline-none"
        defaultValue={`# Design Philosophy — MacLiteOS

Make it look like a Mac.
Make it behave like a Mac.
Make it run like a tiny operating system.

Every component must justify its existence:
  does it improve the Mac-like experience,
  hardware compatibility, multimedia, or
  system management? If not — remove it.

   macOS-like experience
           ▲
     lightweight UI
           ▲
    minimal services
           ▲
  optimized multimedia
           ▲
  hardware-tuned kernel
           ▲
      iMac 2010`}
        spellCheck={false}
      />
    </div>
  );
}

/* ---------------------------------------------------------------- */
export function CalcApp() {
  const [disp, setDisp] = useState("0");
  const [acc, setAcc] = useState<number | null>(null);
  const [op, setOp] = useState<string | null>(null);
  const [fresh, setFresh] = useState(true);

  const num = (n: string) => {
    if (fresh) { setDisp(n === "." ? "0." : n); setFresh(false); }
    else setDisp((d) => (d.replace("-", "").length > 11 ? d : d === "0" && n !== "." ? n : d + n));
  };
  const apply = (a: number, b: number, o: string) =>
    o === "+" ? a + b : o === "−" ? a - b : o === "×" ? a * b : o === "÷" ? (b === 0 ? NaN : a / b) : b;
  const pressOp = (o: string) => {
    const cur = parseFloat(disp);
    if (acc != null && op && !fresh) {
      const r = apply(acc, cur, op);
      setAcc(r); setDisp(fmt(r));
    } else setAcc(cur);
    setOp(o); setFresh(true);
  };
  const eq = () => {
    if (acc == null || !op) return;
    const r = apply(acc, parseFloat(disp), op);
    setDisp(fmt(isNaN(r) ? 0 : r)); setAcc(null); setOp(null); setFresh(true);
  };
  const fmt = (n: number) => {
    const s = Math.abs(n) >= 1e12 || (Math.abs(n) < 1e-7 && n !== 0) ? n.toExponential(6) : String(Math.round(n * 1e9) / 1e9);
    return s;
  };

  const keys: (string | [string, string])[] = [
    ["AC", "fn"], ["±", "fn"], ["%", "fn"], ["÷", "op"],
    "7", "8", "9", ["×", "op"],
    "4", "5", "6", ["−", "op"],
    "1", "2", "3", ["+", "op"],
    ["0", "zero"], ".", ["=", "eq"],
  ];

  const press = (k: string) => {
    if (/\d|\./.test(k)) num(k);
    else if (k === "AC") { setDisp("0"); setAcc(null); setOp(null); setFresh(true); }
    else if (k === "±") setDisp((d) => (d.startsWith("-") ? d.slice(1) : "-" + d));
    else if (k === "%") setDisp((d) => fmt(parseFloat(d) / 100));
    else if (k === "=") eq();
    else pressOp(k);
  };

  return (
    <div className="flex h-full flex-col bg-[#1c1c1e]">
      <div className="flex flex-none items-end justify-end px-4 pb-1 pt-6 text-[44px] font-light tabular-nums text-white">
        {disp}
      </div>
      <div className="grid min-h-0 flex-1 grid-cols-4 gap-px bg-black/40 p-px">
        {keys.map((k, i) => {
          const [label, kind] = Array.isArray(k) ? k : [k, ""];
          return (
            <button
              key={i}
              onClick={() => press(label)}
              className={`text-[19px] font-light transition-colors active:brightness-125 ${
                kind === "zero" ? "col-span-2 bg-[#505050] text-white" :
                kind === "op" ? "bg-[#ff9f0a] text-2xl text-white" :
                kind === "fn" ? "bg-[#a5a5a5] text-black" :
                kind === "eq" ? "bg-[#ff9f0a] text-2xl text-white" : "bg-[#333333] text-white"
              }`}
            >
              {label}
            </button>
          );
        })}
      </div>
    </div>
  );
}

/* ---------------------------------------------------------------- */
export function PhotosApp({ payload }: { payload?: string }) {
  const [zoom, setZoom] = useState(1);
  return (
    <div className="flex h-full flex-col bg-[#1e1e20]">
      <div className="flex h-[40px] flex-none items-center justify-between border-b border-white/10 px-3 text-white/80">
        <span className="text-[12px]">{payload ?? "valley-sunrise.jpg"}</span>
        <div className="flex items-center gap-1">
          <button className="rounded-md p-1.5 hover:bg-white/10" onClick={() => setZoom((z) => Math.max(0.5, z - 0.25))}><ZoomOut size={15} /></button>
          <span className="w-12 text-center text-[11px] tabular-nums text-white/50">{Math.round(zoom * 100)}%</span>
          <button className="rounded-md p-1.5 hover:bg-white/10" onClick={() => setZoom((z) => Math.min(3, z + 0.25))}><ZoomIn size={15} /></button>
          <button className="rounded-md p-1.5 hover:bg-white/10" onClick={() => setZoom(1)}><Maximize size={14} /></button>
        </div>
      </div>
      <div className="grid min-h-0 flex-1 place-items-center overflow-auto p-4">
        <img
          src="./photo.jpg"
          alt="Valley sunrise"
          draggable={false}
          className="max-h-full max-w-full rounded-md shadow-[0_12px_50px_rgba(0,0,0,0.6)]"
          style={{ transform: `scale(${zoom})`, transition: "transform 0.25s cubic-bezier(0.2,0.8,0.3,1)" }}
        />
      </div>
      <div className="flex h-[30px] flex-none items-center justify-between border-t border-white/10 px-3 text-[11px] text-white/45">
        <span>4288 × 2848 · 6.1 MB · JPEG</span>
        <span>Decoded on demand — no photo library database</span>
      </div>
    </div>
  );
}

/* ---------------------------------------------------------------- */
const TRACKS = [
  { title: "Morning Bloom", artist: "Ember Coastlines", time: "3:42", fmt: "FLAC · 24-bit / 96 kHz · 4 608 kbps" },
  { title: "Granite Skyline", artist: "Ember Coastlines", time: "4:15", fmt: "Opus · 160 kbps" },
  { title: "Low Sun", artist: "Ember Coastlines", time: "5:03", fmt: "ALAC · lossless" },
];

export function MusicApp({ payload }: { payload?: string }) {
  const [t, setT] = useState(payload?.includes("granite") ? 1 : payload?.includes("sun") ? 2 : 0);
  const [playing, setPlaying] = useState(true);
  const [pos, setPos] = useState(0.32);
  useEffect(() => {
    if (!playing) return;
    const id = setInterval(() => setPos((p) => (p + 0.006) % 1), 400);
    return () => clearInterval(id);
  }, [playing]);
  const tr = TRACKS[t];

  return (
    <div className="flex h-full flex-col bg-gradient-to-b from-[#2b2130] to-[#191521] p-5 text-white">
      <div className="flex min-h-0 flex-1 items-center gap-5">
        <div className="relative flex-none">
          <img src="./album.jpg" alt="album art" draggable={false} className="h-36 w-36 rounded-xl object-cover shadow-[0_16px_50px_rgba(0,0,0,0.55)]" />
          <div className="absolute -bottom-2 -right-2 flex h-9 items-end gap-[3px] rounded-lg bg-black/55 px-2 py-1.5 backdrop-blur">
            {[0.9, 0.5, 1.2, 0.7, 1.0].map((d, i) => (
              <span key={i} className="eqbar w-[3px] rounded-full bg-rose-300" style={{ height: 16, animationDelay: `${i * 0.13}s`, animationDuration: `${d}s`, opacity: playing ? 1 : 0.35 }} />
            ))}
          </div>
        </div>
        <div className="min-w-0 flex-1">
          <div className="text-[10.5px] font-semibold uppercase tracking-[0.14em] text-rose-300/90">Now Playing · gapless</div>
          <div className="mt-1 truncate text-[22px] font-bold tracking-tight">{tr.title}</div>
          <div className="truncate text-[13.5px] text-white/55">{tr.artist}</div>
          <div className="mt-1.5 inline-flex rounded-md bg-white/10 px-2 py-0.5 font-mono text-[10.5px] text-white/70">{tr.fmt}</div>
          <div className="mt-4">
            <div className="h-1 overflow-hidden rounded-full bg-white/15">
              <div className="h-full rounded-full bg-rose-300 transition-[width] duration-300" style={{ width: `${pos * 100}%` }} />
            </div>
            <div className="mt-1 flex justify-between text-[10.5px] tabular-nums text-white/45">
              <span>{fmtTime(pos, tr.time)}</span><span>{tr.time}</span>
            </div>
          </div>
        </div>
      </div>
      <div className="flex flex-none items-center justify-between pt-2">
        <Volume2 size={15} className="text-white/50" />
        <div className="flex items-center gap-5">
          <button className="text-white/70 hover:text-white" onClick={() => { setT((t + TRACKS.length - 1) % TRACKS.length); setPos(0); }}><SkipBack size={19} fill="currentColor" /></button>
          <button
            className="grid h-11 w-11 place-items-center rounded-full bg-white text-[#2b2130] shadow-lg transition-transform hover:scale-105 active:scale-95"
            onClick={() => setPlaying(!playing)}
          >
            {playing ? <Pause size={19} fill="currentColor" /> : <Play size={19} fill="currentColor" className="ml-0.5" />}
          </button>
          <button className="text-white/70 hover:text-white" onClick={() => { setT((t + 1) % TRACKS.length); setPos(0); }}><SkipForward size={19} fill="currentColor" /></button>
        </div>
        <span className="font-mono text-[10px] text-white/35">mpv · 6 MB RSS</span>
      </div>
    </div>
  );
}
const fmtTime = (pos: number, total: string) => {
  const [m, s] = total.split(":").map(Number);
  const cur = Math.floor(((m * 60 + s) * pos));
  return `${Math.floor(cur / 60)}:${String(cur % 60).padStart(2, "0")}`;
};

/* ---------------------------------------------------------------- */
export function PlayerApp({ payload }: { payload?: string }) {
  const wrapRef = useRef<HTMLDivElement>(null);
  return (
    <div className="relative flex h-full flex-col bg-black" ref={wrapRef}>
      <video
        src={VIDEO_SRC}
        className="min-h-0 w-full flex-1 object-contain"
        autoPlay muted loop playsInline controls
      />
      <div className="pointer-events-none absolute left-3 top-3 flex items-center gap-2">
        <span className="pointer-events-auto rounded-md bg-black/60 px-2 py-1 text-[11px] font-medium text-white backdrop-blur">
          {payload ?? "reef-drift-1080p.mp4"}
        </span>
        <span className="pointer-events-auto rounded-md bg-emerald-400/90 px-2 py-1 text-[10.5px] font-bold text-emerald-950">
          VDPAU · H.264 HW decode
        </span>
      </div>
      <button
        onClick={() => {
          const el = wrapRef.current;
          if (!el) return;
          if (document.fullscreenElement) document.exitFullscreen();
          else el.requestFullscreen?.();
        }}
        className="absolute right-3 top-3 rounded-md bg-black/60 px-2 py-1 text-[11px] text-white/85 backdrop-blur hover:bg-black/80"
      >
        Full Screen
      </button>
      <div className="absolute bottom-3 left-3 rounded-md bg-black/50 px-2 py-1 font-mono text-[10px] text-emerald-300">
        decode 4–9% CPU · UI clock stable at 60 fps
      </div>
    </div>
  );
}

/* ---------------------------------------------------------------- */
export function SysInfoApp() {
  const rows: [string, React.ReactNode, string][] = [
    ["OS Branding", <LogoMark size={16} />, "G1OS 1.0 · Giving life to older machines"],
    ["CPU", <Cpu size={16} />, "Intel Core i3-540 · 3.06 GHz · 2C/4T · SSE4.2 (Clarkdale 32nm)"],
    ["GPU", <MonitorPlay size={16} />, "ATI Radeon HD 4670 (RV730) · 256 MB GDDR3 · Mesa r600 · DRM/KMS native"],
    ["RAM", <MemoryStick size={16} />, "4 GB DDR3-1333 · 384 MB in use (9.6%)"],
    ["Storage", <HardDrive size={16} />, "Crucial CT500MX500SSD1 · 500.1 GB SATA · ext4 (noatime)"],
    ["Wi-Fi", <Wifi size={16} />, "Broadcom BCM43224 · AirPort Extreme 802.11n · 130 Mb/s"],
    ["Audio", <AudioLines size={16} />, "Cirrus CS4206 · ALSA direct — zero sound-server overhead"],
    ["Video acceleration", <ShieldCheck size={16} />, "VDPAU ✓ — 1080p 60fps hardware decode online"],
  ];
  return (
    <div className="flex h-full flex-col overflow-auto bg-[rgba(250,250,252,0.92)] p-6 select-none text-zinc-900">
      <div className="mb-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-blue-600">Operating System</div>
      <h2 className="text-[22px] font-bold tracking-tight text-black">G1OS for iMac (Mid-2010)</h2>
      <div className="mb-4 text-[12.5px] text-blue-600 font-medium">“Giving life to older machines.”</div>
      <div className="space-y-2">
        {rows.map(([k, icon, v]) => (
          <div key={k} className="flex items-center gap-3 rounded-xl border border-black/[0.07] bg-white px-3.5 py-2.5 shadow-[0_1px_2px_rgba(0,0,0,0.04)]">
            <span className="grid h-8 w-8 flex-none place-items-center rounded-lg bg-blue-500/15 text-blue-600">{icon}</span>
            <div className="min-w-0">
              <div className="text-[10.5px] font-semibold uppercase tracking-wide text-black/45">{k}</div>
              <div className="truncate text-[12.5px] text-black/85 font-mono">{v}</div>
            </div>
          </div>
        ))}
      </div>
      <div className="mt-4 rounded-xl bg-emerald-500/10 border border-emerald-500/20 p-3 text-[12px] leading-relaxed text-emerald-900">
        <b>Hardware verification passed:</b> 8/8 subsystems verified. 1080p H.264 streams with single-digit CPU usage via hardware VDPAU. Dual-tier Apple EFI fallback ensures independent internal booting.
      </div>
    </div>
  );
}
