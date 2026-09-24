#!/bin/sh
# Build a verifiable G1OS initramfs from repository files and explicit build outputs.
set -eu
OUT=${1:?usage: make-initrd.sh out.img}
cd "$(dirname "$0")/.."
BUSYBOX=${G1OS_BUSYBOX:-out/busybox}
MODULES=${G1OS_KERNEL_MODULES:-out/kernel-modules}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

mkdir -p "$W/bin" "$W/sbin" "$W/usr/bin" "$W/usr/share/maca-lite" \
  "$W/lib/firmware" "$W/lib/modules" "$W/etc/maca-lite" "$W/etc/modprobe.d" \
  "$W/dev" "$W/proc" "$W/sys" "$W/mnt" "$W/run/live" "$W/run/maclite-base" "$W/tmp"

[ -s "$BUSYBOX" ] || { echo "ERROR: static BusyBox missing: $BUSYBOX" >&2; exit 2; }
[ -d "$MODULES/lib/modules" ] || { echo "ERROR: kernel module staging missing: $MODULES/lib/modules" >&2; exit 3; }

cp "$BUSYBOX" "$W/bin/busybox"
chmod 0755 "$W/bin/busybox"
"$W/bin/busybox" --install -s "$W/bin" 2>/dev/null || true
for applet in mount umount mkdir cat grep sed sh modprobe blkid switch_root chroot \
              fdisk losetup dd partprobe sync awk sleep dmesg ls cp mv rm touch \
              mktemp mkfs.vfat mkdosfs mkfs.ext2 mkfs.ext4 mke2fs find which head tail wc tr cut \
              sort uniq uname ip ifconfig ping udhcpc wget reboot poweroff halt env expr dirname basename \
              readlink realpath date id ps kill setsid cttyhack; do
    ln -sf /bin/busybox "$W/bin/$applet" 2>/dev/null || true
    ln -sf /bin/busybox "$W/sbin/$applet" 2>/dev/null || true
done

# initrd.list contains only files that must be present before the live base is mounted.
while read -r pat; do
    case "$pat" in \#*|"") continue ;; esac
    found=0
    for f in $pat; do
        src=""
        for candidate in \
            "out/$(basename "$f")" \
            "rootfs$f" \
            "rootfs/${f#/}" \
            "${f#/}" \
            "recovery/$(basename "$f")" \
            "drivers/catalog/$(basename "$f")" \
            "$f"; do
            if [ -e "$candidate" ]; then src="$candidate"; break; fi
        done
        [ -n "$src" ] || continue
        found=1
        d="$W/$(dirname "$f")"
        mkdir -p "$d"
        cp -a "$src" "$d/"
    done
    [ "$found" = 1 ] || { echo "ERROR: initrd.list entry has no matching source: $pat" >&2; exit 4; }
done < boot/initrd.list

cp -a "$MODULES/lib/modules/." "$W/lib/modules/"
if [ -n "${G1OS_FIRMWARE_DIR:-}" ]; then
    [ -d "$G1OS_FIRMWARE_DIR" ] || { echo "ERROR: G1OS_FIRMWARE_DIR is not a directory" >&2; exit 5; }
    cp -a "$G1OS_FIRMWARE_DIR/." "$W/lib/firmware/"
fi

copy_runtime_deps() {
    exe="$1"
    [ -x "$exe" ] || return 0
    ldd "$exe" 2>/dev/null | sed -n \
      -e 's/.*=> \(\/[^ ]*\).*/\1/p' \
      -e 's/^[[:space:]]*\(\/[^ ]*\) (0x.*/\1/p' | while read -r lib; do
        [ -f "$lib" ] || continue
        dest="$W$lib"
        mkdir -p "$(dirname "$dest")"
        cp -L "$lib" "$dest"
    done
    for ld in /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 /usr/lib64/ld-linux-x86-64.so.2; do
        if [ -f "$ld" ]; then
            dest="$W$ld"
            mkdir -p "$(dirname "$dest")"
            cp -L "$ld" "$dest"
        fi
    done
}
for exe in "$W"/usr/bin/*; do copy_runtime_deps "$exe"; done

# The init program is deliberately kept in the repository so it can be audited,
# tested and hashed independently of the generated cpio archive.
cp boot/g1os-init "$W/init"
chmod 0755 "$W/init"

mkdir -p "$(dirname "$OUT")"
( cd "$W" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
[ -s "$OUT" ] || { echo "ERROR: initramfs output is empty" >&2; exit 6; }
printf '%s  %s\n' "$(sha256sum "$OUT" | awk '{print $1}')" "$OUT" > "${OUT}.sha256"
echo "INITRAMFS STATUS: PASS"
echo "INITRAMFS: $OUT"
echo "INITRAMFS SHA256: $(cut -d' ' -f1 "${OUT}.sha256")"
