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
        base=$(basename "$f")
        case "$f" in
            /usr/bin/mica-comp|/usr/bin/mica-shell|/usr/bin/mica-finder|/usr/bin/mica-terminal|\
            /usr/bin/mica-viewer|/usr/bin/mica-settings|/usr/bin/mica-sysinfo|/usr/bin/mica-textedit|\
            /usr/bin/mica-player|/usr/bin/mica-music|/usr/bin/mica-pdf|/usr/bin/maclite-browser|\
            /usr/bin/maclite-video|/usr/bin/mica-installer|/usr/bin/g1os-ui-health|/usr/bin/g1os-splash|\
            /usr/bin/maclite-hardware|/usr/bin/maclite-gpu|/usr/bin/maclite-display|/usr/bin/maclite-brightness|\
            /usr/bin/maclite-audio|/usr/bin/maclite-video-test|/usr/bin/maclite-network|/usr/bin/maclite-usb|\
            /usr/bin/maclite-storage|/usr/bin/maclite-power|/usr/bin/maclite-cpu|/usr/bin/maclite-drivers|\
            /usr/bin/maclite-fan|/usr/bin/maclite-gpu-benchmark|/usr/bin/maclite-recovery)
                if [ -x "out/$base" ]; then src="out/$base"; fi
                ;;
            *)
                for candidate in \
                    "out/$base" \
                    "rootfs$f" \
                    "rootfs/${f#/}" \
                    "${f#/}" \
                    "recovery/$base" \
                    "drivers/catalog/$base" \
                    "$f"; do
                    if [ -e "$candidate" ]; then src="$candidate"; break; fi
                done
                ;;
        esac
        [ -n "$src" ] || continue
        found=1
        d="$W/$(dirname "$f")"
        mkdir -p "$d"
        cp -a "$src" "$d/"
        [ -x "$W$f" ] || chmod +x "$W$f" 2>/dev/null || true
    done
    [ "$found" = 1 ] || { echo "ERROR: initrd.list entry has no matching source: $pat" >&2; exit 4; }
done < boot/initrd.list

cp -a "$MODULES/lib/modules/." "$W/lib/modules/"
if [ -n "${G1OS_FIRMWARE_DIR:-}" ]; then
    [ -d "$G1OS_FIRMWARE_DIR" ] || { echo "ERROR: G1OS_FIRMWARE_DIR is not a directory" >&2; exit 5; }
    cp -a "$G1OS_FIRMWARE_DIR/." "$W/lib/firmware/"
fi

# Copy the complete ELF dependency closure. A single ldd pass is insufficient:
# shared libraries can themselves depend on additional libraries that are not
# direct dependencies of the application. Missing dependencies are fatal.
copy_lib() {
    lib="$1"
    [ -f "$lib" ] || return 0
    dest="$W$lib"
    if [ -f "$dest" ]; then return 0; fi
    mkdir -p "$(dirname "$dest")"
    cp -L "$lib" "$dest"
}

for ld in /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 /usr/lib64/ld-linux-x86-64.so.2; do
    [ -f "$ld" ] && copy_lib "$ld"
done

scan_deps() {
    target="$1"
    [ -f "$target" ] || return 0
    deps=$(ldd "$target" 2>&1) || {
        echo "ERROR: cannot inspect ELF dependencies: $target" >&2
        echo "$deps" >&2
        return 1
    }
    echo "$deps" | grep -F "not found" >/dev/null 2>&1 && {
        echo "ERROR: unresolved ELF dependency for $target" >&2
        echo "$deps" >&2
        return 1
    }
    echo "$deps" | sed -n \
      -e 's/.*=> \(\/[^ ]*\).*/\1/p' \
      -e 's/^[[:space:]]*\(\/[^ ]*\) (0x.*/\1/p' | while read -r lib; do
        [ -n "$lib" ] || continue
        [ -f "$lib" ] || { echo "ERROR: ELF dependency path does not exist: $lib (from $target)" >&2; exit 1; }
        copy_lib "$lib"
    done
}

# Iterate until no new shared objects are added, covering transitive dependencies.
iteration=0
while :; do
    iteration=$((iteration + 1))
    before=$(find "$W/lib" "$W/lib64" "$W/usr/lib" -type f 2>/dev/null | wc -l | tr -d ' ')
    for exe in "$W"/usr/bin/*; do
        [ -f "$exe" ] || continue
        scan_deps "$exe"
    done
    for libdir in "$W/lib" "$W/lib64" "$W/usr/lib" "$W/usr/lib64"; do
        [ -d "$libdir" ] || continue
        find "$libdir" -type f -print | while read -r lib; do scan_deps "$lib"; done
    done
    after=$(find "$W/lib" "$W/lib64" "$W/usr/lib" -type f 2>/dev/null | wc -l | tr -d ' ')
    [ "$after" -eq "$before" ] && break
    [ "$iteration" -lt 20 ] || { echo "ERROR: ELF dependency closure did not converge" >&2; exit 7; }
done

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
