#!/bin/sh
# Independent validation of the kernel/initrd/ISO boot chain.
# This script never writes to disks or USB devices.
set -eu

ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
OUT=${G1OS_VALIDATE_OUT:-$ROOT/out}
INITRD=${1:-$OUT/initrd-maclite.img}
ISO=${2:-$OUT/G1OS.iso}

fail() { echo "VALIDATION: FAIL: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "required validation tool missing: $1"; }

for t in file sha256sum cpio gzip awk sed grep find; do need "$t"; done
[ -s "$INITRD" ] || fail "initramfs missing/empty: $INITRD"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/initrd"

gzip -t "$INITRD" || fail "initramfs gzip stream is corrupt"
( cd "$TMP/initrd" && gzip -dc "$INITRD" | cpio -it >/dev/null ) || fail "initramfs cpio archive cannot be listed"
( cd "$TMP/initrd" && gzip -dc "$INITRD" | cpio -idm --no-absolute-filenames >/dev/null 2>&1 ) || fail "initramfs cannot be extracted"

[ -f "$TMP/initrd/init" ] || fail "/init missing from initramfs"
[ -x "$TMP/initrd/init" ] || fail "/init is not executable"
HEAD=$(sed -n '1p' "$TMP/initrd/init")
case "$HEAD" in '#!/bin/sh'|'#!/bin/ash') : ;; *) fail "/init has unsupported interpreter: $HEAD" ;; esac
[ -x "$TMP/initrd/bin/busybox" ] || fail "static BusyBox missing"
[ -x "$TMP/initrd/usr/bin/mica-comp" ] || fail "mica-comp missing from initramfs"
[ -x "$TMP/initrd/usr/bin/mica-installer" ] || fail "mica-installer missing from initramfs"
[ -x "$TMP/initrd/usr/bin/maclite-installer-backend" ] || fail "installer backend missing from initramfs"
[ -d "$TMP/initrd/lib/modules" ] || fail "kernel module tree missing from initramfs"

check_elf() {
    exe="$1"
    [ -x "$exe" ] || return 0
    file "$exe" | grep -Eq 'ELF' || return 0
    interp=$(file "$exe" | sed -n 's/.*interpreter \([^,]*\).*/\1/p')
    if [ -n "$interp" ]; then
        rel=${interp#/}
        [ -e "$TMP/initrd/$rel" ] || fail "ELF interpreter missing for $exe: /$rel"
    fi
}
for exe in "$TMP/initrd"/usr/bin/*; do check_elf "$exe"; done

KERNEL=${G1OS_KERNEL:-$OUT/vmlinuz-maclite}
if [ -s "$KERNEL" ]; then
    file "$KERNEL" | grep -Eiq 'Linux kernel|boot executable|PE32' || fail "kernel is not a recognized x86 boot executable"
fi

if [ -s "$ISO" ]; then
    need xorriso
    need unsquashfs
    sha256sum "$ISO" >/dev/null || fail "cannot hash ISO"
    xorriso -indev "$ISO" -find / -exec report_lba >"$TMP/iso-files" 2>/dev/null || fail "xorriso cannot inspect ISO"
    for required in /boot/vmlinuz-maclite /boot/initrd-maclite.img /boot/grub.cfg /live/maclite-base.sqfs /EFI/BOOT/BOOTX64.EFI; do
        grep -F "$required" "$TMP/iso-files" >/dev/null || fail "ISO missing required path: $required"
    done
    xorriso -osirrox on -indev "$ISO" -extract / "$TMP/iso" >/dev/null 2>&1 || fail "ISO extraction failed"
    [ -s "$TMP/iso/boot/initrd-maclite.img" ] || fail "extracted ISO initramfs missing"
    [ -s "$TMP/iso/boot/vmlinuz-maclite" ] || fail "extracted ISO kernel missing"
    [ -s "$TMP/iso/live/maclite-base.sqfs" ] || fail "extracted ISO SquashFS missing"
    unsquashfs -s "$TMP/iso/live/maclite-base.sqfs" >/dev/null || fail "SquashFS is invalid"
    sha256sum -c "$ISO.sha256" >/dev/null 2>&1 || fail "ISO checksum does not match $ISO.sha256"
fi

echo "VALIDATION: PASS"
echo "INITRD: $INITRD"
echo "INITRD_SHA256: $(sha256sum "$INITRD" | awk '{print $1}')"
[ -s "$KERNEL" ] && echo "KERNEL: $KERNEL"
[ -s "$ISO" ] && echo "ISO: $ISO"
