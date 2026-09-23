import { useEffect, useRef, useState } from "react";
import { useOS, type AppId } from "../os";

interface Line { text: string; cls?: string; }

const BANNER: Line[] = [
  { text: "G1OS 1.0 — tty1 · mica-wm · Giving life to older machines.", cls: "text-zinc-500" },
  { text: 'Type "help" for commands. Try "media-os-benchmark".', cls: "text-zinc-500" },
];

const HELP = `  help                  this list
  sysinfo               machine summary (alias: neofetch)
  lspci | vdpauinfo    hardware probes
  free -m               memory usage
  uptime                boot + load
  powertop              power estimate
  media-os-benchmark    the full acceptance suite
  open <app>            launch finder|music|settings|photos|player
  clear / date          reset terminal / print time`;

export default function TerminalApp() {
  const os = useOS();
  const [lines, setLines] = useState<Line[]>(BANNER);
  const [input, setInput] = useState("");
  const [hist, setHist] = useState<string[]>([]);
  const [hi, setHi] = useState(-1);
  const [busy, setBusy] = useState(false);
  const bodyRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    bodyRef.current?.scrollTo({ top: 1e6 });
  }, [lines]);

  const print = (l: Line[] | Line) =>
    setLines((ls) => [...ls, ...(Array.isArray(l) ? l : [l])]);

  const echo = (cmd: string) => print({ text: `you@iMac ~ % ${cmd}`, cls: "text-zinc-100" });
  const out = (text: string, cls = "text-zinc-300") => print({ text, cls });

  const stream = (items: [string, string?][], gap = 320) => {
    setBusy(true);
    items.forEach(([text, cls], i) =>
      setTimeout(() => {
        print({ text, cls });
        if (i === items.length - 1) setBusy(false);
      }, 220 + i * gap)
    );
  };

  const exec = (raw: string) => {
    const cmd = raw.trim();
    if (!cmd) return;
    echo(cmd);
    setHist((h) => [cmd, ...h]);
    setHi(-1);
    const [bin, ...args] = cmd.split(/\s+/);
    const arg = args.join(" ");

    switch (bin) {
      case "help": out(HELP); break;
      case "date": out(new Date().toString()); break;
      case "clear":
      case "cleardate": setLines([]); break;
      case "open": {
        const app = arg as AppId;
        if (["finder", "settings", "music", "photos", "player", "calc", "textedit", "sysinfo"].includes(app)) {
          os.openApp(app);
          out(`mica-wm: spawned ${app} (pid ${1400 + Math.floor(Math.random() * 500)})`, "text-emerald-300");
        } else out(`open: no such app: "${arg}"`, "text-rose-300");
        break;
      }
      case "uptime":
        out(" 14:03:11 up 3 days, 2:14,  1 user,  load average: 0.02, 0.04, 0.01");
        break;
      case "free":
        out(`               total        used        free      shared  buff/cache   available
Mem:            7907         384        7026           6         497        7320
Swap:              0           0           0`);
        out("Desktop stack: mica-wm 38 MB · dock 4 MB · panel 6 MB · finder 22 MB", "text-sky-300");
        break;
      case "powertop":
        out(`Power est.   Usage   Device / process
   54 W at wall      iMac total (Snow Leopard: 72 W)
    1.2 W           mica-wm idle loop
    0.0 W           dock (event-driven, no polls)`);
        break;
      case "lspci":
        out(`00:00.0 Host bridge: Intel Core Processor DRAM Controller
03:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] RV730/M96-XT [Mobility Radeon HD 4670]
05:00.0 Ethernet controller: Broadcom Inc. NetXtreme BCM5764M Gigabit Ethernet
06:00.0 Network controller: Broadcom Inc. BCM43224 802.11a/b/g/n
07:00.0 Audio device: Cirrus Logic CS4206 (snd_hda_intel)`);
        break;
      case "vdpauinfo":
        out(`display: :0   screen: 0
API version: 1
Information string: G3DVL VDPAU Driver Shared Library (r600)
Video surface:   name            width height types
-------------------------------------------
Decoder capabilities:  H264_BASELINE H264_MAIN H264_HIGH  ✓
                       MPEG1 MPEG2_SIMPLE MPEG2_MAIN      ✓
                       VC1_SIMPLE VC1_MAIN VC1_ADVANCED   ✓
profile chosen at boot: MULTIMEDIA (hw decode confirmed)`, "text-emerald-300");
        break;
      case "sysinfo":
      case "neofetch":
        out(`        ▄▄▄▄▄         you@iMac
      ▄█▀    ▀█▄       -------------
     █▌  ▄▄▄  ▐█       OS: G1OS 1.0 x86_64
     █▌ ▐▌ ▐▌ ▐█      Host: iMac11,2 (21.5-inch, Mid 2010)
      █▄ ▀▀▀ ▄█       Kernel: linux 6.12.9-maclite (NOHZ_FULL, tickless)
       ▀█▄▄▄█▀        WM: mica-wm 0.4 (DRM/KMS direct, no X11)
        ▐▌ ▐▌        Shell: mksh
     ▄▄▄██▄██▄▄▄     CPU: Intel i3-540 (4) @ 3.067GHz
                      GPU: ATI RV730 [Radeon HD 4670] · Mesa r600 · VDPAU
                      Memory: 384MiB / 7907MiB (4.9%)
                      Boot: 11.2s (5400rpm HDD)`, "text-sky-300");
        break;
      case "media-os-benchmark":
        stream([
          ["┌─ media-os-benchmark ─ iMac11,2 ───────────────", "text-zinc-400"],
          ["Boot time ............... 11.2 s (HDD) · 4.9 s (SSD)", "text-zinc-200"],
          ["Idle RAM ................ 384 MB used / 7.9 GB", "text-zinc-200"],
          ["Idle CPU ................ 0.4 % (42 processes)", "text-zinc-200"],
          ["GPU ..................... RV730 · r600 · VDPAU ✓", "text-emerald-300"],
          ["Video decoder ........... H.264 High — hardware", "text-emerald-300"],
          ["720p .................... 60 fps · 0 dropped", "text-zinc-200"],
          ["1080p ................... 30 fps · 2 dropped / 90 s", "text-zinc-200"],
          ["1080p (VDPAU) ........... locked 24/25/30 · 0 dropped", "text-emerald-300"],
          ["Audio ................... CS4206 · ALSA direct · 0 xruns", "text-zinc-200"],
          ["Disk .................... 92 MB/s seq · 3.1 s copy of 1 GB", "text-zinc-200"],
          ["UI during playback ...... Dock & windows at 60 fps", "text-emerald-300"],
          ["└─ verdict: PASSED — 12 / 12 acceptance criteria", "text-emerald-300"],
        ], 260);
        break;
      case "sudo": out("sudo: this OS has 42 processes and one purpose. You're already root of the vibe.", "text-amber-200"); break;
      case "exit": os.closeWin(os.wins.find((w) => w.app === "terminal")!.id); break;
      default:
        if (bin.startsWith("./")) out(`mksh: ${bin}: Permission denied (noexec on /home)`);
        else out(`mksh: command not found: ${bin}`, "text-rose-300");
    }
  };

  return (
    <div
      className="flex h-full flex-col bg-[#141419]/95 font-mono text-[12.5px] leading-[1.55] text-zinc-300"
      onClick={() => inputRef.current?.focus()}
    >
      <div ref={bodyRef} className="min-h-0 flex-1 overflow-auto px-3 py-2.5">
        {lines.map((l, i) => (
          <pre key={i} className={`whitespace-pre-wrap break-words ${l.cls ?? ""}`}>{l.text}</pre>
        ))}
        <div className="flex">
          <span className="mr-0 flex-none text-emerald-300">you@iMac</span>
          <span className="flex-none text-zinc-500">&nbsp;~ %&nbsp;</span>
          <div className="relative min-w-0 flex-1">
            <span className="whitespace-pre-wrap break-all">{input}</span>
            <span className="caret -mb-0.5 ml-px inline-block h-[1.05em] w-[7px] bg-emerald-300/90 align-middle" />
            <input
              ref={inputRef}
              autoFocus
              value={input}
              disabled={busy}
              onChange={(e) => setInput(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") { exec(input); setInput(""); }
                else if (e.key === "ArrowUp") { e.preventDefault(); const n = Math.min(hi + 1, hist.length - 1); if (hist[n]) { setHi(n); setInput(hist[n]); } }
                else if (e.key === "ArrowDown") { e.preventDefault(); const n = Math.max(hi - 1, -1); setHi(n); setInput(hist[n] ?? ""); }
                else if (e.key === "l" && e.ctrlKey) { e.preventDefault(); setLines([]); }
              }}
              className="absolute inset-0 h-full w-full bg-transparent opacity-0 outline-none"
              spellCheck={false}
            />
          </div>
        </div>
      </div>
      <div className="flex h-[26px] flex-none items-center justify-between border-t border-white/10 px-3 text-[10.5px] text-zinc-500">
        <span>mksh · no shell daemon · 1.9 MB RSS</span>
        <span className="flex items-center gap-1.5">
          <span className={`h-1.5 w-1.5 rounded-full ${busy ? "bg-amber-300" : "bg-emerald-400"}`} style={busy ? { animation: "spin 0.6s linear infinite" } : {}} />
          {busy ? "benchmarking…" : "idle"}
        </span>
      </div>
    </div>
  );
}
