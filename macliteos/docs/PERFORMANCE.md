# Performance measurements

Everything below was measured **in the development sandbox** (Debian 12,
2 vCPU Xeon @2.60 GHz, 3939 MB RAM, software raster, headless compositor).
Numbers from a real iMac do not exist yet and will be added here, labelled,
when the hardware is in hand (docs/TESTING.md).

## Compositing cost (maclite-ui-benchmark, software path)

| scenario                | resolution | mean full frame | p95    | worst  | 60 Hz budget |
|-------------------------|-----------|-----------------|--------|--------|--------------|
| desktop, 3 windows      | 1440x900  | 4.59 ms         | 4.94 ms| 6.42 ms| within |
| fullhd, 3 windows       | 1920x1080 | 5.62 ms         | 8.24 ms| 8.82 ms| within |
| fullhd, 6 windows       | 1920x1080 | 9.05 ms         |10.97 ms|13.79 ms| within |
| damage-only clock tick  | 220x26    | 0.045 ms        |        |        | — |
| animated window xform   | 700x460   | 1.23-1.32 ms    |        |        | within |

Frame pacing simulation (240 vsync intervals): mean 16.85 ms, p95 16.88 ms,
worst 23.50 ms, **jitter 0.03 ms**.

## Live session (headless, multi-process)

- Boot-of-session first frame: 64 ms (was 358 ms before damage-rect merging).
- Steady full repaint after cache warm: ~7 ms at 1440x900.
- Scripted demo session (desktop+dock+panel+menus+notifications+mode switch):
  41 frames, 6 dropped (drops are >25 ms gaps during the intentional
  full-screen mode-switch repaints in software rendering).
- Idle CPU: 0.0 % over 600 ms sampling with the full session live
  (maclite-performance); tests/test_idle.c measures 0.1 ms CPU in 1500 ms of
  idle loop and 1 wakeup.

## Memory (maclite-memory, live session: compositor + 3 shell roles)

| component                    | RSS      | PSS     |
|------------------------------|----------|---------|
| compositor + WM              | 18.8 MB  | 17.3 MB |
| panel + dock + desktop (3 proc) | 5.7 MB | 1.5 MB |
| **desktop total (userspace)**| **47 MB RSS / 36 MB PSS** | |

Budget (spec §52): idle target 150-300 MB, hard ceiling 500 MB — we are an
order of magnitude under it before any app starts; finder/terminal/viewer add
~2-4 MB RSS each.

## What these numbers do NOT prove

- No GPU path exercised here (no KMS in the sandbox): GL compositing and
  VDPAU/VA-API decode remain unverified (see TESTING.md).
- Sandbox CPU ≠ Clarkdale/Lynnfield; treat sandbox ms as an upper bound only
  in the sense that the iMac's lower clock will scale them roughly linearly.
