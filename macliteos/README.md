# MacLiteOS

An ultra-light, macOS-*inspired* desktop operating system for the iMac
Mid-2010 (iMac11,2 / iMac11,3): native C11 compositor, shell and apps, no
X11/GNOME/KDE/Electron, no telemetry, no indexers. Original artwork only —
not affiliated with Apple, no Apple assets.

The React site at the repository root is a **design mockup** and is not part
of the shipping OS; the dock magnification formula in `src/os/Dock.tsx` was
ported into the real dock.

## Quick start (development sandbox, no GPU needed)

    sh scripts/build.sh                 # configure + compile + test suite
    cd out
    ./mica-comp --headless -W 1440 -H 900 --session --script demo.script
    # writes session_*.png screenshots of the live multi-process desktop

Real display output needs KMS (`./mica-comp --drm`) or the ISO
(scripts/make-iso.sh on a tooled host) + scripts/run-vm.sh.

## Layout

    compositor/   comp.c (compositor+WM+supervisor), proto.h, client/ runtime
    core/         ml_* library: loop, ipc, region, raster, font, icon, img, cache
    desktop/      mica-shell: panel / dock / desktop / launcher roles
    finder/ applications/ settings/ multimedia/   the nine-app set
    performance/  maclite-memory|-performance|-ui-benchmark|-diagnostics
    hardware/     probing + mode auto-pick + blend benchmark
    boot/ kernel/ installer/ recovery/ rootfs/ scripts/   OS assembly
    docs/         ARCHITECTURE, HARDWARE, PERFORMANCE, TESTING, ROADMAP

## Status, stated plainly

Sandbox-tested: compositing, animations, IPC, crash respawn, idle CPU/RAM,
the whole app set headless (docs/TESTING.md). NOT yet tested: ISO boot, QEMU,
KMS/GL, VDPAU decode, Wi-Fi/audio/EFI on real iron. Every claim in this repo
carries its tier.
