#!/bin/sh
# Build the initramfs from boot/initrd.list. Usage: make-initrd.sh out.img
set -e
OUT=${1:?usage: make-initrd.sh out.img}
cd "$(dirname "$0")/.."
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/bin" "$W/sbin" "$W/usr/bin" "$W/lib/firmware" "$W/etc/maca-lite" "$W/dev" "$W/proc" "$W/sys" "$W/mnt"
# copy exactly what initrd.list names, resolving wildcards against /
while read -r pat; do
    case "$pat" in \#*|"") continue;; esac
    for f in $pat; do
        [ -e "$f" ] || { echo "initrd.list: missing $f (install it or trim the list)"; continue; }
        d="$W/$(dirname "$f")"; mkdir -p "$d"; cp -a "$f" "$d/"
    done
done < boot/initrd.list
echo beautiful > "$W/etc/maca-lite/mode"
cat > "$W/init" <<'INIT'
#!/bin/sh
mount -t proc proc /proc; mount -t sysfs sysfs /sys; mount -t devtmpfs devtmpfs /dev
# load what initrd.list shipped; udev-free on purpose (fast boot, spec §5)
for m in radeon tg3 b43 snd-hda-intel; do modprobe "$m" 2>/dev/null || true; done
mount -o ro LABEL=MACLITE_BASE /mnt || echo "recovery: base missing"
exec /usr/bin/mica-comp --session --drm
INIT
chmod +x "$W/init"
( cd "$W" && find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
ls -la "$OUT"
