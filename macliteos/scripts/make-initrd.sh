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
"$W/bin/busybox" --install -s "$W/bin" 2>/dev/null || true
for applet in mount umount mkdir cat grep sed sh modprobe blkid switch_root chroot \
              fdisk losetup dd partprobe sync awk sleep dmesg ls cp mv rm touch \
              mktemp mkfs.vfat mkdosfs mkfs.ext2 mke2fs find which head tail wc tr cut \
              sort uniq uname ip ifconfig ping udhcpc wget reboot poweroff halt env expr dirname basename; do
    ln -sf /bin/busybox "$W/bin/$applet" 2>/dev/null || true
    ln -sf /bin/busybox "$W/sbin/$applet" 2>/dev/null || true
    ln -sf /bin/busybox "$W/usr/bin/$applet" 2>/dev/null || true
done
ln -sf /bin/busybox "$W/bin/mkfs.ext4" 2>/dev/null || true
ln -sf /bin/busybox "$W/sbin/mkfs.ext4" 2>/dev/null || true

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
    ldd "$exe" 2>/dev/null | sed -n -e 's/.*=> \(\/[^ ]*\).*/\1/p' -e 's/^[[:space:]]*\(\/[^ ]*\) (0x.*/\1/p' | while read -r lib; do
        [ -f "$lib" ] || continue
        dest="$W$lib"
        mkdir -p "$(dirname "$dest")"
        cp -L "$lib" "$dest"
    done
    # Guarantee ld-linux dynamic linker exists
    for ld in /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 /usr/lib64/ld-linux-x86-64.so.2; do
        if [ -f "$ld" ]; then
            dest="$W$ld"
            mkdir -p "$(dirname "$dest")"
            cp -L "$ld" "$dest"
        fi
    done
    if [ -f "$W/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] && [ ! -e "$W/lib64/ld-linux-x86-64.so.2" ]; then
        mkdir -p "$W/lib64"
        ln -sf /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$W/lib64/ld-linux-x86-64.so.2"
    fi
}
for exe in "$W"/usr/bin/*; do copy_runtime_deps "$exe"; done

cat > "$W/init" <<'INIT'
#!/bin/sh
set +e

# Redirect early standard I/O to console if available
if [ -e /dev/console ]; then
    exec </dev/console >/dev/console 2>&1
fi

echo "=========================================================="
echo "    G1OS — macOS-Lite Native Boot Loader (x86_64)         "
echo "=========================================================="

fatal() {
    echo "" >&2
    echo "==========================================================" >&2
    echo "ERROR: $*" >&2
    echo "==========================================================" >&2
    echo "Dropping to interactive rescue shell on console." >&2
    echo "System will NOT reboot automatically. Inspect with dmesg/blkid." >&2
    while true; do
        /bin/sh </dev/console >/dev/console 2>&1 || /bin/sh || sleep 2
    done
}

mount -t proc proc /proc || fatal "cannot mount /proc"
mount -t sysfs sysfs /sys || fatal "cannot mount /sys"
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mount -t tmpfs tmpfs /dev || fatal "cannot mount /dev"
mkdir -p /run /run/live /run/maclite-base /mnt /tmp /var /var/log

CMDLINE="$(cat /proc/cmdline 2>/dev/null || echo '')"
echo "Kernel command line: $CMDLINE"

# Detect graphics mode request
SAFE_GRAPHICS=0
case "$CMDLINE" in
    *nomodeset*|*radeon.modeset=0*|*maclite.gl=off*)
        SAFE_GRAPHICS=1
        echo "G1OS Boot: Safe Graphics requested (nomodeset active, radeon KMS disabled)."
        ;;
    *)
        echo "G1OS Boot: Standard Graphics mode (Radeon KMS)."
        ;;
esac

# Load hardware drivers
if [ "$SAFE_GRAPHICS" = 0 ]; then
    modprobe radeon 2>/dev/null || true
else
    echo "Skipping radeon module load in Safe Graphics mode."
fi
for m in tg3 b43 snd-hda-intel; do
    modprobe "$m" 2>/dev/null || true
done

# Check if recovery shell explicitly requested
case "$CMDLINE" in
*"rd.maclite=recovery"*)
    echo "G1OS recovery shell requested by boot parameters."
    echo "Run 'maclite-hardware' for diagnostics or 'maclite-install' to install."
    while true; do
        /bin/sh </dev/console >/dev/console 2>&1 || sleep 2
    done
    ;;
esac

# Check for installed root target first
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
        root=/dev/*)
            ROOT_TARGET="${param#root=}"
            ;;
    esac
done

if [ -n "$ROOT_TARGET" ] && [ -b "$ROOT_TARGET" ]; then
    echo "Booting installed G1OS root: $ROOT_TARGET"
    mount -o ro "$ROOT_TARGET" /mnt || fatal "cannot mount installed G1OS root: $ROOT_TARGET"
    [ -x /mnt/usr/bin/mica-comp ] || fatal "installed root is missing /usr/bin/mica-comp"
    BACKEND_ARG="auto"
    [ "$SAFE_GRAPHICS" = 1 ] && BACKEND_ARG="fbdev"
    exec /bin/busybox chroot /mnt /usr/bin/mica-comp --session --backend "$BACKEND_ARG"
fi

# Live boot: locate installation medium containing /live/maclite-base.sqfs
echo "Locating G1OS live installation media..."
LIVE_IMAGE=""
for attempt in $(seq 1 15); do
    for dev in /dev/sd[a-z] /dev/sd[a-z][0-9]* /dev/nvme*n1* /dev/mmcblk* /dev/sr*; do
        [ -b "$dev" ] || continue
        # Probe candidate device
        mkdir -p /run/mnt_probe
        if mount -o ro "$dev" /run/mnt_probe 2>/dev/null; then
            if [ -s /run/mnt_probe/live/maclite-base.sqfs ]; then
                umount /run/mnt_probe 2>/dev/null || true
                LIVE_IMAGE="$dev"
                echo "Found G1OS live medium on: $dev"
                break 2
            fi
            umount /run/mnt_probe 2>/dev/null || true
        fi
    done
    echo "Waiting for USB/storage device initialization... ($attempt/15s)"
    sleep 1
done

if [ -z "$LIVE_IMAGE" ]; then
    fatal "G1OS live installation media could not be found after 15s. Check USB connection."
fi

mount -o ro "$LIVE_IMAGE" /run/live || fatal "cannot mount live media $LIVE_IMAGE"
[ -s /run/live/live/maclite-base.sqfs ] || fatal "live media missing live/maclite-base.sqfs"
mount -t squashfs -o ro,loop /run/live/live/maclite-base.sqfs /run/maclite-base || fatal "cannot mount base filesystem"

# Populate/bind live filesystem components into userspace
if [ -d /run/maclite-base/usr ]; then
    for sub in bin share; do
        if [ -d "/run/maclite-base/usr/$sub" ]; then
            mkdir -p "/usr/$sub"
            cp -rP "/run/maclite-base/usr/$sub/." "/usr/$sub/" 2>/dev/null || true
        fi
    done
fi
if [ -d /run/maclite-base/etc ]; then
    cp -rP /run/maclite-base/etc/. /etc/ 2>/dev/null || true
fi

export PATH="/usr/bin:/bin:/sbin:/usr/sbin:/run/maclite-base/usr/bin"
export LD_LIBRARY_PATH="/lib:/usr/lib:/run/maclite-base/lib:/run/maclite-base/usr/lib"

BACKEND_ARG="auto"
if [ "$SAFE_GRAPHICS" = 1 ]; then
    BACKEND_ARG="fbdev"
    export MICA_GL=off
fi

if [ -x /usr/bin/mica-comp ]; then
    echo "Launching G1OS desktop session (backend: $BACKEND_ARG)..."
    /usr/bin/mica-comp --session --backend "$BACKEND_ARG" || true
    echo "Compositor exited. Launching G1OS installer on console..."
elif [ -x /run/maclite-base/usr/bin/mica-comp ]; then
    echo "Launching G1OS desktop session from base (backend: $BACKEND_ARG)..."
    /run/maclite-base/usr/bin/mica-comp --session --backend "$BACKEND_ARG" || true
    echo "Compositor exited. Launching G1OS installer on console..."
fi

# Fallback: if GUI is not available or terminates, launch CLI installer on console
if [ -x /usr/bin/maclite-install ]; then
    echo "Starting interactive G1OS CLI installer..."
    /usr/bin/maclite-install || true
fi

echo "Dropping to G1OS rescue console. Run 'maclite-install' to install."
for tty in /dev/tty0 /dev/tty1 /dev/console; do
    [ -c "$tty" ] || continue
    setsid cttyhack /bin/sh <"$tty" >"$tty" 2>&1 || true
done
while true; do
    echo "G1OS Live Shell active. Press Enter for prompt."
    /bin/sh </dev/console >/dev/console 2>&1 || /bin/sh || sleep 5
done
INIT
chmod +x "$W/init"

mkdir -p "$(dirname "$OUT")"
( cd "$W" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )
[ -s "$OUT" ] || { echo "ERROR: initramfs output is empty" >&2; exit 6; }
ls -la "$OUT"
