# boot/

Boot chain for MacLiteOS on EFI and BIOS machines, including the Apple EFI 1.1
found in iMac11,2 / iMac11,3 (Mid-2010).

Status: **design + config templates, not yet assembled into a bootable ISO.**
The userspace session (compositor + shell + apps) is complete and tested
headless; ISO assembly needs grub/mtools/xorriso on the build host, which the
development sandbox does not have (see docs/TESTING.md for what ran where).

Files:
- `grub-efi.cfg`   GRUB config for EFI boot (Apple EFI 1.1 accepts EFI-x86_64
                   GRUB; keep the kernel and initrd in the ESP-friendly layout).
- `grub-bios.cfg`  legacy/CSM boot config (some iMacs fall back to CSM).
- `initrd.list`    the exact file list the initramfs must contain. Principle:
                   the initrd carries only what mounts the read-only base and
                   starts `mica-comp --session`; everything else is on-disk.
- `kernel-cmdline.txt` documented cmdline, including the quirks that matter on
                   this hardware (b43 firmware, radeon UVD, tg3).

The boot target is a Linux 6.x LTS kernel configured from `kernel/configs/`,
not a custom kernel: MacLiteOS is a userspace OS (compositor, shell, apps,
tools) plus a curated kernel config and boot chain. That choice is deliberate
(spec §2: small, reliable, fast over feature-rich) and is documented in
docs/ARCHITECTURE.md.
