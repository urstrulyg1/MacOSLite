/**
 * G1OS pictorial icons — original artwork.
 *
 * Shared lighting: 12 o'clock key, soft top sheen, darker foot, rim that
 * goes from white to a cool shadow. No SVG filters (those repaint on every
 * Dock scale). Depth is painted into the vectors so it stays cheap at 60 fps
 * and stays sharp from 16 px to 512 px.
 */

import React from "react";

/** Superellipse n=5, inset 2.1 on a 64 grid. Computed once. */
export const SQUIRCLE =
  "M61.9 32L61.87 42.04L61.8 45.24L61.67 47.55L61.49 49.41L61.25 50.99L60.97 52.36L60.63 53.58L60.23 54.66L59.77 55.64L59.26 56.52L58.68 57.31L58.03 58.03L57.31 58.68L56.52 59.26L55.64 59.77L54.66 60.23L53.58 60.63L52.36 60.97L50.99 61.25L49.41 61.49L47.55 61.67L45.24 61.8L42.04 61.87L32 61.9L21.96 61.87L18.76 61.8L16.45 61.67L14.59 61.49L13.01 61.25L11.64 60.97L10.42 60.63L9.34 60.23L8.36 59.77L7.48 59.26L6.69 58.68L5.97 58.03L5.32 57.31L4.74 56.52L4.23 55.64L3.77 54.66L3.37 53.58L3.03 52.36L2.75 50.99L2.51 49.41L2.33 47.55L2.2 45.24L2.13 42.04L2.1 32L2.13 21.96L2.2 18.76L2.33 16.45L2.51 14.59L2.75 13.01L3.03 11.64L3.37 10.42L3.77 9.34L4.23 8.36L4.74 7.48L5.32 6.69L5.97 5.97L6.69 5.32L7.48 4.74L8.36 4.23L9.34 3.77L10.42 3.37L11.64 3.03L13.01 2.75L14.59 2.51L16.45 2.33L18.76 2.2L21.96 2.13L32 2.1L42.04 2.13L45.24 2.2L47.55 2.33L49.41 2.51L50.99 2.75L52.36 3.03L53.58 3.37L54.66 3.77L55.64 4.23L56.52 4.74L57.31 5.32L58.03 5.97L58.68 6.69L59.26 7.48L59.77 8.36L60.23 9.34L60.63 10.42L60.97 11.64L61.25 13.01L61.49 14.59L61.67 16.45L61.8 18.76L61.87 21.96L61.9 32Z";

function useUid(prefix: string) {
  return React.useId().replace(/:/g, "") + prefix;
}

export function Frame({
  size,
  from,
  to,
  mid,
  children,
  gloss = true,
}: {
  size: number;
  from: string;
  to: string;
  mid?: string;
  children?: React.ReactNode;
  gloss?: boolean;
}) {
  const id = useUid("fr");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor={from} />
          {mid && <stop offset="0.46" stopColor={mid} />}
          <stop offset="1" stopColor={to} />
        </linearGradient>
        <linearGradient id={`${id}s`} x1="32" y1="2" x2="32" y2="36" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#fff" stopOpacity="0.55" />
          <stop offset="0.55" stopColor="#fff" stopOpacity="0.08" />
          <stop offset="1" stopColor="#fff" stopOpacity="0" />
        </linearGradient>
        <linearGradient id={`${id}r`} x1="32" y1="1" x2="32" y2="63" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#fff" stopOpacity="0.92" />
          <stop offset="0.35" stopColor="#fff" stopOpacity="0.28" />
          <stop offset="1" stopColor="#000" stopOpacity="0.32" />
        </linearGradient>
        <clipPath id={`${id}c`}>
          <path d={SQUIRCLE} />
        </clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.16)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        {gloss && <rect width="64" height="32" fill={`url(#${id}s)`} />}
        <rect y="48" width="64" height="16" fill="rgba(0,0,0,0.07)" />
        {children}
      </g>
      <path d={SQUIRCLE} fill="none" stroke={`url(#${id}r)`} strokeWidth="1.15" />
    </svg>
  );
}

/* ------------------------------ apps ---------------------------------- */

