# G1OS Releases

This directory contains verified release builds and bootable images for G1OS (MacLiteOS).

## Current Latest Build

- **ISO Image**: [`G1OS.iso`](./G1OS.iso) (55.9 MB — fully self-contained base system)
- **SHA-256 Checksum**: [`G1OS.iso.sha256`](./G1OS.iso.sha256)
- **Kernel**: [`vmlinuz-maclite`](./vmlinuz-maclite) (Linux 6.12.101 LTS)
- **Initramfs**: [`initrd-maclite.img`](./initrd-maclite.img) (8.3 MB, BusyBox 1.36.1 + dynamic linkers)
- **Build Manifest**: [`iso-manifest.txt`](./iso-manifest.txt)
- **File List**: [`iso-file-list.txt`](./iso-file-list.txt)

### Checksum Verification
```text
1415abf4b480c21dfebe052093a99bda293034935663f9a65f295229e85ddec9  G1OS.iso
```

To verify the image integrity locally:
```bash
sha256sum -c releases/G1OS.iso.sha256
```

### Architecture & Boot Structure
- **Target Hardware**: Apple iMac Mid-2010 (iMac11,2 21.5" & iMac11,3 27"), MacBook Pro, and generic x86_64 UEFI/BIOS systems.
- **Bootloader**: Apple EFI 1.10 FAT partition image (`boot/efi.img` via El Torito catalog) with fallback `BOOTX64.EFI`.
- **Safe Graphics Fallback**: `nomodeset radeon.modeset=0 fbcon=map:0 console=tty0 earlycon reboot=pci panic=0` prevents VBIOS freeze on legacy AMD GPUs.
- **Dynamic Linker & Libraries**: Bundles `/lib64/ld-linux-x86-64.so.2` and GNU C Library (glibc 2.39) for guaranteed userspace compositor and shell execution.
- **Offline Capable**: Includes full live base squashfs (`live/maclite-base.sqfs`), allowing 100% offline installation without network connectivity.

### Flashing Instructions
- **macOS / Linux**:
  ```bash
  sudo dd if=releases/G1OS.iso of=/dev/sdX bs=4M status=progress conv=fsync && sync
  ```
  *(Replace `/dev/sdX` with your target USB flash drive identifier, e.g., `/dev/rdisk2` on macOS)*
- **Windows**: Use [Rufus](https://rufus.ie/) with "Write in DD Image mode", or [balenaEtcher](https://etcher.balena.io/).
