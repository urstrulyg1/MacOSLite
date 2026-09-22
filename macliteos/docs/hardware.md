# Target hardware: iMac Mid-2010 (iMac11,2 / iMac11,3)

| part        | iMac11,2 (21.5")            | iMac11,3 (27")                    |
|-------------|-----------------------------|-----------------------------------|
| CPU         | Core i3-540 / i5-650        | i5-760 / i7-870 (Lynnfield)       |
| ISA         | SSE4.2, **no AVX**          | SSE4.2, **no AVX**                |
| GPU         | ATI Radeon HD 4670 (RV730)  | HD 5670 (Redwood) / HD 5750       |
| Wi-Fi       | Broadcom BCM43224 (**PCIe**) → b43 or broadcom-sta `wl` |
| Ethernet    | Broadcom BCM5764M → tg3     |
| Audio       | Realtek ALC889 → snd-hda-intel |
| Decode      | UVD 2.x → VDPAU (G3DVL) / VA-API: H.264, MPEG-2, VC-1 |
| GL          | r600 gallium (GL3.3 / GLES2 viable for compositing) |
| Boot        | Apple EFI 1.1 → GRUB x86_64-efi (CSM fallback exists) |
| RAM         | 8 GB official (16 GB unofficial) |
| Last macOS  | High Sierra 10.13.6         |

Consequences we design around:
- **No AVX anywhere in the codebase** (spec §6): the rasterizer is scalar C;
  any future SIMD path must keep a scalar fallback and runtime dispatch.
- Wi-Fi is b43/wl, *not* brcmfmac (the BCM43224 is PCIe; brcmfmac is the SDIO/
  USB family). Firmware for b43 must ship in the initrd (boot/initrd.list).
- Video decode is VDPAU/VA-API through r600g UVD; we never claim decode works
  until `mpv --hwdec=vdpau` has been run on the machine (docs/testing.md).
- 60 Hz panel: the compositor targets 16.6 ms frames; software path measured
  below budget even on a 2-core sandbox (docs/performance.md).

## v0.2 additions

The capability layer (`hardware/hwcap.c`) now encodes this table as data: PCI id →
model, kernel driver, Mesa driver, decode mask and decode API, plus the two
platform traps that bit v0.1 thinking —

* **brightness**: `acpi_video0` on these iMacs is a documented no-op; the real
  control is `radeon_bl0` and it only appears with `acpi_backlight=native`, which
  is now in `boot/kernel-cmdline.txt`. `maclite-brightness` flags the useless
  device SUSPECT and refuses to write to it (docs/gpu.md, docs/hardware.md);
* **suspend**: unsolved on this platform (the panel does not re-light). MacLiteOS
  reports it `UNSUPPORTED (disabled by policy)` rather than shipping a power
  button that appears to work (docs/hardware.md).

The depth layer added on top of that table:

* `maclite-cpu` (`hardware/cpu.c`) answers the §1/§6/§7 questions from this
  machine's own `/proc/cpuinfo`, sysfs and firmware root: exact part, topology,
  Turbo Boost, cpufreq governor and available governors, idle states, thermal
  source, microcode revision running versus the revision this image ships, and
  whether any monitoring daemon is running. It refuses to write a governor that
  is not in the machine's own `available_governors`, and refuses to write
  anything at all against fixture roots.
* `maclite-drivers` (`hardware/driver.c` + `drivers/catalog/`) resolves the §4
  chain from the real PCI id and installs only the newest release that still
  *covers this hardware*, with a SHA-256 check per file, a snapshot before each
  write and a rollback that restores it. The policy is docs/drivers.md; the
  catalog is data, so "Mesa 25 is newer but regresses RV730" is one `breaks`
  line rather than a paragraph of prose.
* `maclite-hardware` now prints the §31 section order
  (CPU/GPU/Display/Audio/Network/USB/Storage/Optical/SDXC/FireWire/Camera/
  Multimedia/Power) and keeps `NOT TESTED` distinct from `PASS` in every one of
  them — Optical "Disc", SDXC "Read/write", FireWire "Device test", Camera
  "Capture" and the microcode comparison are all shaped so that an untestable
  check cannot be mistaken for a working one.

Status: **the table above is datasheet/kernel-driver knowledge. The detection
code that consumes it is fixture-verified (`tests/fixtures/make_sysfs.py` builds
faithful iMac11,2 / iMac11,3 trees from recorded `/sys` values), but nothing
here has been measured on a real iMac yet.** Run `scripts/hardware-check.sh` on
the machine and paste `out/hw-report-<date>.tsv` into docs/testing.md; the
per-subsystem state table lives in docs/hardware.md.
