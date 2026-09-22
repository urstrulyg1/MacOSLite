# MacLiteOS v0.2 — hardware validation report

**Date:** 2026-09-22 · **Commit:** see `git log -1` · **Method:** every row below
is either a command that ran in this container with its exit code recorded, or it
is labelled `NOT TESTED` because the hardware to run it does not exist here.

Environment of this run:

    host      Linux x86_64 container (Arena sandbox), 2 vCPU Xeon @2.60 GHz, 3939 MB RAM
    GPU       none: no /dev/dri, no /sys/class/drm
    audio     none: no /proc/asound
    display   none: no /dev/fb0
    decoders  none: no ffmpeg / mpv / gstreamer
    qemu      absent      xorriso/grub-mksquashfs   absent      apt network   blocked

That environment is enough to verify **rules, detection code and resource
behaviour**, and not enough to verify **any hardware claim**. The two are kept
apart below, which is the entire point of the exercise.

## 1. Acceptance criteria (spec §23/§24)

| criterion | tier | result | evidence |
|---|---|---|---|
| Suite is green end to end | SANDBOX | **PASS** | `sh scripts/run-tests.sh` → 24 rows, exit 0, `out/test-summary.tsv` |
| Detection code matches a real iMac11,2 | FIXTURE | **PASS** | 21+ assertions per variant, `out/logs/hw-imac11_2.log` |
| Detection code matches a real iMac11,3 | FIXTURE | **PASS** | `out/logs/hw-imac11_3.log` |
| Detection falls back cleanly (VM / bare host) | FIXTURE | **PASS** | `virtual`, `bare` variants |
| No acceleration claim without a real GL query | SANDBOX | **PASS** | gate `honesty:gpu` (suite fails if `maclite-gpu` exits 0 with no verified renderer); on this host it exits 3 |
| No brightness change reported without a hardware write | SANDBOX+FIXTURE | **PASS** | gate `honesty:brightness` (`set 42` → exit 2, "NOT TESTED (fixture roots: no write attempted)"); the gate now also checks the exit code, which was 1 (FAIL) until v0.2 fixed it |
| A mixer that cannot be read is not called broken | FIXTURE | **PASS** | gate `honesty:audio-fixture` (volume/mute rows = NOT TESTED, exit 2) |
| No host data leaks into a machine report | FIXTURE | **PASS** | gate `honesty:net-fixture` (the container's `eth0` address no longer appears in the iMac report) |
| "Nothing decoded" is never a PASS | SANDBOX | **PASS** | gate `honesty:decode` (`maclite-video-test` exits 3 here) |
| An explicit `--backend kms` never degrades silently | SANDBOX | **PASS** | gate `honesty:backend` (`mica-comp --backend kms` with no card → exit 3, "requested but headless is running"); `--backend auto` still falls back on purpose |
| Frame budget at 1080p, worst case | SANDBOX | **PASS** | `--fullscreen`: 4.39 ms mean, 6.92 ms worst, 0 drops (budget 16.6 ms) |
| Idle RAM within goal (150–300 MB, hard <500) | SANDBOX | **PASS** | desktop userspace 47 MB RSS / 36 MB PSS |
| Idle CPU ~0–2 % | SANDBOX | **PASS** | `maclite-performance 5000` → 0.2 % busy, 0.00 fps at idle |
| Settings uses the same brightness backend as the CLI | SANDBOX | **PASS** | `session:settings` (4 screenshots), `settings/mica-settings.c` calls `bl_probe`/`bl_step` |
| Compositor has a real present backend | SANDBOX | **PASS** (headless only) | `MS_STATS.backend` reflects `--backend`; KMS/fbdev paths **NOT TESTED** |
| ISO builds and boots | — | **NOT TESTED** | xorriso/grub/mksquashfs/qemu absent |
| QEMU boot | — | **NOT TESTED** | no qemu binary |
| **KMS scanout on r600** | — | **NOT TESTED** | no `/dev/dri` here |
| **Real GL/compositing acceleration** | — | **NOT TESTED** | no GL stack, no GPU |
| **Brightness changes the panel** | — | **NOT TESTED** | no backlight device here |
| **ALC889 playback** | — | **NOT TESTED** | no `/proc/asound` |
| **VDPAU H.264 / MPEG-2 decode** | — | **NOT TESTED** | no decoder, no VDPAU runtime query |
| **tg3 / b43 traffic** | — | **NOT TESTED** | container NIC only |
| **Suspend / resume** | — | **NOT TESTED** (policy: unsupported on iMac11,x) | documented platform failure, not attempted |
| **EFI boot on Apple firmware** | — | **NOT TESTED** | no machine |

## 2. Commands that produced the rows

    ./configure && make -j4 all                 # 0 errors
    sh scripts/run-tests.sh                     # 24 rows, exit 0
    out/maclite-gpu-benchmark                   # 1.12 ms damage-only
    out/maclite-gpu-benchmark --fullscreen      # 4.39 ms worst case
    out/maclite-gpu-benchmark --windows 8       # 3.15 ms
    python3 tests/fixtures/make_sysfs.py /tmp/fx/imac11_2 --variant imac11_2
    ML_SYSFS_ROOT=/tmp/fx/imac11_2/sys ML_PROC_ROOT=... out/maclite-hardware
    sh scripts/hardware-check.sh --quick        # exercised end to end here (tier HOST)

Exit codes observed on this host (the contract: 0 PASS, 1 FAIL, 2 NOT TESTED,
3 UNSUPPORTED, 4 usage):

| tool | this host (no hardware) | imac11_2 fixture |
|---|---|---|
| `maclite-hardware` | 1 (keyboard/mouse genuinely absent) | 2 (PARTIAL: nothing left to verify) |
| `maclite-gpu` | 3 | 3 (no GL query possible) |
| `maclite-display` | 3 | 0 |
| `maclite-brightness` | 3 | 0 |
| `maclite-audio` | 3 / 4 (no args) | 2 |
| `maclite-video-test` | 3 | 3 |
| `maclite-network` | 0 (real DNS + TCP 53 here) | 2 |
| `maclite-usb` | 3 | 0 |
| `maclite-storage` | 3 | 0 |
| `maclite-power` | 3 | 0 |

## 3. What changed in v0.2, and why it matters on the iMac

1. **The v0.1 detection code could not have worked on real hardware.**
   `class`, `vendor`, `device`, `flags`, `bInterfaceClass`, `idVendor`,
   `idProduct` are hex strings in sysfs and were parsed base-10 → every GPU
   lookup and every USB id came back as 0. Fixed and pinned by tests.
2. **Brightness could have been faked.** `acpi_video0` exists on these iMacs and
   does nothing; a naive tool would have written to it and reported success.
   The device is now flagged SUSPECT on Apple+EFI without `acpi_backlight=native`,
   the write is refused (exit 3), and `boot/kernel-cmdline.txt` documents the
   flag that makes the real `radeon_bl0` appear.
3. **"Hardware decoded" was decided by a substring.** `ffmpeg -hwaccel auto`
   echoes that option even when no hardware path engages. The rule now requires
   the decoder's own output to name a hardware decoder and vetoes failure
   banners; software decodes are reported PARTIAL with the reason, never as
   acceleration.
4. **Two tools answering the same question differently is a bug.** The audio
   mixer verdict and the network link/address verdict now come from single
   functions used by both the specific tool and `maclite-hardware`.
5. **Exit codes disagreed with the printed verdict.** `maclite-brightness set`
   under fixture roots printed "NOT TESTED" but exited 1 (FAIL), which reads as
   "this machine is broken". The mapping is now explicit (absent hardware → 3,
   fixture refusal → 2, failed write → 1) and the `honesty:brightness` gate
   checks the number, not just the text. An audit of all ten tools against the
   contract found no other mismatch.
6. **A requested backend was silently downgraded.** `mica-comp --backend kms`
   on a machine with no card presented through headless and reported success. An
   explicit backend is a requirement now: it exits 3 with the reason, and
   `honesty:backend` pins that.
7. **A fixture file is not hardware.** Every place that could only measure the
   running host — address lookups, mixer ioctls, brightness writes, filesystem
   writes, TCP probes — now reports `NOT TESTED` under fixture roots instead of
   a number that looks like evidence.

## 4. What still has to happen on the machine

    sh scripts/hardware-check.sh --quick       # ~30 s sanity pass
    sh scripts/hardware-check.sh               # full, includes the decode round trip
    # answer the human rows honestly; 'skip' records NOT TESTED, not PASS

Then paste `out/hw-report-<date>.tsv` into `docs/testing.md` and update the
tables in `docs/hardware.md`, `docs/gpu.md`, `docs/audio.md` and
`docs/multimedia.md` that currently say NOT TESTED.

**No claim in this repository should be read as "works on the iMac" until that
report exists.** The architecture is validated; the hardware is not.
