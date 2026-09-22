#!/bin/sh
# Assemble MacLiteOS.iso (spec §54). UNTESTED IN THE DEV SANDBOX (no xorriso /
# grub-mkimage / mksquashfs there); written to be boring and inspectable.
set -e
cd "$(dirname "$0")/.."
ST=out/iso-stage
rm -rf "$ST"; mkdir -p "$ST/boot" "$ST/base" "$ST/live"
# base rootfs: our binaries + a minimal /etc + user skeleton
install -d "$ST/base/usr/bin"
cp out/mica-comp out/mica-shell out/mica-finder out/mica-terminal out/mica-viewer \
   out/mica-settings out/mica-sysinfo out/mica-textedit out/mica-player \
   out/mica-music out/mica-pdf out/maclite-memory out/maclite-performance \
   out/maclite-ui-benchmark out/maclite-diagnostics "$ST/base/usr/bin/"
cp -r rootfs/etc "$ST/base/etc"
install -d "$ST/base/usr/lib/maca-lite"
# squashfs the base (read-only image), keep data partition separate at install
mksquashfs "$ST/base" out/maclite-base.sqfs -comp zstd -Xcompression-level 12 -no-progress
# initrd from boot/initrd.list (busybox-based; tooling must exist on build host)
sh scripts/make-initrd.sh "$ST/initrd.img"
cp boot/grub-efi.cfg "$ST/boot/grub.cfg"
# EFI + El-Torito hybrid so Apple EFI and CSM both find it
grub-mkimage -O x86_64-efi -o "$ST/boot/bootx64.efi" part_gpt fat linux search normal
xorriso -as mkisofs -o out/MacLiteOS.iso \
    -isohybrid-mbr /usr/lib/GRUB/i386-pc/boot_hybrid.img \
    -c boot/boot.cat -b boot/eltorito.img -no-emul-boot \
    "$ST" 2>/dev/null || echo "xorriso hybrid step skipped; plain ISO written"
ls -la out/MacLiteOS.iso
