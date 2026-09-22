#!/bin/sh
# MacLiteOS build entry point (spec §54).
# 1) probe the build host, 2) compile everything (C11 + libc only),
# 3) run the test suite, 4) attempt ISO assembly when the tooling exists.
# ISO assembly is OPTIONAL: missing xorriso/grub/mtools must not fail the build,
# and we say so plainly instead of pretending an ISO exists.
set -e
cd "$(dirname "$0")/.."
echo "== MacLiteOS build $(cat VERSION)"
./configure
make -j"$(nproc 2>/dev/null || echo 2)" all
echo
echo "== tests"
sh scripts/run-tests.sh
echo
echo "== ISO"
if command -v xorriso >/dev/null && command -v grub-mkimage >/dev/null && command -v mksquashfs >/dev/null; then
    sh scripts/make-iso.sh
else
    echo "skipped: xorriso/grub-mkimage/mksquashfs not present on this build host."
    echo "The session itself is fully usable:  cd out && ./mica-comp --session"
    echo "(ISO path is implemented in scripts/make-iso.sh and untested here.)"
fi
