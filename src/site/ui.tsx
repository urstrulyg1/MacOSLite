import React, { useEffect, useRef, useState } from "react";

/* reveal on scroll */
export function useReveal<T extends HTMLElement>() {
  const ref = useRef<T>(null);
  useEffect(() => {
    const el = ref.current!;
    const io = new IntersectionObserver(
      (es) => es.forEach((e) => e.isIntersecting && e.target.classList.add("on")),
      { threshold: 0.12 }
    );
    io.observe(el);
    return () => io.disconnect();
  }, []);
  return ref;
}

export function Reveal({ children, className = "", delay = 0 }: { children: React.ReactNode; className?: string; delay?: number }) {
  const ref = useReveal<HTMLDivElement>();
  return (
    <div ref={ref} className={`reveal ${className}`} style={{ transitionDelay: `${delay}ms` }}>
      {children}
    </div>
  );
}

/* animated counter */
export function Counter({ to, dur = 1400, dec = 0, suffix = "", prefix = "" }: { to: number; dur?: number; dec?: number; suffix?: string; prefix?: string }) {
  const ref = useRef<HTMLSpanElement>(null);
  const [started, setStarted] = useState(false);
  const [val, setVal] = useState(0);
  useEffect(() => {
    const el = ref.current!;
    const io = new IntersectionObserver((es) => es.forEach((e) => e.isIntersecting && setStarted(true)), { threshold: 0.4 });
    io.observe(el);
    return () => io.disconnect();
  }, []);
  useEffect(() => {
    if (!started) return;
    const t0 = performance.now();
    let raf = 0;
    const tick = (t: number) => {
      const p = Math.min(1, (t - t0) / dur);
      const e = 1 - Math.pow(1 - p, 4);
      setVal(to * e);
      if (p < 1) raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [started, to, dur]);
  return (
    <span ref={ref} className="tabular-nums">
      {prefix}{val.toFixed(dec)}{suffix}
    </span>
  );
}

/* section scaffolding */
export function Section({ id, kicker, title, lede, children, className = "" }: {
  id: string; kicker: string; title: React.ReactNode; lede?: string; children: React.ReactNode; className?: string;
}) {
  return (
    <section id={id} className={`relative mx-auto w-full max-w-6xl px-6 py-28 md:py-36 ${className}`}>
      <Reveal>
        <div className="mb-3 flex items-center gap-3">
          <span className="font-mono text-[11px] uppercase tracking-[0.3em] text-peri/90">{kicker}</span>
          <span className="h-px w-16 bg-gradient-to-r from-peri/60 to-transparent" />
        </div>
        <h2 className="track-tight max-w-3xl text-4xl font-bold leading-[1.04] text-white md:text-6xl">{title}</h2>
        {lede && <p className="mt-5 max-w-2xl text-[15px] leading-relaxed text-white/55">{lede}</p>}
      </Reveal>
      <div className="mt-14">{children}</div>
    </section>
  );
}

export function Chip({ children, tone = "zinc" }: { children: React.ReactNode; tone?: "zinc" | "green" | "peri" | "amber" }) {
  const tones: Record<string, string> = {
    zinc: "border-white/15 text-white/60",
    green: "border-emerald-400/30 bg-emerald-400/10 text-emerald-300",
    peri: "border-peri/40 bg-peri/10 text-peri",
    amber: "border-amber-400/30 bg-amber-400/10 text-amber-300",
  };
  return <span className={`inline-flex items-center gap-1.5 rounded-full border px-3 py-1 font-mono text-[10.5px] uppercase tracking-wider ${tones[tone]}`}>{children}</span>;
}
