# Performance measurements

Everything below was measured **in the development sandbox** (Debian 12,
2 vCPU Xeon @2.60 GHz, 3939 MB RAM, software raster, headless compositor).
Numbers from a real iMac do not exist yet and will be added here, labelled,
when the hardware is in hand (docs/testing.md).

## Performance modes and automatic reduction (§24)

The compositor starts in the mode `mica_pick_mode()` derives from the detected
GPU: no KMS → performance, no hardware renderer → balanced, fewer than two cores
or under 1 GB RAM → balanced, otherwise beautiful. `--mode` and `MICA_MODE` pin
it, and so do scripted sessions and screenshots, because a report has to render
the same way twice.

From then on the mode can only go *down*. The frame loop already calls a frame
dropped when it arrives later than 25 ms, and it feeds that same measurement to
`hw_mode_step()` (hardware/hwprobe.c): thirty net-slow frames step the ladder
down one level, notify the user and full-damage the screen, and the streak
starts over — so a machine that is *still* too slow reaches `performance`
rather than sitting at `balanced` being slow. Fast frames decay the streak, one
hiccup does nothing, the decision is one-way so nothing can oscillate, and there
is no timer, sampler or daemon involved.

The policy is a pure function so it is unit-tested without a compositor
(`test_hardware.c::test_mode_step`: 29 slow frames do nothing, the 30th steps
once, a fast frame decays, the second step lands on `performance`, the ladder
stops there, a frozen/pinned mode never moves). `honesty:mode` in
scripts/run-tests.sh holds the other half: with no KMS and no GL renderer the
compositor must log `mode=performance` (the pick really came from detection),
and `MICA_MODE=beautiful` must log `mode=beautiful` (a pin beats the picker).
`maclite-gpu` reports the live mode and how many steps §24 took.

## Compositing cost (maclite-ui-benchmark, software path)

| scenario                | resolution | mean full frame | p95    | worst  | 60 Hz budget |
|-------------------------|-----------|-----------------|--------|--------|--------------|
| desktop, 3 windows      | 1440x900  | 3.69 ms         | 5.08 ms| 5.98 ms| within |
| fullhd, 3 windows       | 1920x1080 | 3.97 ms         | 5.07 ms| 5.37 ms| within |
| fullhd, 6 windows       | 1920x1080 | 7.07 ms         | 8.76 ms|12.82 ms| within |
| damage-only clock tick  | 220x26    | 0.028 ms        |        |        | — |
| animated window xform   | 700x460   | 0.98-1.01 ms    |        |        | within |

## Damage-only vs worst-case full screen (maclite-gpu-benchmark, 300 frames)

| scene | mean composite | p95 | worst | CPU | RSS |
|---|---|---|---|---|---|
| 3 windows + menu-bar clock, damage-only | 1.12 ms | 2.94 ms | 3.48 ms | 7 % | 22.5 MB |
| 8 windows, damage-only | 3.15 ms | — | — | 19 % | 28.9 MB |
| **full-screen damage every frame** (`--fullscreen`) | 4.39 ms | 5.46 ms | 6.92 ms | 27 % | 22.3 MB |

The last row is the honest upper bound for anything that repaints the whole
screen — a fullscreen video window, a full-screen drag, a workspace slide. It
still fits 16.6 ms with ~3.8x headroom on one 2.6 GHz core, and dropped frames
were 0 in every run. This is the **software raster path**; it is what makes
"MacLiteOS stays smooth without a GL compositor" a measured statement rather
than a hope.

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

- No GPU path exercised here (no KMS in the sandbox): GL compositing, the KMS
  page-flip path and VDPAU/VA-API decode remain **NOT TESTED** (see testing.md).
- Sandbox CPU ≠ Clarkdale/Lynnfield; treat sandbox ms as an upper bound only
  in the sense that the iMac's lower clock will scale them roughly linearly.
- Idle figures here are for a *headless* session. A KMS session adds a flip
  ioctl per present; it does not add a polling loop, so idle CPU should stay at
  0-2 %, but that is a prediction until `maclite-performance 5000` runs on the
  iMac (scripts/hardware-check.sh records it).

## Historical v0.1 notes (kept for comparison)

- Boot-of-session first frame: 64 ms (was 358 ms before damage-rect merging).
- Steady full repaint after cache warm: ~7 ms at 1440x900.
- Scripted demo session: 67 frames, 0 dropped after the damage-coalescing fix
  (the v0.1 note recorded 6 drops during full-screen mode-switch repaints).
- `tests/test_idle.c`: 0.1 ms CPU and **1 wakeup** in 1500 ms of idle loop.
