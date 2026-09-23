#!/bin/sh
# Reproducible upstream Linux kernel build for G1OS x86_64.
# Outputs: out/vmlinuz-maclite and out/kernel-modules/.
set -eu
cd "$(dirname "$0")/.."

KVER=${G1OS_KERNEL_VERSION:-6.12.101}
KURL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KVER}.tar.xz"
# SHA-256 published by the Linux Kernel Archives for the pinned source tarball.
KSHA="7d2e1b5d5ab36b3a01856e71782dad2a54e634fb2b37c0a42998def3bbf957c1"
OUT="$PWD/out"
SRC="$OUT/kernel-src"
TARBALL="$OUT/linux-${KVER}.tar.xz"
JOBS=${G1OS_JOBS:-$(nproc 2>/dev/null || echo 2)}

need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: required kernel-build tool missing: $1" >&2; exit 2; }; }
for tool in curl sha256sum tar xz make gcc bison flex bc perl; do need "$tool"; done

mkdir -p "$OUT"
if [ ! -s "$TARBALL" ]; then
  echo "== downloading Linux ${KVER}"
  curl -fL --retry 3 --retry-delay 2 -o "$TARBALL" "$KURL"
fi
printf '%s  %s\n' "$KSHA" "$TARBALL" | sha256sum -c -

rm -rf "$SRC"
mkdir -p "$SRC"
tar -xJf "$TARBALL" -C "$SRC" --strip-components=1

# Start from upstream x86_64 defconfig, then apply the small G1OS fragment.
make -C "$SRC" O="$OUT/kernel-build" x86_64_defconfig
scripts_config="$SRC/scripts/config"
[ -x "$scripts_config" ] || chmod +x "$scripts_config"
# Kconfig fragment is fed through KCONFIG_ALLCONFIG so dependency resolution is
# performed by the kernel's own Kconfig machinery rather than text substitution.
KCONFIG_ALLCONFIG="$PWD/kernel/g1os-x86_64.fragment" make -C "$SRC" O="$OUT/kernel-build" allnoconfig
make -C "$SRC" O="$OUT/kernel-build" olddefconfig

# Required boot-path settings must resolve to the requested values.
for setting in \
  CONFIG_BLK_DEV_INITRD=y CONFIG_DEVTMPFS=y CONFIG_DEVTMPFS_MOUNT=y \
  CONFIG_EFI=y CONFIG_EFI_STUB=y CONFIG_EFI_PARTITION=y \
  CONFIG_ISO9660_FS=y CONFIG_SQUASHFS=y CONFIG_EXT4_FS=y \
  CONFIG_SATA_AHCI=y CONFIG_USB_STORAGE=y; do
  grep -qx "$setting" "$OUT/kernel-build/.config" || { echo "ERROR: kernel configuration missing $setting" >&2; exit 3; }
done

make -C "$SRC" O="$OUT/kernel-build" -j"$JOBS" bzImage modules
rm -rf "$OUT/kernel-modules"
make -C "$SRC" O="$OUT/kernel-build" modules_install INSTALL_MOD_PATH="$OUT/kernel-modules"

cp "$OUT/kernel-build/arch/x86/boot/bzImage" "$OUT/vmlinuz-maclite"
[ -s "$OUT/vmlinuz-maclite" ] || { echo "ERROR: kernel artifact missing or empty" >&2; exit 4; }
[ -d "$OUT/kernel-modules/lib/modules" ] || { echo "ERROR: kernel modules staging missing" >&2; exit 4; }

printf '%s\n' "$KVER" > "$OUT/kernel-version.txt"
printf '%s  %s\n' "$KSHA" "linux-${KVER}.tar.xz" > "$OUT/kernel-source.sha256"

echo "KERNEL STATUS: PASS"
echo "KERNEL: $OUT/vmlinuz-maclite"
echo "MODULES: $OUT/kernel-modules/lib/modules"
