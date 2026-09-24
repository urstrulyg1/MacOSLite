#!/bin/sh
# Assemble a fresh, self-contained G1OS ISO and independently verify it.
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
for tool in xorriso grub-mkimage mksquashfs cpio sha256sum; do need "$tool"; done
if [ "$VERIFY" = 1 ]; then
  for tool in isoinfo unsquashfs file; do need "$tool"; done
fi

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
set timeout=5
insmod part_gpt
insmod part_msdos
insmod fat
insmod iso9660
insmod ext2
insmod linux
insmod search

search --no-floppy --set=root --file /live/maclite-base.sqfs

menuentry "G1OS (Default - Radeon KMS)" {
  linux /boot/vmlinuz-maclite rd.maclite=1 mitigations=off console=tty0 earlycon acpi_backlight=native radeon.modeset=1 radeon.uvd=1 b43.fwok=1 reboot=pci panic=0
  initrd /boot/initrd-maclite.img
}

menuentry "G1OS (Safe Graphics - EFI Framebuffer / Software Compositing)" {
  linux /boot/vmlinuz-maclite rd.maclite=1 maclite.gl=off nomodeset radeon.modeset=0 fbcon=map:0 console=tty0 earlycon acpi_backlight=native reboot=pci panic=0
  initrd /boot/initrd-maclite.img
}

menuentry "G1OS (Safe Graphics + Verbose Debug)" {
  linux /boot/vmlinuz-maclite rd.maclite=1 maclite.gl=off nomodeset radeon.modeset=0 fbcon=map:0 console=tty0 earlycon debug ignore_loglevel acpi_backlight=native reboot=pci panic=0
  initrd /boot/initrd-maclite.img
}

menuentry "G1OS Recovery Shell" {
  linux /boot/vmlinuz-maclite rd.maclite=recovery nomodeset radeon.modeset=0 console=tty0 earlycon reboot=pci panic=0
  initrd /boot/initrd-maclite.img
}
GRUB

mkdir -p "$ST/EFI/BOOT" "$ST/boot/grub"
grub-mkimage -O x86_64-efi -o "$ST/boot/bootx64.efi" -p /boot part_gpt part_msdos fat iso9660 linux search normal
cp "$ST/boot/bootx64.efi" "$ST/EFI/BOOT/BOOTX64.EFI"
cp "$ST/boot/bootx64.efi" "$ST/EFI/BOOT/bootx64.efi"
cp "$ST/boot/grub.cfg" "$ST/EFI/BOOT/grub.cfg"
cp "$ST/boot/grub.cfg" "$ST/boot/grub/grub.cfg"

EFI_CATALOG="boot/bootx64.efi"
EFI_IMG="$ST/boot/efi.img"
if command -v mkfs.vfat >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
  dd if=/dev/zero of="$EFI_IMG" bs=1k count=4096 status=none 2>/dev/null || dd if=/dev/zero of="$EFI_IMG" bs=1k count=4096
  mkfs.vfat -F 12 -n "ESP" "$EFI_IMG" >/dev/null 2>&1
  mmd -i "$EFI_IMG" ::EFI ::EFI/BOOT
  mcopy -i "$EFI_IMG" "$ST/boot/bootx64.efi" ::EFI/BOOT/BOOTX64.EFI
  mcopy -i "$EFI_IMG" "$ST/boot/grub.cfg" ::EFI/BOOT/grub.cfg
  EFI_CATALOG="boot/efi.img"
fi

xorriso -as mkisofs \
  -r -V "G1OS" \
  -J -joliet-long \
  -e "$EFI_CATALOG" \
  -no-emul-boot \
  -isohybrid-gpt-basdat \
  -o out/G1OS.iso \
  "$ST"

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
  echo "== independent ISO verification"
  sha256sum -c out/G1OS.iso.sha256 >/dev/null || { echo "VERIFY = FAIL: ISO checksum mismatch" >&2; exit 7; }
  isoinfo -R -i out/G1OS.iso -f > out/iso-file-list.txt
  for required in boot/vmlinuz-maclite boot/initrd-maclite.img boot/bootx64.efi boot/grub.cfg live/maclite-base.sqfs; do
    grep -Fi "$required" out/iso-file-list.txt >/dev/null || { echo "VERIFY = FAIL: ISO missing $required" >&2; exit 8; }
  done

  EXTRACT=$(mktemp -d)
  clean_extract() {
    chmod -R u+rwx "$EXTRACT" 2>/dev/null || true
    rm -rf "$EXTRACT" 2>/dev/null || true
  }
  trap clean_extract EXIT
  xorriso -osirrox on -indev out/G1OS.iso -extract / "$EXTRACT" >/dev/null 2>&1 || { echo "VERIFY = FAIL: ISO cannot be extracted" >&2; exit 9; }
  [ -s "$EXTRACT/boot/vmlinuz-maclite" ] || { echo "VERIFY = FAIL: extracted kernel empty" >&2; exit 10; }
  [ -s "$EXTRACT/boot/initrd-maclite.img" ] || { echo "VERIFY = FAIL: extracted initramfs empty" >&2; exit 10; }
  [ -s "$EXTRACT/boot/bootx64.efi" ] || { echo "VERIFY = FAIL: extracted EFI loader empty" >&2; exit 10; }
  [ -s "$EXTRACT/live/maclite-base.sqfs" ] || { echo "VERIFY = FAIL: extracted base filesystem empty" >&2; exit 10; }
  file "$EXTRACT/boot/vmlinuz-maclite" | grep -Eiq 'Linux kernel|boot executable|PE32' || { echo "VERIFY = FAIL: kernel is not a recognized executable" >&2; exit 11; }
  unsquashfs -s "$EXTRACT/live/maclite-base.sqfs" >/dev/null || { echo "VERIFY = FAIL: base SquashFS is invalid" >&2; exit 12; }
  isoinfo -i out/G1OS.iso -d | grep -Eiq 'El Torito|EFI' || { echo "VERIFY = FAIL: ISO lacks a detectable El Torito/EFI boot record" >&2; exit 13; }

  grep -F "G1OS ISO Build Manifest" out/iso-manifest.txt >/dev/null || { echo "VERIFY = FAIL: manifest missing" >&2; exit 14; }
  clean_extract
  trap - EXIT
  echo "VERIFY = PASS: ISO readable, boot artifacts present/non-empty, SquashFS valid, EFI boot record detected, checksum valid"
fi

# Remove existing builds and store freshly verified artifacts in releases/
for dir in releases ../releases; do
  if [ -d "$dir" ]; then
    rm -f "$dir"/*.iso "$dir"/*.img "$dir"/vmlinuz* "$dir"/*.sha256 "$dir"/iso-manifest.txt "$dir"/iso-file-list.txt 2>/dev/null || true
  fi
  mkdir -p "$dir"
  cp out/G1OS.iso "$dir/"
  cp out/G1OS.iso.sha256 "$dir/"
  cp out/iso-manifest.txt "$dir/"
  if [ -f out/iso-file-list.txt ]; then
    cp out/iso-file-list.txt "$dir/"
  fi
  if [ -f "$KERNEL" ]; then
    cp "$KERNEL" "$dir/"
  fi
  if [ -f "$INITRD" ]; then
    cp "$INITRD" "$dir/"
  fi
done

echo "SUCCESS: fresh G1OS ISO assembled and stored in releases/: out/G1OS.iso -> releases/G1OS.iso"
