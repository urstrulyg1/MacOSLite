# Roadmap (ordered by the spec's priorities: HW compat > smoothness > multimedia
# > low RAM > low CPU > fast boot > animations > size)

## v0.1 — done in this tree
- Damage-tracking compositor + WM + supervisor, shm clients, spring animation
  engine with interruptible retargets, 3 performance modes + auto-pick.
- Shell: menu bar with menus + control center, magnifying autohide-capable
  dock, desktop layer, on-demand launcher (Meta+Space), notifications.
- Apps: finder (lazy, inotify, capped search, 8 MB thumb LRU), terminal (VT100
  subset, pty), viewer, settings, sysinfo, textedit, player/music/pdf
  front-ends that delegate to real decoders when present.
- Tools: maclite-memory / -performance / -ui-benchmark / -diagnostics.
- Tests: units, idle, leak, headless session, benchmark; all PASS in sandbox.
- boot/kernel/installer/recovery/scripts scaffolding with honest status.

## v0.2 — bootable
- Assemble the ISO on a tooled host (scripts/make-iso.sh), boot in QEMU with
  virtio-gpu, fix what breaks, publish QEMU-tier results.
- GL compositor backend (r600g GLES2) behind maclite.gl=on, software fallback
  automatic; frame pacing from KMS vblank instead of timerfd.
- Installer first-boot chain: squashfs base + data volume + grub install.

## v0.3 — multimedia on metal
- VDPAU/VA-API path validated on the iMac; music via ALSA mix in-process
  (no daemon) or delegate to pipewire-free `aplay`.
- Real-hardware measurement campaign (docs/TESTING.md IMAC tier).

## Deliberately not planned
- Indexers, telemetry, cloud sync, background updaters, Electron/JS shells,
  GNOME/KDE/XFCE reuse, Apple assets, AVX-only paths, unlimited caches.
