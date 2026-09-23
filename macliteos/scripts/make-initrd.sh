#!/bin/sh
# Build the G1OS initramfs from boot/initrd.list. Usage: make-initrd.sh out.img
set -eu
OUT=${1:?usage: make-initrd.sh out.img}
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/bin" "$W/sbin" "$W/usr/bin" "$W/lib/firmware" "$W/etc/maca-lite" "$W/dev" "$W/proc" "$W/sys" "$W/mnt" "$W/run"

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

if [ -x "$W/bin/busybox" ]; then
    for applet in mount umount mkdir cat grep sed sh modprobe blkid switch_root; do
        ln -sf /bin/busybox "$W/bin/$applet" 2>/dev/null || true
    done
fi

echo balanced > "$W/etc/maca-lite/mode"
cat > "$W/init" <<'INIT'
#!/bin/sh
set -eu
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /run /mnt

for m in radeon tg3 b43 snd-hda-intel; do modprobe "$m" 2>/dev/null || true; done

# Live boot: locate the ISO9660 medium and mount its bundled G1OS base.
LIVE_IMAGE=""
for dev in /dev/sd[a-z][0-9] /dev/nvme*n1p* /dev/mmcblk*p* /dev/sr*; do
    [ -b "$dev" ] || continue
    if blkid "$dev" 2>/dev/null | grep -q 'TYPE="iso9660"'; then LIVE_IMAGE="$dev"; break; fi
done
if [ -n "$LIVE_IMAGE" ]; then
    mkdir -p /run/live
    mount -o ro "$LIVE_IMAGE" /run/live || true
    if [ -s /run/live/live/maclite-base.sqfs ]; then
        mkdir -p /run/maclite-base
        mount -t squashfs -o ro,loop /run/live/live/maclite-base.sqfs /run/maclite-base || true
    fi
fi

ROOT_TARGET=""
for param in $(cat /proc/cmdline); do case "$param" in root=*) ROOT_TARGET="${param#root=}" ;; esac; done
case "$(cat /proc/cmdline)" in
*rd.maclite=recovery*)
    echo "G1OS recovery shell — run maclite-hardware first"
    exec /bin/sh
    ;;
esac

if [ -n "$ROOT_TARGET" ]; then mount -o ro "$ROOT_TARGET" /mnt || true; fi
if [ -x /mnt/usr/bin/mica-comp ]; then
    exec /mnt/usr/bin/mica-comp --session --backend auto
elif [ -x /run/maclite-base/usr/bin/mica-comp ]; then
    exec /run/maclite-base/usr/bin/mica-comp --session --backend auto
else
    echo "ERROR: G1OS runtime base could not be mounted" >&2
    exec /bin/sh
fi
INIT
chmod +x "$W/init"
( cd "$W" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
[ -s "$OUT" ] || { echo "ERROR: initramfs output is empty" >&2; exit 1; }
ls -la "$OUT"