export function FinderIcon({ size = 48 }: { size?: number }) {
  const id = useUid("fd");
  const fine = size >= 28;
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}r`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#9ad7ff" />
          <stop offset="0.5" stopColor="#4eb4ff" />
          <stop offset="1" stopColor="#1c8ef0" />
        </linearGradient>
        <linearGradient id={`${id}l`} x1="20" y1="2" x2="20" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#2f95ff" />
          <stop offset="0.5" stopColor="#0b74ef" />
          <stop offset="1" stopColor="#0054c8" />
        </linearGradient>
        <linearGradient id={`${id}rim`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#fff" stopOpacity="0.7" />
          <stop offset="1" stopColor="#003" stopOpacity="0.25" />
        </linearGradient>
        <clipPath id={`${id}c`}>
          <path d={SQUIRCLE} />
        </clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.16)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}r)`} />
        <path d="M2 2H33C33 16 31.6 22 30.4 26.2C29.2 30.2 36 33.2 36 36.4C36 39.4 33 42 33 62H2Z" fill={`url(#${id}l)`} />
        <rect width="64" height="28" fill="#fff" opacity="0.16" />
        <path d="M33 3C33 16 31.6 22 30.4 26.2C29.2 30.2 36 33.2 36 36.4C36 39.4 33 42 33 61" fill="none" stroke="#07182c" strokeWidth="2.15" strokeLinecap="round" />
        {fine && (
          <>
            <path d="M15.5 19.5c2-2.1 7-2.1 9.2.4" fill="none" stroke="#07182c" strokeWidth="2.05" strokeLinecap="round" />
            <path d="M39.2 19.9c2.2-2.5 7.2-2.5 9.2-.4" fill="none" stroke="#07182c" strokeWidth="2.05" strokeLinecap="round" />
          </>
        )}
        <ellipse cx="20.2" cy="27.2" rx="2.15" ry="3.35" fill="#07182c" />
        <ellipse cx="43.6" cy="27.2" rx="2.15" ry="3.35" fill="#07182c" />
        {fine && (
          <>
            <ellipse cx="19.5" cy="26.2" rx="0.7" ry="1.05" fill="#fff" opacity="0.85" />
            <ellipse cx="42.9" cy="26.2" rx="0.7" ry="1.05" fill="#fff" opacity="0.85" />
          </>
        )}
        <path d="M18.2 40.2c4.6 6.4 23 6.4 27.6 0" fill="none" stroke="#07182c" strokeWidth="2.55" strokeLinecap="round" />
      </g>
      <path d={SQUIRCLE} fill="none" stroke={`url(#${id}rim)`} strokeWidth="1.15" />
    </svg>
  );
}

export function SafariIcon({ size = 48 }: { size?: number }) {
  const id = useUid("sf");
  const ticks = size >= 32;
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <radialGradient id={`${id}d`} cx="50%" cy="38%" r="62%">
          <stop offset="0%" stopColor="#3ec0ff" />
          <stop offset="46%" stopColor="#0a86ff" />
          <stop offset="100%" stopColor="#004fd0" />
        </radialGradient>
        <linearGradient id={`${id}rim`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#fff" />
          <stop offset="1" stopColor="#c5ccd8" />
        </linearGradient>
        <clipPath id={`${id}c`}>
          <path d={SQUIRCLE} />
        </clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.14)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill="#f7f8fb" />
        <rect width="64" height="30" fill="#fff" opacity="0.7" />
        <circle cx="32" cy="32.4" r="22.2" fill={`url(#${id}d)`} />
        <circle cx="32" cy="32.4" r="22.2" fill="none" stroke="rgba(255,255,255,0.35)" strokeWidth="1.2" />
        {ticks &&
          Array.from({ length: 12 }, (_, i) => {
            const a = (i * 30 * Math.PI) / 180;
            const r1 = i % 3 === 0 ? 16.6 : 18.2;
            const r2 = 20.6;
            return (
              <line
                key={i}
                x1={32 + r1 * Math.sin(a)}
                y1={32.4 - r1 * Math.cos(a)}
                x2={32 + r2 * Math.sin(a)}
                y2={32.4 - r2 * Math.cos(a)}
                stroke="#fff"
                strokeWidth={i % 3 === 0 ? 1.5 : 0.8}
                strokeLinecap="round"
                opacity={i % 3 === 0 ? 0.95 : 0.65}
              />
            );
          })}
        <g transform="rotate(42 32 32.4)">
          <polygon points="32,12.6 27.6,32.4 32,30.2" fill="#ff4b40" />
          <polygon points="32,12.6 36.4,32.4 32,30.2" fill="#d30b16" />
          <polygon points="32,52.2 27.6,32.4 32,34.6" fill="#ffffff" />
          <polygon points="32,52.2 36.4,32.4 32,34.6" fill="#d5dbe6" />
          <circle cx="32" cy="32.4" r="3.1" fill="#fff" />
          <circle cx="32" cy="32.4" r="1.45" fill="#243044" />
        </g>
      </g>
      <path d={SQUIRCLE} fill="none" stroke={`url(#${id}rim)`} strokeWidth="1.2" />
    </svg>
  );
}

function Gear({
  cx, cy, r, teeth, rot, fill, stroke,
}: { cx: number; cy: number; r: number; teeth: number; rot: number; fill: string; stroke: string }) {
  return (
    <g transform={`translate(${cx} ${cy}) rotate(${rot})`}>
      {Array.from({ length: teeth }, (_, i) => (
        <rect
          key={i}
          x={-r * 0.16}
          y={-r - r * 0.22}
          width={r * 0.32}
          height={r * 0.32}
          rx={r * 0.08}
          fill={fill}
          stroke={stroke}
          strokeWidth="0.4"
          transform={`rotate(${(360 / teeth) * i})`}
        />
      ))}
      <circle r={r} fill={fill} stroke={stroke} strokeWidth="0.6" />
      <circle r={r * 0.42} fill="#1c1e24" />
    </g>
  );
}

