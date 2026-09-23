#!/bin/sh
# Build the G1OS initramfs from boot/initrd.list. Usage: make-initrd.sh out.img
set -eu
OUT=${1:?usage: make-initrd.sh out.img}
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/bin" "$W/sbin" "$W/usr/bin" "$W/lib/firmware" "$W/lib/modules" "$W/etc/maca-lite" "$W/etc/modprobe.d" "$W/dev" "$W/proc" "$W/sys" "$W/mnt" "$W/run" "$W/run/live" "$W/run/maclite-base"

while read -r pat; do
    case "$pat" in \#*|"") continue ;; esac
    found=0
    for f in $pat; do
        [ -e "$f" ] || continue
        found=1
        d="$W/$(dirname "$f")"
        mkdir -p "$d"
        cp -a "$f" "$d/"
    done
    [ "$found" = 1 ] || { echo "ERROR: initrd.list entry has no matching source: $pat" >&2; exit 1; }
done < boot/initrd.list

[ -x "$W/bin/busybox" ] || { echo "ERROR: initramfs requires a real busybox binary" >&2; exit 2; }
for applet in mount umount mkdir cat grep sed sh modprobe blkid switch_root; do
    ln -sf /bin/busybox "$W/bin/$applet"
done

echo balanced > "$W/etc/maca-lite/mode"
cat > "$W/init" <<'INIT'
#!/bin/sh
set -eu

fatal() { echo "ERROR: $*" >&2; exec /bin/sh; }

mount -t proc proc /proc || fatal "cannot mount /proc"
mount -t sysfs sysfs /sys || fatal "cannot mount /sys"
mount -t devtmpfs devtmpfs /dev || fatal "cannot mount /dev"
mkdir -p /run /run/live /run/maclite-base /mnt

# Best-effort module loading is intentional: the kernel may have these drivers
# built in. Required boot-path failures below are fatal.
for m in radeon tg3 b43 snd-hda-intel; do modprobe "$m" 2>/dev/null || true; done

# Live boot: locate the ISO9660 medium and mount its bundled G1OS base.
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
fi

if [ -x /mnt/usr/bin/mica-comp ]; then
    exec /mnt/usr/bin/mica-comp --session --backend auto
elif [ -x /run/maclite-base/usr/bin/mica-comp ]; then
    exec /run/maclite-base/usr/bin/mica-comp --session --backend auto
else
    fatal "G1OS runtime base could not be mounted"
fi
INIT
chmod +x "$W/init"

mkdir -p "$(dirname "$OUT")"
( cd "$W" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
[ -s "$OUT" ] || { echo "ERROR: initramfs output is empty" >&2; exit 3; }
ls -la "$OUT"
