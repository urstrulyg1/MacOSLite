# GPU, compositing and acceleration — measured, not assumed

## The claim that must never be made casually

"Hardware accelerated" is a claim about a **real query**, not about a device
node. The rule enforced by `maclite-gpu`:

* `/dev/dri/card0` existing proves *nothing* — it proves a kernel driver bound a
  device. `maclite-gpu` says `PASS` on the "DRM device" row for that, and
  `NOT TESTED` on "Hardware Rendering" until a GL query returns a non-software
  renderer.
* `mica_gl_probe()` runs a real query (`glxinfo -B`, `eglinfo`, `es2_info`).
  It returns `GL_HW` only if a context was created and its renderer string is
  not `llvmpipe`/`softpipe`/`swrast`. No query tool → `GL_UNKNOWN` → `NOT TESTED`.
* Because of that, `maclite-gpu`'s exit code on a machine where GL cannot be
  queried is **2 or 3, never 0**. `scripts/run-tests.sh` asserts this: the row
  `honesty:gpu` FAILS the whole suite if `maclite-gpu` exits 0 without a
  verified renderer.

`maclite-gpu` prints, at the end, every required check that is not PASS — so a
green-looking run cannot hide an unverified row.

## What the compositor actually does on this hardware

MacLiteOS does not use GL for composition: the renderer is a damage-tracking CPU
rasteriser (`ml_ctx`, `core/src/raster.c`). What the v0.2 backend adds is a real
**scanout** path:

| backend | how | when chosen |
|---|---|---|
| `kms` | dumb buffers + `SETCRTC` + non-blocking `PAGE_FLIP` on `/dev/dri/cardN`, flip-completion read from the DRM fd (epoll, not polling) | a card is bound and the connectors enumerate |
| `fbdev` | `mmap(/dev/fb0)` and blit damaged rects | KMS unusable (the documented radeon black-screen case) |
| `headless` | render to RAM, no device | tests, CI, and any machine without a display |

That means the honest description of a KMS session is **"GPU scanout, CPU
raster"**: pixels are computed on the CPU and the GPU scans them out. The
compositor reports this itself in `MS_STATS` (`backend`, `accel`), which
`maclite-gpu`, `maclite-display` and `maclite-ui-benchmark --live` all read.
Nothing in the tree converts "we have a KMS session" into "we have GL".

If a GL context *is* available (`mica_gl_probe` says `GL_HW`), the mode picker
allows the `beautiful` budget; if not, it stays conservative — see
`hw_pick_mode_caps()`.

### Why no libdrm / EGL dependency

The kernel UAPI (`<drm/*.h>`, ioctls) is enough for dumb buffers and flips, so
`hardware/kms.c` includes `hardware/drm_uapi.h` (the handful of structs and
ioctl numbers, re-declared) and links against nothing. `./configure` still
reports whether libdrm/EGL headers exist on the build host, but the DRM/KMS and
fbdev backends build and run **without any optional library**. That keeps the
dependency list at "C11 + libc + libm + libpthread + libdl" and keeps the ISO
small — a core design goal (spec §1/§25).

### The iMac Mid-2010 KMS quirk

On `iMac11,x` the radeon path can come up black when the CRTC state inherited
from the Apple firmware is not replaced. `hardware/kms.c` therefore always does
an explicit mode set on open instead of trusting the inherited state, and
`maclite-display --set WxH` can re-do it by hand (`--set 1920x1080`). If even
that fails, `--backend fbdev` is the documented fallback, and the compositor's
`auto` order is exactly `kms → fbdev → headless`.

## Numbers from this sandbox (software path only — no GPU here)

`maclite-gpu-benchmark`, 1920x1080, 300 frames, this dev container (1 core):

| scene | mean composite | p95 | worst | CPU | RSS |
|---|---|---|---|---|---|
| damage-only (3 windows + clock) | **1.12 ms** | 2.94 ms | 3.48 ms | 7 % | 22.5 MB |
| 8 windows, damage-only | 3.15 ms | — | — | 19 % | 28.9 MB |
| **full-screen damage** (`--fullscreen`, the worst case a fullscreen video or a full-screen drag can cost) | **4.39 ms** | 5.46 ms | 6.92 ms | 27 % | 22.3 MB |

Budget is 16.6 ms at 60 Hz, so even the worst case has ~3.8x headroom on a
1-core 2.6 GHz Xeon — the 2010 iMac's Core i3/i5 is comparable or faster per
core. Frame drops: **0** in all three runs.

`maclite-ui-benchmark` (static table) on the same host: `fullhd 3 windows`
3.97 ms (p95 5.07) worst 5.37; `fullhd 6 windows` 7.07 ms (p95 8.76) worst 12.82
— both `WITHIN` the 16.6 ms budget.

**These are software-raster numbers.** They say the CPU path fits the budget
here; they say nothing about the iMac's GPU scanout or GL path, which is
untested until `scripts/hardware-check.sh` runs on the machine.

## Commands

    maclite-gpu                     # identity, driver, DRM nodes, GL, display, live session
    maclite-gpu --tsv               # machine readable, one row per check
    maclite-gpu --no-live           # skip the running-compositor section
    maclite-gpu-benchmark           # damage-only scene, 300 frames
    maclite-gpu-benchmark --fullscreen      # whole-screen damage (worst case)
    maclite-gpu-benchmark --live            # sample the live session's MS_STATS
    maclite-display --modes         # every mode with EDID attribution
    maclite-display --set 1920x1080 # explicit mode set, verified by read-back

Exit codes are the contract: 0 PASS, 1 FAIL, 2 NOT TESTED, 3 UNSUPPORTED,
4 usage.

## Brightness and the display power path

`maclite-brightness` and Settings > Displays share one backend
(`hardware/backlight.c`): device ranking, the Apple/EFI no-op detection, and the
rule that a value is only reported after **sysfs read-back**. Full mechanism
documentation, including why `acpi_backlight=native` is required, is in
`docs/hardware.md`; the observable contract is:

    maclite-brightness status       # mechanism + device + current value, no change
    maclite-brightness list         # every device, why it was or was not chosen (SUSPECT shown)
    maclite-brightness set 40       # exits 0 ONLY as "40% (verified by read-back from radeon_bl0)"
    maclite-brightness up|down      # 10% steps through the same verified path

Refusals are non-zero: no device → 3, suspect device → 3, write that does not
stick → 1, fixture roots → 2 (`NOT TESTED`). A brightness change is never
reported from a write alone.