export function SettingsIcon({ size = 48 }: { size?: number }) {
  const id = useUid("st");
  const full = size >= 36;
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#8d8e96" />
          <stop offset="0.5" stopColor="#5c5e66" />
          <stop offset="1" stopColor="#3a3c44" />
        </linearGradient>
        <radialGradient id={`${id}m`} cx="40%" cy="32%" r="70%">
          <stop offset="0%" stopColor="#ffffff" />
          <stop offset="42%" stopColor="#e7e8ee" />
          <stop offset="100%" stopColor="#9a9caa" />
        </radialGradient>
        <radialGradient id={`${id}bl`} cx="40%" cy="32%" r="70%">
          <stop offset="0%" stopColor="#8ec5ff" />
          <stop offset="100%" stopColor="#1d6fe0" />
        </radialGradient>
        <radialGradient id={`${id}or`} cx="40%" cy="30%" r="70%">
          <stop offset="0%" stopColor="#ffc56a" />
          <stop offset="100%" stopColor="#f07a00" />
        </radialGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.18)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        <rect width="64" height="28" fill="#fff" opacity="0.12" />
        <circle cx="32" cy="33" r="20" fill="#23252b" />
        {full ? (
          <>
            <Gear cx={38} cy={28} r={11} teeth={8} rot={12} fill={`url(#${id}m)`} stroke="#b7b9c4" />
            <Gear cx={24} cy={38} r={10} teeth={8} rot={-8} fill={`url(#${id}bl)`} stroke="#7eb6ff" />
            <Gear cx={42} cy={42} r={6.4} teeth={7} rot={18} fill={`url(#${id}or)`} stroke="#ffd18a" />
          </>
        ) : (
          <Gear cx={32} cy={33} r={12} teeth={8} rot={8} fill={`url(#${id}m)`} stroke="#c5c7d0" />
        )}
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.35)" strokeWidth="1.1" />
    </svg>
  );
}

export function TerminalIcon({ size = 48 }: { size?: number }) {
  const id = useUid("tm");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#2a2d34" />
          <stop offset="1" stopColor="#0c0e12" />
        </linearGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.2)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        <rect width="64" height="22" fill="#fff" opacity="0.05" />
        {size >= 36 && (
          <g>
            <circle cx="14" cy="14" r="2.1" fill="#ff5f57" />
            <circle cx="20.2" cy="14" r="2.1" fill="#febc2e" />
            <circle cx="26.4" cy="14" r="2.1" fill="#28c840" />
          </g>
        )}
        <path d="M16 26.5L26.5 34.5L16 42.5" fill="none" stroke="#f4f7ff" strokeWidth="3.1" strokeLinecap="round" strokeLinejoin="round" />
        <rect x="30.5" y="39.2" width="14" height="3.2" rx="1" fill="#f4f7ff" />
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.18)" strokeWidth="1.1" />
    </svg>
  );
}

export function MusicIcon({ size = 48 }: { size?: number }) {
  return (
    <Frame size={size} from="#ff5b7a" mid="#f31d49" to="#c4002a">
      <g fill="#9a001f" transform="translate(1.4 1.8)" opacity="0.35">
        <ellipse cx="23" cy="42" rx="5.2" ry="4" transform="rotate(-16 23 42)" />
        <ellipse cx="40.5" cy="37" rx="5.2" ry="4" transform="rotate(-16 40.5 37)" />
        <rect x="26" y="18" width="3.2" height="24" rx="1" />
        <rect x="43.4" y="13" width="3.2" height="24" rx="1" />
        <polygon points="26,18 46.6,13 46.6,18.2 26,23.2" />
      </g>
      <g fill="#fff">
        <ellipse cx="23" cy="41" rx="5.2" ry="4.05" transform="rotate(-16 23 41)" />
        <ellipse cx="40.5" cy="36" rx="5.2" ry="4.05" transform="rotate(-16 40.5 36)" />
        <rect x="26" y="17.2" width="3.3" height="24" rx="1.1" />
        <rect x="43.4" y="12.2" width="3.3" height="24" rx="1.1" />
        <polygon points="26,17.2 46.7,12.2 46.7,17.6 26,22.6" />
      </g>
    </Frame>
  );
}

const PETALS: [string, string, number][] = [
  ["#ffe14a", "#f5b400", 0],
  ["#ffb03a", "#f07a00", 45],
  ["#ff6a5a", "#e4232b", 90],
  ["#ff5d9a", "#e2186a", 135],
  ["#c86bff", "#7a3ee0", 180],
  ["#5aa7ff", "#1d6fe8", 225],
  ["#5ad7ff", "#1493d6", 270],
  ["#5ee08a", "#1ea84a", 315],
];

export function PhotosIcon({ size = 48 }: { size?: number }) {
  const id = useUid("ph");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        {PETALS.map((p, i) => (
          <linearGradient key={i} id={`${id}p${i}`} x1="0" y1="1" x2="0" y2="0">
            <stop offset="0" stopColor={p[1]} />
            <stop offset="1" stopColor={p[0]} />
          </linearGradient>
        ))}
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.12)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill="#fff" />
        <rect width="64" height="28" fill="#fff" />
        <g transform="translate(32 33)">
          {PETALS.map((p, i) => (
            <ellipse key={i} cx="0" cy="-11.2" rx="4.7" ry="10" fill={`url(#${id}p${i})`} transform={`rotate(${p[2]})`} opacity="0.95" />
          ))}
          <circle r="4.3" fill="#fff" />
          <circle r="4.3" fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth="0.6" />
        </g>
      </g>
      <path d={SQUIRCLE} fill="none" stroke="#e6e8ee" strokeWidth="1.1" />
    </svg>
  );
}

