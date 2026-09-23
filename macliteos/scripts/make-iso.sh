#!/bin/sh
# Assemble a fresh, self-contained G1OS ISO. No placeholders are permitted.
set -eu
cd "$(dirname "$0")/.."
VERIFY=0
for arg in "$@"; do
  case "$arg" in
    --verify) VERIFY=1 ;;
    -h|--help) echo "usage: sh scripts/make-iso.sh [--verify]"; exit 0 ;;
    *) echo "ERROR: unknown option: $arg" >&2; exit 2 ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required tool missing: $1" >&2; exit 2; }; }
for tool in xorriso grub-mkimage mksquashfs cpio; do need "$tool"; done

KERNEL="${G1OS_KERNEL:-}"
INITRD="${G1OS_INITRD:-}"
[ -n "$KERNEL" ] || for p in out/vmlinuz-maclite /boot/vmlinuz-maclite /run/maclite-live/boot/vmlinuz-maclite; do [ -s "$p" ] && { KERNEL=$p; break; }; done
[ -n "$INITRD" ] || for p in out/initrd-maclite.img /boot/initrd-maclite.img /run/maclite-live/boot/initrd-maclite.img; do [ -s "$p" ] && { INITRD=$p; break; }; done
[ -n "$KERNEL" ] || { echo "ERROR: no real G1OS kernel image supplied (set G1OS_KERNEL)" >&2; exit 3; }
[ -n "$INITRD" ] || { echo "ERROR: no real G1OS initramfs supplied (set G1OS_INITRD)" >&2; exit 3; }

REQUIRED_BINS="out/mica-comp out/mica-shell out/mica-finder out/mica-terminal out/mica-viewer out/mica-settings out/mica-sysinfo out/mica-textedit out/mica-player out/mica-music out/mica-pdf out/maclite-browser out/maclite-video out/mica-installer"
for bin in $REQUIRED_BINS; do [ -x "$bin" ] || { echo "ERROR: required binary missing: $bin" >&2; exit 4; }; done

ST=out/iso-stage
rm -rf "$ST"
mkdir -p "$ST/boot" "$ST/live" "$ST/base/usr/bin" "$ST/base/usr/share/maca-lite/scripts" "$ST/base/usr/share/maca-lite/catalog"
for bin in $REQUIRED_BINS; do cp "$bin" "$ST/base/usr/bin/"; done
cp installer/maclite-install "$ST/base/usr/bin/"
cp installer/maclite-installer-backend "$ST/base/usr/bin/"
chmod +x "$ST/base/usr/bin/maclite-install" "$ST/base/usr/bin/maclite-installer-backend"
cp scripts/hardware-check.sh "$ST/base/usr/share/maca-lite/scripts/"
chmod +x "$ST/base/usr/share/maca-lite/scripts/hardware-check.sh"
[ -f drivers/catalog/maclite-offline.cat ] && cp drivers/catalog/maclite-offline.cat "$ST/base/usr/share/maca-lite/catalog/"
[ -d rootfs/etc ] && cp -r rootfs/etc "$ST/base/"
[ -d rootfs/usr ] && cp -r rootfs/usr/. "$ST/base/usr/"

mksquashfs "$ST/base" "$ST/live/maclite-base.sqfs" -comp zstd -Xcompression-level 12 -no-progress
cp "$KERNEL" "$ST/boot/vmlinuz-maclite"
cp "$INITRD" "$ST/boot/initrd-maclite.img"

cat > "$ST/boot/grub.cfg" <<'GRUB'
set default=0
set timeout=3
insmod part_gpt
insmod fat
insmod iso9660
insmod linux
insmod search
menuentry "G1OS" {
  linux /boot/vmlinuz-maclite rd.maclite=1
  initrd /boot/initrd-maclite.img
}
menuentry "G1OS Safe Graphics" {
  linux /boot/vmlinuz-maclite rd.maclite=1 maclite.gl=off
  initrd /boot/initrd-maclite.img
}
menuentry "G1OS Recovery" {
  linux /boot/vmlinuz-maclite rd.maclite=recovery
  initrd /boot/initrd-maclite.img
}
GRUB

grub-mkimage -O x86_64-efi -o "$ST/boot/bootx64.efi" -p /boot part_gpt part_msdos fat iso9660 linux search normal

xorriso -as mkisofs -o out/G1OS.iso -e boot/bootx64.efi -no-emul-boot -isohybrid-gpt-basdat "$ST"
[ -s out/G1OS.iso ] || { echo "ERROR: ISO missing or empty" >&2; exit 5; }
sha256sum out/G1OS.iso > out/G1OS.iso.sha256
{
  echo "G1OS ISO Build Manifest"
  echo "Build Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "Kernel: $(basename "$KERNEL")"
  echo "Initramfs: $(basename "$INITRD")"
  echo "Base: live/maclite-base.sqfs"
  echo "ISO: out/G1OS.iso"
  echo "SHA256: $(cat out/G1OS.iso.sha256)"
} > out/iso-manifest.txt

[ -s "$ST/boot/vmlinuz-maclite" ] || { echo "ERROR: staged kernel missing" >&2; exit 6; }
[ -s "$ST/boot/initrd-maclite.img" ] || { echo "ERROR: staged initramfs missing" >&2; exit 6; }
[ -s "$ST/live/maclite-base.sqfs" ] || { echo "ERROR: staged live base missing" >&2; exit 6; }
[ -s "$ST/boot/bootx64.efi" ] || { echo "ERROR: staged EFI loader missing" >&2; exit 6; }
[ -s "$ST/boot/grub.cfg" ] || { echo "ERROR: staged GRUB configuration missing" >&2; exit 6; }

if [ "$VERIFY" = 1 ]; then
  need isoinfo
  ISO_FILES=$(mktemp)
  trap 'rm -f "$ISO_FILES"' EXIT
  isoinfo -i out/G1OS.iso -f > "$ISO_FILES"
  for required in /boot/vmlinuz-maclite /boot/initrd-maclite.img /boot/bootx64.efi /boot/grub.cfg /live/maclite-base.sqfs; do
    grep -F "$required" "$ISO_FILES" >/dev/null || { echo "ERROR: ISO missing $required" >&2; exit 7; }
  done
fi

echo "SUCCESS: fresh G1OS ISO assembled and artifact checks passed: out/G1OS.iso"
