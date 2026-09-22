#!/bin/sh
# Run MacLiteOS in QEMU (spec §54). NOTE: the development sandbox has no
# /dev/kvm and no qemu binary, so this script is WRITTEN BUT UNTESTED there;
# on a machine with qemu it boots the ISO with virtio-gl off (software path).
set -e
cd "$(dirname "$0")/.."
ISO=${1:-out/MacLiteOS.iso}
[ -f "$ISO" ] || { echo "no ISO at $ISO — run scripts/build.sh first (needs xorriso/grub)"; exit 1; }
MEM=${MEM:-1024}          # prove the 150-300 MB idle budget with room to spare
command -v qemu-system-x86_64 >/dev/null || { echo "qemu-system-x86_64 not installed"; exit 1; }
exec qemu-system-x86_64 \
    -machine q35 -cpu host -smp 2 -m "$MEM" \
    -cdrom "$ISO" -boot d \
    -device virtio-gpu-pci -display gtk,gl=off \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -serial mon:stdio
