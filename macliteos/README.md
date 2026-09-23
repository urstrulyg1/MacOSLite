# G1OS — Giving life to older machines.

An ultra-light, classic macOS-inspired desktop operating system engineered for the iMac
Mid-2010 (iMac11,2 / iMac11,3): native C11 compositor, shell and apps, no
X11/GNOME/KDE/Electron, no telemetry, no indexers. Original artwork only —
not affiliated with Apple, no Apple assets.

The React app in `src/` provides an interactive desktop simulator reproducing the full
G1OS experience: boot sequence (JeevanOS → G1OS), login, desktop, dock, Safari browser,
multimedia, system dialogs, and setup wizard.

## USB Boot & Installation (iMac Mid-2010)

For the complete, step-by-step walkthrough on preparing a USB drive with Rufus or BalenaEtcher, booting the Apple EFI manager with the `Option` (⌥) key, and installing to an internal SSD/HDD, see the root [README.md](file:///c:/Users/jeeva/Desktop/Jeevan/MacOSLite/README.md).

Quick summary:
1. **Flash ISO**: Write `out/G1OS.iso` to a USB flash drive (in **DD Image mode** if using Rufus).
2. **Boot**: Insert USB into rear port, power on iMac while holding `Option` (⌥), select **EFI Boot**.
3. **Install via G1OS GUI Wizard**: Double-click **“Install G1OS”** on the live desktop or Dock.
   - **Welcome**: "Welcome to G1OS" · "Giving life to older machines."
   - **Disk Detection**: Auto-detects internal SATA SSD/HDD, excludes and protects live USB media.
   - **Interactive Confirmation**: Displays target model, size, devnode, erasure warning, and confirmation checkbox.
   - **Installation Stages**: Preparing → Detecting Disk → Partitioning → Formatting → Installing → Configuring Boot → Finalizing.
   - **UUID Boot Binding**: Binds GRUB and `/etc/fstab` to unique filesystem UUIDs (no live USB references).
   - **Apple EFI 1.1 Compatibility**: Self-contained fallback loader at `/EFI/BOOT/BOOTX64.EFI` + Apple `.disk_label`.
   - **Offline Pre-Flight Verification**: Validates internal EFI loader, UUID bindings, kernel, and initramfs.
   - **Complete & First Boot**: Shows "MacLiteOS Installation Complete — Remove USB and Restart". Unplug USB and click **Restart**.
4. **Fallback CLI**: For automated or headless environments, `sudo maclite-install` delegates to the same installer engine.
5. **Fan Control**: Run `maclite-fan --quiet` to regulate fans after an SSD upgrade.

## Quick start (development sandbox, no GPU needed)

    sh scripts/build.sh                 # configure + compile + test suite
    cd out
    ./mica-comp --headless -W 1440 -H 900 --session --script demo.script
    # writes session_*.png screenshots of the live multi-process desktop

Real display output picks a backend itself:

    ./mica-comp --backend auto      # kms -> fbdev -> headless, whichever works
    ./mica-comp --backend kms       # fail loudly if there is no DRM card
    ./maclite-hardware              # one report for the whole machine

## Hardware diagnostics (v0.2)

Every tool below reports PASS / FAIL / UNSUPPORTED / PARTIAL / NOT TESTED and
returns 0 / 1 / 3 / 2 / 2 — a check that could not be exercised is **never**
printed as PASS:

    maclite-hardware                 unified report (--full, --tsv)
    maclite-gpu                      GPU, driver, DRM nodes, real GL query, live backend
    maclite-gpu-benchmark            frame pacing (--fullscreen, --live, --windows N)
    maclite-display                  EDID, modes (--modes), verified mode set (--set WxH)
    maclite-brightness               get/set/up/down/list/status — read-back verified
    maclite-audio                    list/volume/mute/test — real tone, no sound server
    maclite-video-test               per-codec hardware vs software decode (--perf)
    maclite-network                  link, address, DNS, TCP (--up/--down/--host)
    maclite-usb                      devices, HID classes, hotplug watch (--watch N)
    maclite-storage                  disks, space, throughput (--bench), unmount
    maclite-power                    suspend policy, display sleep/wake

On the target machine run the checklist instead of the individual tools:

    sh scripts/hardware-check.sh     # automated rows + human rows + a report file

## Layout

    compositor/   comp.c (compositor+WM+supervisor), proto.h, client/ runtime
    core/         ml_* library: loop, ipc, region, raster, font, icon, img, cache
    desktop/      mica-shell: panel / dock / desktop / launcher roles
    finder/ applications/ settings/ multimedia/   the nine-app set
    performance/  maclite-memory|-performance|-ui-benchmark|-diagnostics
    hardware/     capability DB, probes, EDID, backlight, audio, media, DRM/KMS
    diagnostics/  the maclite-* hardware tools
    tests/        units, idle, leak, hardware fixtures + scripted sessions
    boot/ kernel/ installer/ recovery/ rootfs/ scripts/   OS assembly
    docs/         hardware, gpu, audio, multimedia, testing, validation-report,
                  ARCHITECTURE, HARDWARE, PERFORMANCE, ROADMAP

## Status, stated plainly

**Measured in this sandbox (SANDBOX tier):** compositing cost and frame pacing
(damage-only 1.12 ms, worst-case full screen 4.39 ms at 1080p — `--fullscreen`),
idle CPU 0.2 %, desktop RSS 47 MB / PSS 36 MB, the whole app set headless, seven
scripted sessions, and the hardware detection code against four fixture machines
(iMac11,2, iMac11,3, a VM, a bare host) with five honesty gates.

**NOT tested (no hardware in this container, no qemu, no /dev/dri, no
/proc/asound):** KMS scanout, backlight writes, ALC889 playback, VDPAU/VA-API
decode, tg3/b43 traffic, EFI boot, suspend/resume. `maclite-video-test` on this
host says *"H.264: ATI Radeon HD 4670 (RV730) has it in silicon but no vdpauinfo
query ran here — NOT TESTED"* and exits 3; that is the intended behaviour.

**Do not read this repository as a claim that acceleration works on an iMac.**
Run `scripts/hardware-check.sh` on the machine, paste `out/hw-report-<date>.tsv`
into docs/testing.md, and only then does a hardware claim exist. Every number and
every claim in the docs carries its tier.
