#!/bin/sh
# G1OS production build entry point.
# A successful build is NOT a release-ready installation artifact unless a
# fresh ISO is assembled and its contents are verified.
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
# Installation-readiness policy: never silently skip ISO creation. Missing
# ISO tooling is a hard failure because the physical-install path cannot be
# considered verified without a fresh artifact.
for tool in xorriso grub-mkimage mksquashfs; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required ISO tool missing: $tool" >&2
        exit 2
    fi
done

sh scripts/make-iso.sh --verify

[ -s out/MacLiteOS.iso ] || { echo "ERROR: fresh ISO missing or empty" >&2; exit 3; }
[ -s out/MacLiteOS.iso.sha256 ] || { echo "ERROR: ISO SHA-256 checksum missing" >&2; exit 3; }
[ -s out/iso-manifest.txt ] || { echo "ERROR: ISO manifest missing" >&2; exit 3; }

echo
echo "BUILD STATUS: PASS"
echo "ISO STATUS: PASS"
echo "NOTE: Physical iMac boot/install validation is still required before declaring installation readiness."
