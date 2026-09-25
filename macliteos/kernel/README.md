# G1OS kernel build

G1OS uses a pinned upstream Linux x86_64 long-term kernel as the hardware enablement layer. The build is reproducible and never creates a placeholder kernel.

## Pinned inputs

- Linux: `6.12.101`
- Source archive: `https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.101.tar.xz`
- Source SHA-256: `0d21cd11933f49f7151b7c9dbb8cc3fddc8c8abe506434b850feecf41fc28a76`
- Configuration: `kernel/g1os-x86_64.fragment` merged into upstream `x86_64_defconfig`
- Required boot/hardware paths include EFI/GPT, AHCI/SATA, USB mass storage, ISO9660, SquashFS, ext4, Radeon DRM, Broadcom TG3/B43 and Intel HDA audio.
- Out-of-tree patches: none currently required.

## Artifact contract

The final kernel artifact is **only** `out/vmlinuz-maclite` produced by `scripts/build-kernel.sh`. The build also produces `out/g1os-kernel-manifest.txt` and `out/g1os-kernel.sha256`. ISO assembly is not allowed to discover a kernel from `/boot`, `releases/`, `/Volumes/G1OS`, or another stale location.

The manifest binds the exact artifact path, x86_64 bzImage format, EFI-stub requirement, kernel version, source hash, config hash, size, and SHA-256. ISO creation consumes this manifest and independently verifies the extracted final ISO copy before publication. The installer consumes the same ISO manifest and never substitutes the running installer kernel.

## Build

From `macliteos/` on Linux:

```sh
sh scripts/build-kernel.sh
sh scripts/build-busybox.sh
G1OS_BUSYBOX="$PWD/out/busybox" \
G1OS_KERNEL_MODULES="$PWD/out/kernel-modules" \
  sh scripts/make-initrd.sh "$PWD/out/initrd-maclite.img"
```

Outputs:

- `out/vmlinuz-maclite` — real Linux kernel image
- `out/kernel-modules/lib/modules/` — kernel modules for the initramfs
- `out/busybox` — static BusyBox for the initramfs
- `out/initrd-maclite.img` — real gzip-compressed initramfs
- `out/kernel-version.txt` and `out/kernel-source.sha256` — provenance metadata

A failed download, checksum, Kconfig step, compilation, module installation or artifact check exits non-zero. No empty or dummy boot artifact is accepted.

## Firmware

Kernel modules such as B43 may require external firmware. The build deliberately does not silently bundle third-party firmware. `make-initrd.sh` accepts `G1OS_FIRMWARE_DIR` as an explicit input and copies it into `/lib/firmware`; the provider/distributor is responsible for applicable licensing and provenance.

```sh
G1OS_FIRMWARE_DIR=/path/to/licensed/firmware \
G1OS_BUSYBOX="$PWD/out/busybox" \
G1OS_KERNEL_MODULES="$PWD/out/kernel-modules" \
  sh scripts/make-initrd.sh "$PWD/out/initrd-maclite.img"
```
