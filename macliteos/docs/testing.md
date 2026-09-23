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

## Real-iMac Acceptance Procedure & Zero-Fabrication Standard

**Policy:** ZERO FAKE DATA. ZERO SIMULATED PASS RESULTS. ZERO ASSUMED HARDWARE RESULTS.
Expected iMac Mid-2010 specs are a compatibility reference only; actual results must derive exclusively from runtime detection on the booted machine.

### Execution on Target Machine:
```sh
sh scripts/hardware-check.sh --quick     # ~30 s: runtime identity, GPU, display, audio, net, USB, storage
sh scripts/hardware-check.sh             # full: adds video decode round trip, memory, benchmarks
```

Answer the human confirmation prompts honestly (`y` = confirmed, `n` = failed, `skip` = unconfirmed).
Any unconfirmed check is recorded as **NOT TESTED**, never PASS.
The script writes:
- `out/hw-report-<date>.txt` (human-readable comprehensive report)
- `out/hw-report-<date>.tsv` (raw test rows with exit codes)
- `out/hw-matrix-<date>.tsv` (the 18-component hardware validation matrix)

Tagged **`IMAC`** (`Real iMac hardware: YES`) only when DMI vendor is Apple and machine is not virtualized.

### 18-Component Hardware Validation Matrix:
The acceptance runner populates the 18-component matrix:
1. **CPU:** Model, family, stepping, core topology, scaling driver, thermal zone (`maclite-cpu`).
2. **RAM:** Total capacity, available capacity, allocation tests.
3. **GPU:** Exact PCI ID (`1002:9488` or `1002:68d8`), bound driver (`radeon`), render node (`maclite-gpu`).
4. **KMS:** Card node `/dev/dri/card*`, connector enumeration, page flips (`maclite-display`).
5. **OpenGL:** Query real Mesa/Gallium r600 GL renderer string (`maclite-gpu`).
6. **Brightness:** Readback verification of `radeon_bl0`; verify `acpi_video0` is rejected if suspect.
7. **Audio:** ALSA PCM playback node on ALC889, mixer volume/mute ioctls (`maclite-audio`).
8. **Microphone:** Audio capture PCM stream presence and recording validation.
9. **Ethernet:** Broadcom BCM5764M link state, DHCP lease, traffic via `tg3`.
10. **Wi-Fi:** Broadcom BCM43224 association and ping via `b43`.
11. **Bluetooth:** Apple BCM2046B1 controller inquiry and pairing.
12. **USB:** Intel 5 Series root hubs, keyboard, mouse, flash drive hotplug.
13. **SD reader:** Broadcom PCIe SDXC reader mount and sector read/write.
14. **FireWire:** Agere FW643 controller detection and peripheral bus probe.
15. **SATA/storage:** Read/write throughput on internal disk (`maclite-storage --bench`).
16. **Optical drive:** GA32N SuperDrive detection and unpolled handle check.
17. **iSight:** `/dev/video0` video4linux capture node and frame capture.
18. **Video decode:** UVD2 H.264/MPEG-2 hardware decode (`maclite-video-test --perf`).
19. **External display:** Mini DisplayPort hotplug and mode negotiation.

### Multimedia & Streaming Stack Verification (Spec §20):
- **Single Browser & Player:** `maclite-browser --diagnostics` and `maclite-video --diagnostics`.
- **Widevine DRM:** Probes for `libwidevinecdm.so` across known locations; checks initialization status.
- **Zero Background Daemons:** Verifies that no updater daemons, media indexers, or adblock proxy daemons are running.
- **OTT Streaming Shortcuts:** Verifies lightweight PWA launcher shortcuts (`--app=<url>`) for YouTube, Netflix, Prime Video, and Disney+.

Any subsystem without physical test confirmation remains **`NOT TESTED`**.
No row may be copied into a PASS column without the auditable raw command log.
