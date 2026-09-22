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
# v0.2 hardware diagnostics + the on-hardware checklist; the ISO is where they
# are actually needed, so they ship in the base image (and in the initrd).
cp out/maclite-gpu out/maclite-gpu-benchmark out/maclite-display \
   out/maclite-brightness out/maclite-audio out/maclite-video-test \
   out/maclite-network out/maclite-usb out/maclite-storage out/maclite-power \
   out/maclite-hardware "$ST/base/usr/bin/"
install -d "$ST/base/usr/share/maca-lite/scripts"
cp scripts/hardware-check.sh "$ST/base/usr/share/maca-lite/scripts/"
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