export function PlayerIcon({ size = 48 }: { size?: number }) {
  const holes = size >= 40;
  return (
    <Frame size={size} from="#a06bff" mid="#7a45ef" to="#4b22c4">
      {holes && (
        <g fill="rgba(255,255,255,0.28)">
          {[18, 26, 34, 42].map((y) => (
            <g key={y}>
              <circle cx="11.5" cy={y} r="1.15" />
              <circle cx="52.5" cy={y} r="1.15" />
            </g>
          ))}
        </g>
      )}
      <circle cx="32" cy="33" r="13.5" fill="rgba(255,255,255,0.14)" />
      <path d="M28 24.6c0-1.2 1.3-2 2.3-1.35l11 6.7c1 0.6 1 2.05 0 2.65l-11 6.7c-1 .65-2.3-.15-2.3-1.35z" fill="#fff" />
    </Frame>
  );
}

const TILES: [number, number, string, string][] = [
  [14.5, 14.5, "#ff6a45", "#ff3b1f"],
  [27.2, 14.5, "#5ec8ff", "#1d8ff0"],
  [39.9, 14.5, "#5ee0a0", "#18b86a"],
  [14.5, 27.2, "#ff5d7a", "#e2184a"],
  [27.2, 27.2, "#49d4ff", "#0094e0"],
  [39.9, 27.2, "#8b8cff", "#4d46e0"],
  [14.5, 39.9, "#c084fc", "#8b3fe0"],
  [27.2, 39.9, "#7ddea0", "#22a85a"],
  [39.9, 39.9, "#ffd15a", "#f0a000"],
];

export function LaunchpadIcon({ size = 48 }: { size?: number }) {
  const id = useUid("lp");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        {TILES.map((t, i) => (
          <linearGradient key={i} id={`${id}t${i}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor={t[2]} />
            <stop offset="1" stopColor={t[3]} />
          </linearGradient>
        ))}
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.12)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill="#eef0f8" />
        <rect width="64" height="30" fill="#fff" opacity="0.55" />
        {TILES.map((t, i) => (
          <g key={i}>
            <rect x={t[0]} y={t[1]} width="9.6" height="9.6" rx="2.5" fill={`url(#${id}t${i})`} />
            <rect x={t[0]} y={t[1]} width="9.6" height="3.2" rx="2.5" fill="#fff" opacity="0.35" />
          </g>
        ))}
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.9)" strokeWidth="1.1" />
    </svg>
  );
}

export function CalculatorIcon({ size = 48 }: { size?: number }) {
  const id = useUid("ca");
  const keys = [
    [20, 28, 0], [32, 28, 0], [44, 28, 1],
    [20, 40, 0], [32, 40, 0], [44, 40, 1],
    [20, 52, 0], [32, 52, 0], [44, 52, 1],
  ];
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#3a3c42" />
          <stop offset="1" stopColor="#141518" />
        </linearGradient>
        <linearGradient id={`${id}g`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#f2f2f4" />
          <stop offset="1" stopColor="#a7a8b0" />
        </linearGradient>
        <linearGradient id={`${id}o`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#ffb43a" />
          <stop offset="1" stopColor="#ef6d00" />
        </linearGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.2)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        <rect width="64" height="24" fill="#fff" opacity="0.06" />
        {size >= 36 && <rect x="14" y="10" width="36" height="10" rx="2.2" fill="#0e0f12" />}
        {keys.map(([x, y, o], i) => (
          <circle key={i} cx={x} cy={y - (size >= 36 ? 0 : 6)} r="4.15" fill={o ? `url(#${id}o)` : `url(#${id}g)`} />
        ))}
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.16)" strokeWidth="1.1" />
    </svg>
  );
}

export function TextEditIcon({ size = 48 }: { size?: number }) {
  const id = useUid("te");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}p`} x1="32" y1="8" x2="32" y2="60" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#fff" />
          <stop offset="1" stopColor="#e7eef8" />
        </linearGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.14)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill="#dbe7f5" />
        <path d="M12 10h28l12 12v30a4 4 0 0 1-4 4H16a4 4 0 0 1-4-4V14a4 4 0 0 1 4-4z" fill={`url(#${id}p)`} />
        <path d="M40 10v12h12" fill="#c5d4e8" />
        <path d="M18 30h22M18 36h22M18 42h16" stroke="#8aa0be" strokeWidth="1.6" strokeLinecap="round" />
        <g transform="translate(34 34) rotate(38)">
          <rect x="0" y="0" width="5" height="22" rx="1.2" fill="#3b82f6" />
          <polygon points="0,22 5,22 2.5,26.5" fill="#f2c14e" />
          <rect x="0" y="-2" width="5" height="3.2" rx="0.6" fill="#f43f5e" />
        </g>
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.8)" strokeWidth="1.1" />
    </svg>
  );
}

