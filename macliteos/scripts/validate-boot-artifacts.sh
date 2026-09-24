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

for t in file sha256sum cpio gzip awk sed grep find ldd; do need "$t"; done
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
sh -n "$TMP/initrd/init" || fail "/init has shell syntax errors"
[ -x "$TMP/initrd/bin/busybox" ] || fail "static BusyBox missing"
for required in mica-comp mica-installer g1os-ui-health g1os-failure-ui maclite-installer-backend; do
    [ -x "$TMP/initrd/usr/bin/$required" ] || fail "$required missing from initramfs"
done
[ -d "$TMP/initrd/lib/modules" ] || fail "kernel module tree missing from initramfs"

# shellcheck disable=SC2016
grep -F '/usr/bin/mica-comp --backend "$BACKEND"' "$TMP/initrd/init" >/dev/null || fail "initrd does not launch dedicated graphical compositor path"
grep -F '/usr/bin/mica-installer' "$TMP/initrd/init" >/dev/null || fail "initrd does not launch graphical installer"
grep -F 'g1os-ui-health' "$TMP/initrd/init" >/dev/null || fail "graphical installer health gate is missing"
grep -F 'g1os-failure-ui' "$TMP/initrd/init" >/dev/null || fail "graphical failure UI path is missing"

grep -F 'export XDG_RUNTIME_DIR=' "$TMP/initrd/init" >/dev/null || fail "XDG_RUNTIME_DIR is not initialized"
grep -F 'MICA_GL=off' "$TMP/initrd/init" >/dev/null || fail "Safe Graphics software-rendering mode is not configured"

check_elf() {
    exe="$1"
    [ -x "$exe" ] || return 0
    file "$exe" | grep -Eq 'ELF' || return 0
    interp=$(file "$exe" | sed -n 's/.*interpreter \([^,]*\).*/\1/p')
    if [ -n "$interp" ]; then
        rel=${interp#/}
        [ -e "$TMP/initrd/$rel" ] || fail "ELF interpreter missing for $exe: /$rel"
    fi
    ldd_out=$(ldd "$exe" 2>&1) || fail "ldd could not inspect $exe: $ldd_out"
    if echo "$ldd_out" | grep -q 'not found'; then
        fail "shared-library dependency missing for $exe: $ldd_out"
    fi
    libs=$(echo "$ldd_out" | sed -n -E 's/.*=>[[:space:]]*(\/[^[:space:]]+).*/\1/p; s/^[[:space:]]*(\/[^[:space:]]+)[[:space:]]+\(.*/\1/p')
    for lib in $libs; do
        [ -f "$TMP/initrd$lib" ] || fail "ELF dependency missing from initramfs: $lib (required by $exe)"
    done
}
for exe in "$TMP/initrd"/usr/bin/*; do check_elf "$exe"; done

KERNEL=${G1OS_KERNEL:-$OUT/vmlinuz-maclite}
[ -s "$KERNEL" ] || fail "kernel artifact missing: $KERNEL"
file "$KERNEL" | grep -Eiq 'Linux kernel|boot executable|PE32' || fail "kernel is not a recognized x86 boot executable"

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
    for required in usr/bin/mica-comp usr/bin/mica-installer usr/share/maca-lite/runtime-provenance.sha256; do
        unsquashfs -cat "$TMP/iso/live/maclite-base.sqfs" "$required" >/dev/null 2>&1 || fail "SquashFS missing graphical runtime artifact: $required"
    done
    sha256sum -c "$ISO.sha256" >/dev/null 2>&1 || fail "ISO checksum does not match $ISO.sha256"
fi

echo "VALIDATION: PASS"
echo "INITRD: $INITRD"
echo "INITRD_SHA256: $(sha256sum "$INITRD" | awk '{print $1}')"
echo "KERNEL: $KERNEL"
if [ -s "$ISO" ]; then
    echo "ISO: $ISO"
fi
