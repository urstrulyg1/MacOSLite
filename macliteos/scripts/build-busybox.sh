#!/bin/sh
# Build a static BusyBox used only by the G1OS initramfs.
set -eu
cd "$(dirname "$0")/.."

VERSION=${G1OS_BUSYBOX_VERSION:-1.36.1}
URL="https://busybox.net/downloads/busybox-${VERSION}.tar.bz2"
SHA="b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314"
OUT="$PWD/out"
SRC="$OUT/busybox-src"
TARBALL="$OUT/busybox-${VERSION}.tar.bz2"
JOBS=${G1OS_JOBS:-$(nproc 2>/dev/null || echo 2)}

need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required BusyBox-build tool missing: $1" >&2; exit 2; }; }
for tool in curl sha256sum tar bzip2 make gcc; do need "$tool"; done

mkdir -p "$OUT"
if [ ! -s "$TARBALL" ]; then curl -fL --retry 3 --retry-delay 2 -o "$TARBALL" "$URL"; fi
printf '%s  %s\n' "$SHA" "$TARBALL" | sha256sum -c -
rm -rf "$SRC"
mkdir -p "$SRC"
tar -xjf "$TARBALL" -C "$SRC" --strip-components=1

make -C "$SRC" defconfig
"$SRC/scripts/config" --enable CONFIG_STATIC
"$SRC/scripts/config" --disable CONFIG_PIE
"$SRC/scripts/config" --enable CONFIG_CHROOT
make -C "$SRC" olddefconfig
make -C "$SRC" -j"$JOBS"
[ -s "$SRC/busybox" ] || { echo "ERROR: BusyBox build produced no binary" >&2; exit 3; }
mkdir -p "$OUT"
cp "$SRC/busybox" "$OUT/busybox"
chmod 0755 "$OUT/busybox"
echo "BUSYBOX STATUS: PASS"
