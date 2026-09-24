# G1OS Releases

This directory contains the release builds and bootable images for G1OS (MacLiteOS).

## Current Latest Build

- **ISO Image**: [`G1OS.iso`](./G1OS.iso) (34.2 MB)
- **SHA-256 Checksum**: [`G1OS.iso.sha256`](./G1OS.iso.sha256)
- **Kernel**: [`vmlinuz-maclite`](./vmlinuz-maclite) (Linux 6.12.101 LTS)
- **Initramfs**: [`initrd-maclite.img`](./initrd-maclite.img) (BusyBox 1.36.1)
- **Build Manifest**: [`iso-manifest.txt`](./iso-manifest.txt)
- **File List**: [`iso-file-list.txt`](./iso-file-list.txt)

### Checksum Verification
```text
7FC04B15F7FCC9B6C646ADD99D4D9EC36D811705A91DFF3C6FE505BE930EDE4A  G1OS.iso
```

### Flashing Instructions
- **Windows**: Use [Rufus](https://rufus.ie/) with "Write in DD Image mode".
- **Linux/macOS**:
  ```bash
  sudo dd if=releases/G1OS.iso of=/dev/sdX bs=4M status=progress conv=fsync && sync
  ```
