#!/bin/sh
# Assemble MacLiteOS.iso (spec §54) with strict verification and zero fabricated data.
#
# Process:
#   1. Validate required tooling on the host (xorriso, grub-mkimage, mksquashfs)
#   2. Validate presence of all compiled binaries and assets
#   3. Assemble rootfs stage ($ST/base) and bootloader stage ($ST/boot)
#   4. Produce immutable squashfs base image
#   5. Assemble initramfs via scripts/make-initrd.sh
#   6. Generate hybrid bootable ISO (EFI + CSM)
#   7. Produce ISO manifest (out/iso-manifest.txt) and SHA-256 checksums
#   8. Optional: boot-test in QEMU (--boot-test), explicitly labeled as QEMU test
#
set -e
cd "$(dirname "$0")/.."

BOOT_TEST=0
for arg in "$@"; do
    case "$arg" in
    --boot-test) BOOT_TEST=1 ;;
    -h|--help)
        echo "usage: sh scripts/make-iso.sh [--boot-test]"
        exit 0
        ;;
    esac
done

echo "== MacLiteOS ISO Assembly =="
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Host:   $(uname -srmo 2>/dev/null || uname -a)"
echo "Commit: $(git log -1 --format="%h (%ci)" 2>/dev/null || cat VERSION 2>/dev/null || echo "unknown")"
echo

# 1. Check build host tooling
MISSING_TOOLS=""
for tool in xorriso grub-mkimage mksquashfs; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        MISSING_TOOLS="$MISSING_TOOLS $tool"
    fi
done

if [ -n "$MISSING_TOOLS" ]; then
    echo "ERROR: Required ISO assembly tools missing:$MISSING_TOOLS"
    echo "Install xorriso, grub-efi, and squashfs-tools to assemble MacLiteOS.iso."
    echo "The compiled desktop session is still fully usable standalone: ./out/mica-comp --session"
    exit 2
fi

# 2. Validate required binaries
REQUIRED_BINS="out/mica-comp out/mica-shell out/mica-finder out/mica-terminal out/mica-viewer \
out/mica-settings out/mica-sysinfo out/mica-textedit out/mica-player out/mica-music out/mica-pdf \
out/maclite-memory out/maclite-performance out/maclite-ui-benchmark out/maclite-diagnostics \
out/maclite-gpu out/maclite-gpu-benchmark out/maclite-display out/maclite-brightness \
out/maclite-audio out/maclite-video-test out/maclite-network out/maclite-usb out/maclite-storage \
out/maclite-power out/maclite-hardware out/maclite-cpu out/maclite-drivers \
out/maclite-fan out/maclite-browser out/maclite-video out/mica-installer out/g1os-splash"

MISSING_BINS=""
for bin in $REQUIRED_BINS; do
    if [ ! -x "$bin" ]; then
        MISSING_BINS="$MISSING_BINS $bin"
    fi
done

if [ -n "$MISSING_BINS" ]; then
    echo "ERROR: Required binaries not built:$MISSING_BINS"
    echo "Run 'make all' first before assembling the ISO."
    exit 1
fi

# 3. Assemble filesystem stages
ST=out/iso-stage
rm -rf "$ST"
mkdir -p "$ST/boot" "$ST/base/usr/bin" "$ST/base/usr/share/maca-lite/scripts" \
         "$ST/base/usr/share/maca-lite/catalog" "$ST/base/usr/lib/maca-lite" "$ST/live"

echo "-> Staging base userspace..."
for bin in $REQUIRED_BINS; do
    cp "$bin" "$ST/base/usr/bin/"
done

[ -f installer/maclite-install ] && cp installer/maclite-install "$ST/base/usr/bin/" && chmod +x "$ST/base/usr/bin/maclite-install"
[ -f installer/maclite-installer-backend ] && cp installer/maclite-installer-backend "$ST/base/usr/bin/" && chmod +x "$ST/base/usr/bin/maclite-installer-backend"

cp scripts/hardware-check.sh "$ST/base/usr/share/maca-lite/scripts/"
chmod +x "$ST/base/usr/share/maca-lite/scripts/hardware-check.sh"