export function NotesIcon({ size = 48 }: { size?: number }) {
  const id = useUid("nt");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#ffe56a" />
          <stop offset="1" stopColor="#f5c400" />
        </linearGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.14)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        <rect width="64" height="16" fill="#f0b400" />
        <rect x="14" y="8" width="36" height="3" rx="1" fill="rgba(0,0,0,0.08)" />
        <path d="M16 26h32M16 33h32M16 40h32M16 47h22" stroke="rgba(160,110,0,0.45)" strokeWidth="1.35" strokeLinecap="round" />
        <rect x="12" y="16" width="1.6" height="46" fill="#f43f5e" opacity="0.8" />
        <rect width="64" height="22" fill="#fff" opacity="0.18" />
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.45)" strokeWidth="1.1" />
    </svg>
  );
}

export function CalendarIcon({ size = 48 }: { size?: number }) {
  const id = useUid("cal");
  const now = new Date();
  const mon = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"][now.getMonth()];
  const day = String(now.getDate());
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
        <linearGradient id={`${id}r`} x1="32" y1="2" x2="32" y2="22" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#ff6b63" />
          <stop offset="1" stopColor="#e4232b" />
        </linearGradient>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.12)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill="#fff" />
        <rect width="64" height="20" fill={`url(#${id}r)`} />
        <text x="32" y="14.2" textAnchor="middle" fontFamily="Inter, system-ui, sans-serif" fontSize="9" fontWeight="700" fill="#fff" letterSpacing="0.12em">{mon}</text>
        <text x="32" y="48" textAnchor="middle" fontFamily="Inter, system-ui, sans-serif" fontSize={day.length > 1 ? 22 : 26} fontWeight="300" fill="#1d1d1f">{day}</text>
      </g>
      <path d={SQUIRCLE} fill="none" stroke="#e6e8ee" strokeWidth="1.1" />
    </svg>
  );
}

export function ClockIcon({ size = 48 }: { size?: number }) {
  const id = useUid("ck");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="2" x2="32" y2="62" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#2a2c31" />
          <stop offset="1" stopColor="#0c0d10" />
        </linearGradient>
        <clipPath id={`${id}c`}><path d={SQUIRCLE} /></clipPath>
      </defs>
      <ellipse cx="32" cy="62.6" rx="17" ry="2.1" fill="rgba(0,0,0,0.2)" />
      <g clipPath={`url(#${id}c)`}>
        <rect width="64" height="64" fill={`url(#${id}b)`} />
        <circle cx="32" cy="32" r="22" fill="#16181c" stroke="#3a3d44" strokeWidth="1" />
        <path d="M14 18c6-8 14-10 18-10" fill="none" stroke="#fff" strokeWidth="3" opacity="0.12" strokeLinecap="round" />
        {[0, 90, 180, 270].map((a) => (
          <line key={a} x1="32" y1="13" x2="32" y2="16.2" stroke="#d0d3da" strokeWidth="1.6" strokeLinecap="round" transform={`rotate(${a} 32 32)`} />
        ))}
        <line x1="32" y1="32" x2="32" y2="18.5" stroke="#fff" strokeWidth="2.3" strokeLinecap="round" />
        <line x1="32" y1="32" x2="43.5" y2="34" stroke="#fff" strokeWidth="2" strokeLinecap="round" />
        <line x1="32" y1="32" x2="38" y2="44" stroke="#ff9f0a" strokeWidth="1.15" strokeLinecap="round" />
        <circle cx="32" cy="32" r="2.1" fill="#ff9f0a" />
      </g>
      <path d={SQUIRCLE} fill="none" stroke="rgba(255,255,255,0.16)" strokeWidth="1.1" />
    </svg>
  );
}

export function InstallerIcon({ size = 48 }: { size?: number }) {
  return (
    <Frame size={size} from="#4ec4ff" mid="#1a95f8" to="#0066e0">
      <circle cx="32" cy="32" r="16.5" fill="#0047a8" />
      <path d="M16 32a16.5 16.5 0 0 1 32 0" fill="none" stroke="rgba(0,0,0,0.28)" strokeWidth="2.2" />
      <path d="M29 20h6v9h6.5L32 40.5 22.5 29H29z" fill="#fff" />
    </Frame>
  );
}

export function ComputerIcon({ size = 48 }: { size?: number }) {
  const id = useUid("pc");
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}a`} x1="32" y1="6" x2="32" y2="42" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#f8fafc" />
          <stop offset="1" stopColor="#b7c0cc" />
        </linearGradient>
        <linearGradient id={`${id}s`} x1="32" y1="12" x2="32" y2="34" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#7ec8ff" />
          <stop offset="1" stopColor="#1d4f86" />
        </linearGradient>
      </defs>
      <ellipse cx="32" cy="58" rx="16" ry="2.2" fill="rgba(0,0,0,0.16)" />
      <rect x="10" y="8" width="44" height="32" rx="4" fill={`url(#${id}a)`} stroke="#8b97a6" strokeWidth="0.8" />
      <rect x="13.5" y="11" width="37" height="22" rx="1.6" fill={`url(#${id}s)`} />
      <rect x="10" y="36" width="44" height="6" rx="1.2" fill="#d5dbe3" />
      <rect x="28" y="42" width="8" height="7" fill="#c5ccd6" />
      <rect x="20" y="49" width="24" height="3.2" rx="1.4" fill="#b7c0cc" />
      <circle cx="32" cy="39" r="0.9" fill="#34c759" />
    </svg>
  );
}

