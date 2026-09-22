# Testing — what ran where, and what is still NOT TESTED

Legend: **SANDBOX** = this dev container (no `/dev/dri`, no `/proc/asound`, no
qemu, no apt). **FIXTURE** = the detector run against a recorded `/sys`+`/proc`
tree (`tests/fixtures/make_sysfs.py`) — it proves the *code* reads a real
machine's data correctly; it is not hardware. **QEMU** = not executed here (no
qemu binary). **IMAC** = not executed here (no iMac attached).

A result only counts in the tier it actually ran in. Nothing in this repository
upgrades a row because the code looks right.

## Run everything

    ./configure && make -j4 all
    sh scripts/run-tests.sh              # 24 rows; --quick, --no-fixtures also work

Artifacts: `out/test-summary.tsv` (one row per check), `out/logs/*.log`
(each row's full output), `out/session_*.png` / `out/settings_*.png`
(screenshots the sessions assert on).

## The 24 rows (all PASS on this host, `Result: PASS`)

| row | tier | what it proves |
|---|---|---|
| `test_render` `test_units` `test_idle` `test_leak` | SANDBOX | region/raster/cache/easing correctness; 0.1 ms CPU and **1 wakeup** in 1500 ms idle; RSS returns after 300 window cycles |
| `test_hardware:imac11_2` `:imac11_3` `:virtual` `:bare` | FIXTURE | the detector reads four different machines correctly, including the fallbacks |
| `honesty:gpu` | SANDBOX | `maclite-gpu` may not exit 0 without a verified renderer (exit 3 here → PASS) |
| `honesty:brightness` | SANDBOX | a write against fixture files is refused (`BL_SET_NOT_TESTED`), never "verified" |
| `honesty:audio-fixture` | SANDBOX | an unreadable mixer is NOT TESTED, not FAIL |
| `honesty:net-fixture` | SANDBOX | this host's address never leaks into a fixture report |
| `honesty:decode` | SANDBOX | nothing decoded is never PASS (`maclite-video-test` exits 3 here) |
| `honesty:backend` | SANDBOX | `--backend kms` with no card exits non-zero instead of silently presenting through headless |
| `session:demo` `:interact` `:dockhide` `:menuclick` `:cc` `:hardware` `:settings` | SANDBOX | real headless sessions: render loop, IPC, input, workspace switch, minimize/dock autohide, menu action, control centre, hardware report UI, Settings > Displays (four screenshots each, checked by the runner) |
| `ui-benchmark` | SANDBOX | frame cost table (docs/performance.md) |
| `maclite-memory` | SANDBOX | live RSS/PSS of a real session |
| `maclite-performance` | SANDBOX | idle CPU busy % over 5 s |

## Verdict vocabulary (spec §21)

`PASS / FAIL / UNSUPPORTED / PARTIAL / NOT TESTED`, exit codes `0 / 1 / 3 / 2 / 2`
(`4` = usage). `maclite-hardware` prints that contract and prints the list of
required checks that did **not** pass, so a green-looking report cannot hide an
unverified row. The runner exits non-zero if any row fails; `SKIP` rows are
printed as `SKIP (NOT TESTED)`, never as PASS.

## Bugs the fixture matrix caught (i.e. why it exists)

1. `class`, `vendor`, `device`, `flags`, `bInterfaceClass`, `idVendor`,
   `idProduct` are **hex** in sysfs and were parsed base-10 → every GPU/USB id
   read as 0 on a real machine.
2. EDID detailed timing descriptors were read at the wrong indices; established
   timings missed the bit order.
3. DRM connector names were split from the wrong field.
4. The default gateway needed `htonl()`; input devices needed `strsep` (records
   start with a space).
5. `/sys/block` is absent in a minimal tree → the storage probe now falls back to
   `class/block`, then `block`.
6. `iface_ip()` returned the *container's* address while reporting an iMac: the
   fix is an unconditional early return under `hw_using_fixture()`, not a check
   for `/sys/class/net/<name>` (which exists in the fixture).
7. The audio and network verdicts were implemented twice (tool + report) and
   drifted; both now come from `ml_audio_status_get()` / `ml_net_link_status()`.

## Real-iMac acceptance procedure (to be executed on hardware)

    sh scripts/hardware-check.sh --quick     # ~30 s: identity, GPU, display, audio, net, USB, storage
    sh scripts/hardware-check.sh             # full: adds the decode round trip and benchmarks

Answer the human rows honestly; `s` = skip records **NOT TESTED** (not PASS).
The script writes `out/hw-report-<date>.txt` and `.tsv`, tagged `IMAC` when the
DMI vendor is Apple, `HOST` otherwise.

1. `./maclite-hardware` — every row must be PASS/UNSUPPORTED with evidence; a
   `NOT TESTED` in GPU/Display/Brightness/Storage is the thing to fix next.
2. `./maclite-gpu`; `./maclite-gpu-benchmark --fullscreen` while a video plays.
3. `./maclite-display --modes`, then `--set 1920x1080`.
4. `./maclite-brightness get|down|up` — value must change **and** the panel must
   visibly change; `maclite-brightness list` must name `radeon_bl0`, and must flag
   `acpi_video0` as suspect unless `acpi_backlight=native` is on the cmdline.
5. `./maclite-audio test` (real tone) and `./maclite-audio volume 40`.
6. `./maclite-video-test` — H.264 and MPEG-2 must report HW, SW or explicitly
   NOT TESTED with a reason; then `--perf --seek 30` for A/V skew.
7. `./maclite-network` (tg3 up, address, DNS, TCP) and b43 association.
8. `./maclite-usb`, `./maclite-storage --bench ~`, `./maclite-power status`.
9. `maclite-memory` and `maclite-performance 5000` at idle → RAM 150-300 MB
   (hard limit 500 MB), CPU 0-2 %.
10. Suspend is **expected to fail** on iMac11,x (panel does not re-light):
    `maclite-power --allow-suspend` records the attempt; do not report it as PASS.

Each result gets a dated row here, tagged IMAC, with the command and the raw
output. Until then every hardware claim in this tree stays labelled
"designed for", not "measured".
