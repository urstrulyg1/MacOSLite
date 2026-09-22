# Testing matrix — what ran where, honestly

Legend: **SANDBOX** = dev container (no KMS, no qemu, no apt). **QEMU** = not
yet executed (no qemu binary in sandbox). **IMAC** = not yet executed (no
hardware in hand). We never label a result with a tier it did not run in.

| test                          | tier    | result |
|-------------------------------|---------|--------|
| test_units (region/cache/easing/spring-interrupt/font/png/raster/loop) | SANDBOX | PASS |
| test_idle (0 wakeups, ~0 CPU when settled) | SANDBOX | PASS |
| test_leak (300 window cycles, RSS returns) | SANDBOX | PASS |
| headless multi-process session (comp+panel+dock+desktop+apps) | SANDBOX | PASS, 41 frames / 6 dropped |
| maclite-ui-benchmark synthetic + pacing | SANDBOX | PASS (tables in PERFORMANCE.md) |
| maclite-memory / -performance / -diagnostics live | SANDBOX | PASS |
| screenshots (session_*.png in out/) | SANDBOX | verified visually |
| interaction scripts: menu action launches app, Meta+Space launcher, Meta+arrow workspace switch, minimize-to-dock, dock autohide hide/reveal | SANDBOX | PASS (interact.script, dockhide.script, menuclick.script) |
| ISO build (scripts/make-iso.sh) | — | NOT RUN: xorriso/grub/mksquashfs absent |
| run-vm.sh QEMU boot | — | NOT RUN: no qemu in sandbox |
| KMS/DRM compositing on r600 | — | NOT RUN: no /dev/dri here |
| VDPAU/VA-API H.264 + MPEG-2 decode | — | NOT RUN |
| b43 Wi-Fi association, tg3 link | — | NOT RUN |
| ALC889 audio playback | — | NOT RUN |
| EFI boot on Apple EFI 1.1 | — | NOT RUN |
| idle RAM/CPU on real iMac | — | NOT RUN |

## Reproduce the sandbox rows

    cd macliteos && sh scripts/build.sh        # configure + make + suite
    cd out && ./mica-comp --headless -W 1440 -H 900 --session --script demo.script
    ./maclite-ui-benchmark && ./maclite-memory && ./maclite-performance

## Real-iMac acceptance procedure (to be executed on hardware)

1. Boot the ISO (EFI entry). Record time from power to dock visible.
2. `maclite-memory` at idle, 5 min after login → must be 150-300 MB, <500 MB.
3. `maclite-performance 5000` at idle → CPU busy must be ~0-2 %.
4. `maclite-ui-benchmark --live` while dragging a window → fps and worst frame.
5. `mpv --hwdec=vdpau <h264 file>` and `--hwdec=vdpau <mpeg2 file>`; check
   `mpv --msg-level=vd=v` shows vdpau, and CPU stays <30 % during 1080p.
6. Wi-Fi: `b43` in dmesg, associate, iperf3 both directions.
7. Suspend/resume, lid-less iMac: display sleep/wake via brightness node.
Each result gets a dated row in the IMAC tier above; until then every claim
about acceleration stays labelled "designed for", not "measured".
