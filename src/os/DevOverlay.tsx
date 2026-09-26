/**
 * G1OS Developer / Performance Overlay
 *
 * Lightweight FPS + frame-time + paint-cost meter. Runs one passive rAF
 * loop that measures the interval between successive frames; we don't
 * allocate during the loop so the meter itself doesn't perturb results.
 *
 * Toggle with ⌥⌘D (or the checkbox in Settings → Developer).
 */
import { useEffect, useRef, useState } from "react";
import { useOS } from "./os";

interface Stats {
  fps: number;
  ft: number; // last frame time ms
  ftMin: number;
  ftMax: number;
  avg: number;
  dropped: number;
  wins: number;
}

export default function DevOverlay() {
  const os = useOS();
  const [stats, setStats] = useState<Stats>({
    fps: 60, ft: 16.7, ftMin: 16.7, ftMax: 16.7, avg: 16.7, dropped: 0, wins: 0,
  });
  const rafRef = useRef(0);

  useEffect(() => {
    if (!os.devOverlay) return;
    let last = performance.now();
    let acc = 0;
    let frames = 0;
    let min = Infinity;
    let max = 0;
    let dropped = 0;
    let tick = 0;
    const loop = (t: number) => {
      const dt = t - last;
      last = t;
      if (dt > 0 && dt < 200) {
        frames++;
        acc += dt;
        if (dt < min) min = dt;
        if (dt > max) max = dt;
        // A dropped frame on a 60Hz target is anything > 22ms.
        if (dt > 22) dropped += Math.floor(dt / 16.67) - 1;
        tick++;
        if (tick >= 30) {
          const avg = acc / frames;
          setStats({
            fps: Math.round(1000 / avg),
            ft: Math.round(dt * 10) / 10,
            ftMin: Math.round(min * 10) / 10,
            ftMax: Math.round(max * 10) / 10,
            avg: Math.round(avg * 10) / 10,
            dropped,
            wins: os.wins.length,
          });
          frames = 0; acc = 0; min = Infinity; max = 0; tick = 0;
        }
      }
      rafRef.current = requestAnimationFrame(loop);
    };
    rafRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(rafRef.current);
  }, [os.devOverlay, os.wins.length]);

  if (!os.devOverlay) return null;

  const fpsColor = stats.fps >= 58 ? "#34c759" : stats.fps >= 45 ? "#ff9f0a" : "#ff3b30";

  return (
    <div
      className="absolute left-2 top-8 z-[900] rounded-lg px-3 py-2 font-mono text-[11px] leading-tight text-white"
      style={{
        background: "rgba(0,0,0,0.7)",
        backdropFilter: "blur(10px)",
        WebkitBackdropFilter: "blur(10px)",
        boxShadow: "0 4px 14px rgba(0,0,0,0.4)",
        pointerEvents: "none",
      }}
    >
      <div className="flex items-center gap-2">
        <span style={{ color: fpsColor, fontWeight: 700, fontSize: 14 }}>{stats.fps}</span>
        <span>FPS</span>
      </div>
      <div className="mt-0.5 text-white/65">
        ft {stats.ft}ms · avg {stats.avg}ms
      </div>
      <div className="text-white/65">
        min {stats.ftMin} · max {stats.ftMax}
      </div>
      <div className={stats.dropped > 0 ? "text-rose-400" : "text-emerald-400"}>
        dropped {stats.dropped}
      </div>
      <div className="text-white/65">windows {stats.wins} · visual {os.visualMode}</div>
    </div>
  );
}