export function SysInfoIcon({ size = 48 }: { size?: number }) {
  return (
    <Frame size={size} from="#7ecbff" mid="#3aa0f5" to="#1d5ed8">
      <circle cx="32" cy="32" r="16" fill="none" stroke="#fff" strokeWidth="3.2" />
      <rect x="29.6" y="28" width="4.8" height="14" rx="2" fill="#fff" />
      <circle cx="32" cy="22.2" r="2.5" fill="#fff" />
    </Frame>
  );
}

/* ------------------------------ trash --------------------------------- */

export function TrashIcon({ size = 48, count = 0 }: { size?: number; count?: number }) {
  const id = useUid("tr");
  const full = count > 0;
  const wires = [18.2, 22.2, 26.4, 30.6, 34.2, 38, 41.8, 45.6];
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}m`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#ffffff" />
          <stop offset="0.45" stopColor="#d5dbe4" />
          <stop offset="1" stopColor="#8e98a6" />
        </linearGradient>
        <linearGradient id={`${id}b`} x1="16" y1="20" x2="48" y2="56" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="rgba(255,255,255,0.55)" />
          <stop offset="0.4" stopColor="rgba(210,218,228,0.18)" />
          <stop offset="1" stopColor="rgba(120,130,145,0.28)" />
        </linearGradient>
        <clipPath id={`${id}c`}>
          <path d="M15.5 18.5 L20.2 52.2 C20.6 55 25 57.2 32 57.2 C39 57.2 43.4 55 43.8 52.2 L48.5 18.5 Z" />
        </clipPath>
      </defs>
      <ellipse cx="32" cy="58.6" rx="13" ry="2" fill="rgba(0,0,0,0.18)" />
      <path d="M26 12.5h12" stroke={`url(#${id}m)`} strokeWidth="2.2" strokeLinecap="round" />
      <path d="M28.2 12.4V10.2c0-1.3 7.6-1.3 7.6 0v2.2" fill="none" stroke={`url(#${id}m)`} strokeWidth="2" strokeLinecap="round" />
      <ellipse cx="32" cy="18.2" rx="16.6" ry="5.1" fill="#c5ccd6" />
      <g clipPath={`url(#${id}c)`}>
        <rect x="14" y="16" width="36" height="42" fill={`url(#${id}b)`} />
        {full && (
          <g>
            <polygon points="22,20 28,14 34,18 30,24" fill="#fff" />
            <polygon points="30,16 38,11 44,16 40,23 32,21" fill="#f1f4f8" />
            <polygon points="24,28 34,24 42,30 36,36 26,34" fill="rgba(255,255,255,0.8)" />
          </g>
        )}
        {wires.map((x, i) => {
          const x2 = 20.2 + ((x - 15.5) / 33) * 23.6;
          return (
            <line key={i} x1={x} y1="18" x2={x2} y2="54" stroke={i < 2 || i > 5 ? "rgba(255,255,255,0.85)" : "rgba(90,100,115,0.55)"} strokeWidth="1.15" />
          );
        })}
        {[28, 38, 48].map((y) => (
          <ellipse key={y} cx="32" cy={y} rx={16.2 - (y - 18) * 0.18} ry="3.3" fill="none" stroke="rgba(255,255,255,0.7)" strokeWidth="1.15" />
        ))}
      </g>
      <path d="M15.5 18.5 L20.2 52.2 C20.6 55 25 57.2 32 57.2 C39 57.2 43.4 55 43.8 52.2 L48.5 18.5" fill="none" stroke={`url(#${id}m)`} strokeWidth="1.3" />
      <ellipse cx="32" cy="18.2" rx="16.6" ry="5.1" fill="none" stroke="#fff" strokeWidth="1.6" />
      <ellipse cx="32" cy="18.2" rx="16.6" ry="5.1" fill="none" stroke="#8b95a3" strokeWidth="0.6" />
    </svg>
  );
}

/* ------------------------------ folders -------------------------------- */

