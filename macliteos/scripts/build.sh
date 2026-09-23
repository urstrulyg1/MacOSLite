#!/bin/sh
# G1OS production build entry point.
# A successful build is NOT release-ready unless a fresh ISO is assembled and
# independently verified.
set -eu
cd "$(dirname "$0")/.."

echo "== G1OS build $(cat VERSION)"
./configure
make -j"$(nproc 2>/dev/null || echo 2)" all

echo
echo "== tests"
sh scripts/run-tests.sh

echo
echo "== production ISO"
for tool in xorriso grub-mkimage mksquashfs cpio isoinfo unsquashfs sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required release tool missing: $tool" >&2
        exit 2
    fi
done

sh scripts/make-iso.sh --verify

[ -s out/G1OS.iso ] || { echo "ERROR: fresh ISO missing or empty" >&2; exit 3; }
[ -s out/G1OS.iso.sha256 ] || { echo "ERROR: ISO SHA-256 checksum missing" >&2; exit 3; }
[ -s out/iso-manifest.txt ] || { echo "ERROR: ISO manifest missing" >&2; exit 3; }
sha256sum -c out/G1OS.iso.sha256 >/dev/null || { echo "ERROR: ISO checksum verification failed" >&2; exit 4; }

echo
echo "BUILD STATUS: PASS"
echo "ISO STATUS: PASS"
echo "NOTE: Physical iMac boot/install validation is still required before declaring installation readiness."
