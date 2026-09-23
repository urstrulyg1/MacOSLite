#!/bin/sh
# Reproducible upstream Linux kernel build for G1OS x86_64.
# Outputs: out/vmlinuz-maclite and out/kernel-modules/.
set -eu
cd "$(dirname "$0")/.."

KVER=${G1OS_KERNEL_VERSION:-6.12.101}
KURL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KVER}.tar.xz"
# SHA-256 published by the Linux Kernel Archives for the pinned source tarball.
KSHA="0d21cd11933f49f7151b7c9dbb8cc3fddc8c8abe506434b850feecf41fc28a76"
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

rm -rf "$SRC" "$OUT/kernel-build" "$OUT/kernel-modules"
mkdir -p "$SRC"
tar -xJf "$TARBALL" -C "$SRC" --strip-components=1

# Start from the upstream x86_64 defconfig, merge the G1OS requirements, then
# let Kconfig resolve dependencies and reject invalid/unknown settings.
make -C "$SRC" O="$OUT/kernel-build" x86_64_defconfig
"$SRC/scripts/kconfig/merge_config.sh" -O "$OUT/kernel-build" -m "$OUT/kernel-build/.config" "$PWD/kernel/g1os-x86_64.fragment"
make -C "$SRC" O="$OUT/kernel-build" olddefconfig

for setting in \
  CONFIG_BLK_DEV_INITRD=y CONFIG_DEVTMPFS=y CONFIG_DEVTMPFS_MOUNT=y \
  CONFIG_EFI=y CONFIG_EFI_STUB=y CONFIG_EFI_PARTITION=y \
  CONFIG_ISO9660_FS=y CONFIG_SQUASHFS=y CONFIG_EXT4_FS=y \
  CONFIG_SATA_AHCI=y CONFIG_USB_STORAGE=y; do
  grep -qx "$setting" "$OUT/kernel-build/.config" || { echo "ERROR: kernel configuration missing $setting" >&2; exit 3; }
done

make -C "$SRC" O="$OUT/kernel-build" -j"$JOBS" bzImage modules
make -C "$SRC" O="$OUT/kernel-build" modules_install INSTALL_MOD_PATH="$OUT/kernel-modules"

cp "$OUT/kernel-build/arch/x86/boot/bzImage" "$OUT/vmlinuz-maclite"
[ -s "$OUT/vmlinuz-maclite" ] || { echo "ERROR: kernel artifact missing or empty" >&2; exit 4; }
[ -d "$OUT/kernel-modules/lib/modules" ] || { echo "ERROR: kernel modules staging missing" >&2; exit 4; }

printf '%s\n' "$KVER" > "$OUT/kernel-version.txt"
printf '%s  %s\n' "$KSHA" "linux-${KVER}.tar.xz" > "$OUT/kernel-source.sha256"

echo "KERNEL STATUS: PASS"
echo "KERNEL: $OUT/vmlinuz-maclite"
echo "MODULES: $OUT/kernel-modules/lib/modules"