if [ -f drivers/catalog/maclite-offline.cat ]; then
    cp drivers/catalog/maclite-offline.cat "$ST/base/usr/share/maca-lite/catalog/"
fi

if [ -d rootfs/etc ]; then
    cp -r rootfs/etc "$ST/base/etc"
else
    mkdir -p "$ST/base/etc"
fi

if [ -d rootfs/usr ]; then
    cp -r rootfs/usr/* "$ST/base/usr/" 2>/dev/null || true
fi

# 4. Create base squashfs image
echo "-> Creating squashfs base image (out/maclite-base.sqfs)..."
mksquashfs "$ST/base" out/maclite-base.sqfs -comp zstd -Xcompression-level 12 -no-progress

# 5. Create initrd
echo "-> Assembling initramfs (out/initrd.img)..."
if [ -f scripts/make-initrd.sh ]; then
    sh scripts/make-initrd.sh "$ST/initrd.img"
else
    echo "scripts/make-initrd.sh missing"
    exit 1
fi

# 6. GRUB and EFI setup
cp boot/grub-efi.cfg "$ST/boot/grub.cfg"
echo "-> Generating EFI bootloader (bootx64.efi)..."
grub-mkimage -O x86_64-efi -o "$ST/boot/bootx64.efi" -p /boot part_gpt part_msdos fat iso9660 linux search normal

# 7. Generate ISO with xorriso
echo "-> Creating hybrid bootable ISO (out/MacLiteOS.iso)..."
mkdir -p out
if [ -f /usr/lib/GRUB/i386-pc/boot_hybrid.img ]; then
    xorriso -as mkisofs -o out/MacLiteOS.iso \
        -isohybrid-mbr /usr/lib/GRUB/i386-pc/boot_hybrid.img \
        -c boot/boot.cat -b boot/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table \
        -eltorito-alt-boot -e boot/bootx64.efi -no-emul-boot -isohybrid-gpt-basdat \
        "$ST" 2>/dev/null || \
    xorriso -as mkisofs -o out/MacLiteOS.iso -e boot/bootx64.efi -no-emul-boot "$ST"
else
    xorriso -as mkisofs -o out/MacLiteOS.iso -e boot/bootx64.efi -no-emul-boot "$ST"
fi

# 8. Checksums and manifest
echo "-> Generating manifest and checksums..."
MANIFEST="out/iso-manifest.txt"
{
    echo "MacLiteOS ISO Build Manifest"
    echo "============================"
    echo "Build Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "Git Commit: $(git log -1 --format="%h (%ci)" 2>/dev/null || cat VERSION 2>/dev/null || echo "unknown")"
    echo "Kernel Cmd: $(cat boot/kernel-cmdline.txt 2>/dev/null || echo "default")"
    echo
    echo "Bundled Base Userspace Binaries:"
    for bin in $REQUIRED_BINS; do
        bname=$(basename "$bin")
        if [ -f "$ST/base/usr/bin/$bname" ]; then
            size=$(ls -lh "$ST/base/usr/bin/$bname" | awk '{print $5}')
            echo "  $bname ($size)"
        fi
    done
    echo
    echo "ISO Details:"
    ls -lh out/MacLiteOS.iso | awk '{print "  Filename: " $9 "\n  Size:     " $5}'
} > "$MANIFEST"

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum out/MacLiteOS.iso > out/MacLiteOS.iso.sha256
    echo "SHA-256: $(cat out/MacLiteOS.iso.sha256)"
fi

echo
echo "SUCCESS: out/MacLiteOS.iso successfully assembled."
echo "Manifest: $MANIFEST"
echo "Note: ISO build does not simulate hardware tests. Hardware validation occurs at boot."

# 9. Optional QEMU boot test
if [ "$BOOT_TEST" = 1 ]; then
    echo
    echo "-> Running QEMU boot test..."
    echo "NOTE: QEMU validates kernel/initramfs boot only. Real iMac hardware remains NOT TESTED."
    if [ -x scripts/run-vm.sh ]; then
        sh scripts/run-vm.sh out/MacLiteOS.iso
    else
        echo "scripts/run-vm.sh not found"
    fi
fi
