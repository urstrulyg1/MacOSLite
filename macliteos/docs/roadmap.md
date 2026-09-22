# Roadmap (ordered by the spec's priorities: HW compat > smoothness > multimedia
# > low RAM > low CPU > fast boot > animations > size)

## v0.1 — done in this tree
- Damage-tracking compositor + WM + supervisor, shm clients, spring animation
  engine with interruptible retargets, 3 performance modes + auto-pick.
- Shell: menu bar with menus + control center, dock with gaussian
  magnification, autohide + reveal-on-approach (dock prefs menu), desktop
  layer, on-demand launcher (Meta+Space), Meta+arrow workspace switching,
  notifications.
- Apps: finder (lazy, inotify, capped search, 8 MB thumb LRU), terminal (VT100
  subset, pty), viewer, settings, sysinfo, textedit, player/music/pdf
  front-ends that delegate to real decoders when present.
- Tools: maclite-memory / -performance / -ui-benchmark / -diagnostics.
- Tests: units, idle, leak, headless session, benchmark; all PASS in sandbox.
- boot/kernel/installer/recovery/scripts scaffolding with honest status.

## v0.2 — hardware layer (in this tree, awaiting metal)
Done and verified here:
- Capability layer + capability DB + backend choosers (docs/architecture.md,
  docs/hardware.md); GPU/display/brightness/audio/video/net/USB/storage/power
  diagnostics with a five-state result vocabulary and an exit-code contract.
- Present backend: real KMS scanout (dumb buffers, SETCRTC, non-blocking page
  flips read from the DRM fd), fbdev fallback, headless; `MS_STATS` carries the
  real backend instead of a hard-coded 0.
- EDID parsing, backlight backend with read-back verification, ALSA UAPI audio
  with no daemon, decode capability intersection with strict hardware
  attribution, `--perf` seek + A/V decode-skew measurement, `--fullscreen`
  worst-case frame measurement.
- Settings > Displays drives the same brightness backend as the CLI tool.
- Tests: hardware fixture matrix (4 machines), five honesty gates, seven scripted
  sessions; `sh scripts/run-tests.sh` = 23 rows, all PASS on this host.
Not done, and not claimed:
- Nothing has run on an iMac or in QEMU yet: KMS scanout, backlight writes,
  ALC889 playback, VDPAU/VA-API decode, tg3/b43 traffic and suspend behaviour are
  all **NOT TESTED** (docs/testing.md).
- GL compositing (r600g GLES2) is still not implemented; the compositor is a CPU
  rasteriser with GPU scanout, and says exactly that.

## v0.3 — the metal tier
- Assemble the ISO on a tooled host (scripts/make-iso.sh), boot the iMac through
  Apple EFI, fix what breaks, and publish the IMAC-tier report produced by
  scripts/hardware-check.sh.
- Installer first-boot chain: squashfs base + data volume + grub install.
- Only after the report exists: any claim of acceleration, decode or audio
  support on this hardware.

## Deliberately not planned
- Indexers, telemetry, cloud sync, background updaters, Electron/JS shells,
  GNOME/KDE/XFCE reuse, Apple assets, AVX-only paths, unlimited caches.