const FOLDER_GLYPH: Record<string, React.ReactNode> = {
  documents: <path d="M22 30h8.5l4.5 4.5V46h-13z M30.5 30v4.5H35" fill="none" stroke="#fff" strokeWidth="1.7" strokeLinejoin="round" />,
  downloads: <path d="M32 30v10 M28 36.5L32 40.5 36 36.5 M26 44h12" fill="none" stroke="#fff" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" />,
  desktop: <><rect x="24" y="31" width="16" height="10" rx="1.2" fill="none" stroke="#fff" strokeWidth="1.6" /><path d="M29 44h6 M32 41v3" stroke="#fff" strokeWidth="1.6" strokeLinecap="round" /></>,
  pictures: <><rect x="23" y="31" width="18" height="13" rx="1.4" fill="none" stroke="#fff" strokeWidth="1.6" /><circle cx="28" cy="35.2" r="1.4" fill="#fff" /><path d="M24.5 43l5-4.2 3.2 2.6 3-2.8 4.8 4.4" fill="none" stroke="#fff" strokeWidth="1.4" strokeLinejoin="round" /></>,
  music: <><path d="M28 42.5V32.5l9-1.6v7.4" fill="none" stroke="#fff" strokeWidth="1.6" strokeLinecap="round" /><circle cx="26.4" cy="42.5" r="2" fill="#fff" /><circle cx="35.2" cy="38.6" r="2" fill="#fff" /></>,
  movies: <path d="M28 31.5v12l10-6z" fill="#fff" />,
  apps: <g fill="#fff">{[[24, 32], [30, 32], [24, 38], [30, 38]].map(([x, y]) => <rect key={`${x}${y}`} x={x} y={y} width="4.6" height="4.6" rx="1" />)}</g>,
  shared: <><circle cx="28" cy="33" r="2.3" fill="#fff" /><circle cx="36" cy="33.6" r="2" fill="#fff" /><path d="M22.5 44c.6-3.2 2.6-4.6 5.5-4.6s4.6 1.2 5.4 3.6" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" /><path d="M33 40.2c1.8.1 3.2 1 4 2.8" fill="none" stroke="#fff" strokeWidth="1.4" strokeLinecap="round" /></>,
  network: <><circle cx="32" cy="38" r="7" fill="none" stroke="#fff" strokeWidth="1.5" /><path d="M25 38h14 M32 31c1.8 2 2.6 4.2 2.6 7s-.8 5-2.6 7c-1.8-2-2.6-4.2-2.6-7s.8-5 2.6-7z" fill="none" stroke="#fff" strokeWidth="1.2" /></>,
  cloud: <path d="M26 42.5h12.2a4.2 4.2 0 0 0 .4-8.3 5.2 5.2 0 0 0-9.8-1.2A3.6 3.6 0 0 0 26 42.5z" fill="#fff" />,
  home: <path d="M23 38.5V44h5.2v-3.4h3.6V44H37v-5.5L30 31.5z" fill="#fff" />,
};

export function FolderIcon({ size = 48, variant = "plain" }: { size?: number; variant?: string }) {
  const id = useUid("fo");
  const glyph = FOLDER_GLYPH[variant];
  const open = variant === "open";
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}k`} x1="32" y1="12" x2="32" y2="28" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#7ecbff" />
          <stop offset="1" stopColor="#1f8fe8" />
        </linearGradient>
        <linearGradient id={`${id}f`} x1="32" y1="24" x2="32" y2="56" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#9fd8ff" />
          <stop offset="0.35" stopColor="#49b4fb" />
          <stop offset="1" stopColor="#0c6ec8" />
        </linearGradient>
      </defs>
      <ellipse cx="32" cy="56.5" rx="16" ry="2.2" fill="rgba(0,0,0,0.16)" />
      <path d="M8 22c0-2.4 1.8-4 4.2-4H24c2.2 0 3.2 1.2 4.6 2.8l1.6 1.8H52c2.6 0 4.4 1.8 4.4 4.2V46c0 2.6-2 4.6-4.6 4.6H12.4C9.8 50.6 8 48.6 8 46Z" fill={`url(#${id}k)`} />
      {!open && <rect x="14" y="20" width="34" height="8" rx="1.2" fill="#fff" opacity="0.88" />}
      <path
        d={open
          ? "M6 30h52c1.6 0 2.6 1.4 2.2 2.8L56 48c-.6 2.2-2.4 3.8-4.6 3.8H14c-2.2 0-4-1.6-4.6-3.8L3.6 32.8C3.2 31.4 4.2 30 6 30z"
          : "M6 27.5h52c1.6 0 2.6 1.4 2.2 2.8L56 49.2c-.6 2.2-2.4 3.8-4.6 3.8H14c-2.2 0-4-1.6-4.6-3.8L3.6 30.3C3.2 28.9 4.2 27.5 6 27.5z"}
        fill={`url(#${id}f)`}
      />
      <path d="M8 28.2h48" stroke="#fff" strokeWidth="1.1" opacity="0.45" strokeLinecap="round" />
      {glyph && <g>{glyph}</g>}
    </svg>
  );
}

/* ------------------------------ drives --------------------------------- */

