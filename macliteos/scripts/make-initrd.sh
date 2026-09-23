#!/bin/sh
# Build the G1OS initramfs from repository files plus explicit build outputs.
# Usage: G1OS_BUSYBOX=out/busybox G1OS_KERNEL_MODULES=out/kernel-modules sh scripts/make-initrd.sh out/initrd-maclite.img
set -eu
OUT=${1:?usage: make-initrd.sh out.img}
cd "$(dirname "$0")/.."
BUSYBOX=${G1OS_BUSYBOX:-out/busybox}
MODULES=${G1OS_KERNEL_MODULES:-out/kernel-modules}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/bin" "$W/sbin" "$W/usr/bin" "$W/lib/firmware" "$W/lib/modules" "$W/etc/maca-lite" "$W/etc/modprobe.d" "$W/dev" "$W/proc" "$W/sys" "$W/mnt" "$W/run" "$W/run/live" "$W/run/maclite-base"

[ -s "$BUSYBOX" ] || { echo "ERROR: static BusyBox missing: $BUSYBOX" >&2; exit 2; }
cp "$BUSYBOX" "$W/bin/busybox"
chmod 0755 "$W/bin/busybox"
for applet in mount umount mkdir cat grep sed sh modprobe blkid switch_root; do
    ln -sf /bin/busybox "$W/bin/$applet"
done

while read -r pat; do
    case "$pat" in \#*|"") continue ;; esac
    found=0
    # Entries are canonical runtime paths. Resolve them first against the repo,
    # then against out/ for binaries produced by the normal build.
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
            if [ -e "$candidate" ]; then
                src="$candidate"
                break
            fi
        done
        [ -n "$src" ] || continue
        found=1
        d="$W/$(dirname "$f")"
        mkdir -p "$d"
        cp -a "$src" "$d/"
    done
    [ "$found" = 1 ] || { echo "ERROR: initrd.list entry has no matching source: $pat" >&2; exit 3; }
done < boot/initrd.list

[ -d "$MODULES/lib/modules" ] || { echo "ERROR: kernel module staging missing: $MODULES/lib/modules" >&2; exit 4; }
cp -a "$MODULES/lib/modules/." "$W/lib/modules/"

# Firmware is intentionally an explicit input. This prevents silently shipping
# an incomplete Wi-Fi path while allowing distributors to provide firmware under
# their applicable licensing terms.
if [ -n "${G1OS_FIRMWARE_DIR:-}" ]; then
    [ -d "$G1OS_FIRMWARE_DIR" ] || { echo "ERROR: G1OS_FIRMWARE_DIR is not a directory" >&2; exit 5; }
    cp -a "$G1OS_FIRMWARE_DIR/." "$W/lib/firmware/"
fi

copy_runtime_deps() {
    exe="$1"
    [ -x "$exe" ] || return 0
    ldd "$exe" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p; s/^\(\/[^ ]*\) (0x.*/\1/p' | while read -r lib; do
        [ -f "$lib" ] || continue
        dest="$W$lib"
        mkdir -p "$(dirname "$dest")"
        cp -L "$lib" "$dest"
    done
}
for exe in "$W/usr/bin/mica-comp" "$W/usr/bin/mica-shell" "$W/usr/bin/g1os-splash"; do copy_runtime_deps "$exe"; done

cat > "$W/init" <<'INIT'
#!/bin/sh
set -eu

fatal() { echo "ERROR: $*" >&2; exec /bin/sh; }

mount -t proc proc /proc || fatal "cannot mount /proc"
mount -t sysfs sysfs /sys || fatal "cannot mount /sys"
mount -t devtmpfs devtmpfs /dev || fatal "cannot mount /dev"
mkdir -p /run /run/live /run/maclite-base /mnt

for m in radeon tg3 b43 snd-hda-intel; do modprobe "$m" 2>/dev/null || true; done

# Live boot: locate an ISO9660 medium and mount its bundled G1OS base.
LIVE_IMAGE=""
for dev in /dev/sd[a-z] /dev/sd[a-z][0-9] /dev/nvme*n1 /dev/nvme*n1p* /dev/mmcblk* /dev/mmcblk*p* /dev/sr*; do
    [ -b "$dev" ] || continue
    if blkid "$dev" 2>/dev/null | grep -q 'TYPE="iso9660"'; then LIVE_IMAGE="$dev"; break; fi
done
if [ -n "$LIVE_IMAGE" ]; then
    mount -o ro "$LIVE_IMAGE" /run/live || fatal "cannot mount live media $LIVE_IMAGE"
    [ -s /run/live/live/maclite-base.sqfs ] || fatal "live ISO is missing live/maclite-base.sqfs"
    mount -t squashfs -o ro,loop /run/live/live/maclite-base.sqfs /run/maclite-base || fatal "cannot mount G1OS base filesystem"
fi

CMDLINE="$(cat /proc/cmdline)"
ROOT_TARGET=""
for param in $CMDLINE; do
    case "$param" in
        root=UUID=*)
            UUID="${param#root=UUID=}"
            ROOT_TARGET="$(blkid -U "$UUID" 2>/dev/null || true)"
            ;;
        root=PARTUUID=*)
            PARTUUID="${param#root=PARTUUID=}"
            ROOT_TARGET="$(blkid -t PARTUUID="$PARTUUID" -o device 2>/dev/null || true)"
            ;;
        root=/dev/*) ROOT_TARGET="${param#root=}" ;;
    esac
done

case "$CMDLINE" in
*"rd.maclite=recovery"*)
    echo "G1OS recovery shell — run maclite-hardware first"
    exec /bin/sh
    ;;
esac

if [ -n "$ROOT_TARGET" ]; then
    [ -b "$ROOT_TARGET" ] || fatal "resolved root target is not a block device: $ROOT_TARGET"
    mount -o ro "$ROOT_TARGET" /mnt || fatal "cannot mount installed G1OS root"
    # The installed root is the authoritative runtime root. chroot preserves the
    # lightweight initramfs design while ensuring /lib, /etc and /usr resolve from
    # the installed filesystem rather than from transient boot media.
    [ -x /mnt/usr/bin/mica-comp ] || fatal "installed root is missing mica-comp"
    exec /bin/busybox chroot /mnt /usr/bin/mica-comp --session --backend auto
elif [ -x /run/maclite-base/usr/bin/mica-comp ]; then
    exec /run/maclite-base/usr/bin/mica-comp --session --backend auto
else
    fatal "G1OS runtime base could not be mounted"
fi
INIT
chmod +x "$W/init"

mkdir -p "$(dirname "$OUT")"
( cd "$W" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
[ -s "$OUT" ] || { echo "ERROR: initramfs output is empty" >&2; exit 6; }
ls -la "$OUT"
