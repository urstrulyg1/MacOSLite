#!/bin/sh
# G1OS production build entry point.
# A successful build is NOT release-ready unless a fresh ISO and its embedded
# boot artifacts pass independent validation.
set -eu
cd "$(dirname "$0")/.."

JOBS=${G1OS_JOBS:-$(nproc 2>/dev/null || echo 2)}
export G1OS_JOBS="$JOBS"

echo "== G1OS build $(cat VERSION)"
./configure
make -j"$JOBS" all

echo
echo "== kernel"
sh scripts/build-kernel.sh

echo
echo "== initramfs"
sh scripts/build-busybox.sh
G1OS_BUSYBOX="$PWD/out/busybox" \
G1OS_KERNEL_MODULES="$PWD/out/kernel-modules" \
  sh scripts/make-initrd.sh "$PWD/out/initrd-maclite.img"
[ -s out/initrd-maclite.img ] || { echo "ERROR: initramfs build failed or output is empty" >&2; exit 3; }
sh scripts/validate-boot-artifacts.sh "$PWD/out/initrd-maclite.img"

echo
echo "== tests"
sh scripts/run-tests.sh

echo
echo "== production ISO"
for tool in xorriso mksquashfs cpio unsquashfs sha256sum file; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required release/validation tool missing: $tool" >&2
        exit 4
    fi
done

G1OS_KERNEL="$PWD/out/vmlinuz-maclite" \
G1OS_INITRD="$PWD/out/initrd-maclite.img" \
  sh scripts/make-iso.sh --verify

[ -s out/G1OS.iso ] || { echo "ERROR: fresh ISO missing or empty" >&2; exit 5; }
[ -s out/G1OS.iso.sha256 ] || { echo "ERROR: ISO SHA-256 checksum missing" >&2; exit 5; }
[ -s out/iso-manifest.txt ] || { echo "ERROR: ISO manifest missing" >&2; exit 5; }
sha256sum -c out/G1OS.iso.sha256 >/dev/null || { echo "ERROR: ISO checksum verification failed" >&2; exit 6; }

# This is intentionally a second, independent pass after ISO assembly. It
# extracts the actual ISO, re-extracts the actual initrd and checks the files
# that the firmware/kernel/userspace path depends on.
sh scripts/validate-boot-artifacts.sh "$PWD/out/initrd-maclite.img" "$PWD/out/G1OS.iso"

echo
echo "BUILD STATUS: PASS"
echo "KERNEL STATUS: PASS"
echo "INITRAMFS STATUS: PASS"
echo "ISO STATUS: PASS"
echo "BOOT ARTIFACT VALIDATION: PASS"
echo "NOTE: Physical iMac boot/install validation is still required before declaring installation readiness."