export function DriveIcon({ size = 48, kind = "internal" }: { size?: number; kind?: "internal" | "external" | "usb" }) {
  const id = useUid("dv");
  if (kind === "usb") {
    return (
      <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
        <defs>
          <linearGradient id={`${id}b`} x1="32" y1="16" x2="32" y2="56" gradientUnits="userSpaceOnUse">
            <stop offset="0" stopColor="#f8fafc" />
            <stop offset="1" stopColor="#d5dbe3" />
          </linearGradient>
          <linearGradient id={`${id}m`} x1="32" y1="6" x2="32" y2="18" gradientUnits="userSpaceOnUse">
            <stop offset="0" stopColor="#eef2f6" />
            <stop offset="1" stopColor="#9aa6b4" />
          </linearGradient>
        </defs>
        <ellipse cx="32" cy="57" rx="12" ry="2" fill="rgba(0,0,0,0.16)" />
        <rect x="24" y="6" width="16" height="12" rx="1.6" fill={`url(#${id}m)`} stroke="#8b97a6" strokeWidth="0.7" />
        <rect x="27" y="9" width="3.2" height="5" rx="0.4" fill="#6b7684" />
        <rect x="33.8" y="9" width="3.2" height="5" rx="0.4" fill="#6b7684" />
        <rect x="18" y="16" width="28" height="36" rx="6" fill={`url(#${id}b)`} stroke="#b7c0cc" strokeWidth="0.8" />
        <rect x="22" y="22" width="20" height="16" rx="2" fill="#0a84ff" />
        <path d="M32 26.2v8 M29 31.2l3 3 3-3" fill="none" stroke="#fff" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    );
  }
  const external = kind === "external";
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}b`} x1="32" y1="8" x2="32" y2="54" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#f7f9fb" />
          <stop offset="0.45" stopColor="#e4e9ef" />
          <stop offset="1" stopColor="#aeb8c4" />
        </linearGradient>
      </defs>
      <ellipse cx="32" cy="56" rx="18" ry="2.3" fill="rgba(0,0,0,0.16)" />
      <rect x="8" y="14" width="48" height="34" rx="6" fill={`url(#${id}b)`} stroke="#8d98a6" strokeWidth="0.8" />
      <rect x="12" y="18" width="40" height="18" rx="2.4" fill="#2c3440" />
      <rect x="16" y="24" width="22" height="2.2" rx="1" fill="#0f141c" />
      {external && <rect x="8" y="14" width="6" height="34" rx="3" fill="#0a84ff" />}
      <circle cx="16" cy="42" r="2" fill={external ? "#ff9f0a" : "#34c759"} />
      <circle cx="22.5" cy="42" r="2" fill="#0a84ff" />
      <rect x="30" y="40.6" width="18" height="2.8" rx="1.2" fill="#8b97a6" />
    </svg>
  );
}

/* ------------------------------ documents ------------------------------ */

const DOC_COLOR: Record<string, [string, string]> = {
  pdf: ["#ff5a4e", "PDF"],
  text: ["#3b82f6", "TXT"],
  image: ["#14b8c7", "IMG"],
  video: ["#8b5cf6", "MOV"],
  audio: ["#f43f5e", "AUD"],
  archive: ["#f59e0b", "ZIP"],
  sheet: ["#22c55e", "NUM"],
  presentation: ["#0ea5e9", "KEY"],
  keynote: ["#0ea5e9", "KEY"],
  code: ["#6366f1", "C"],
  "disk-image": ["#0284c7", "DMG"],
  executable: ["#10b981", "APP"],
  config: ["#64748b", "CFG"],
  generic: ["#94a3b8", "FILE"],
};

export function DocIcon({ size = 48, kind = "generic" }: { size?: number; kind?: string }) {
  const id = useUid("dc");
  const [color, label] = DOC_COLOR[kind] ?? DOC_COLOR.generic;
  const showLabel = size >= 28;
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" className="g1-ico" aria-hidden>
      <defs>
        <linearGradient id={`${id}p`} x1="32" y1="6" x2="32" y2="60" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#ffffff" />
          <stop offset="1" stopColor="#eef1f6" />
        </linearGradient>
      </defs>
      <ellipse cx="32" cy="59" rx="14" ry="2" fill="rgba(0,0,0,0.12)" />
      <path d="M16 8h22l12 12v34a4 4 0 0 1-4 4H16a4 4 0 0 1-4-4V12a4 4 0 0 1 4-4z" fill={`url(#${id}p)`} stroke="#d5dbe6" strokeWidth="0.8" />
      <path d="M38 8v12h12" fill="#e4e9f1" stroke="#d5dbe6" strokeWidth="0.6" />
      <rect x="12" y="36" width="40" height="12" fill={color} />
      {showLabel && (
        <text x="32" y="45" textAnchor="middle" fontFamily="Inter, system-ui, sans-serif" fontSize="8" fontWeight="700" fill="#fff" letterSpacing="0.04em">
          {label}
        </text>
      )}
    </svg>
  );
}

/* ------------------------------ status badges ------------------------- */

export function StatusBadge({
  size = 32,
  tone,
  glyph,
}: {
  size?: number;
  tone: "info" | "warning" | "error" | "success" | "lock" | "sync";
  glyph: React.ReactNode;
}) {
  const fill =
    tone === "info" ? ["#5ac8ff", "#0a84ff"] :
    tone === "warning" ? ["#ffd60a", "#ff9f0a"] :
    tone === "error" ? ["#ff6b63", "#ff3b30"] :
    tone === "success" ? ["#63e07a", "#28c840"] :
    tone === "sync" ? ["#7ecbff", "#1d6fe0"] :
    ["#d5dbe3", "#8e98a6"];
  return (
    <Frame size={size} from={fill[0]} to={fill[1]} gloss>
      <g transform="translate(16 16) scale(1.35)" stroke="#fff" fill="none" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
        {glyph}
      </g>
    </Frame>
  );
}
