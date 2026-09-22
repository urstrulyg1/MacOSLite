#!/usr/bin/env python3
"""Generate sysfs/procfs fixtures for tests/test_hardware.c.

None of this is real hardware. The point is to pin the *detection logic* — the
parts that decide what exists and what to do about it — so that a change which
breaks iMac support fails in CI instead of on the machine. Runtime verification
on real hardware is a separate tier (docs/testing.md).

    python3 tests/fixtures/make_sysfs.py <outdir> [--variant imac11_2|imac11_3|virtual|bare]

The generated tree is read through ML_SYSFS_ROOT / ML_PROC_ROOT / ML_DEV_ROOT /
ML_ETC_ROOT, which is exactly what the fixtures exercise.
"""
import os
import struct
import sys


def wr(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode) as f:
        f.write(data)


def symlink(target, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.islink(path) or os.path.exists(path):
        os.remove(path)
    os.symlink(target, path)


def edid_dtd(pixclk_khz, hactive, hblank, vactive, vblank, hoff=88, hpulse=44,
             voff=4, vpulse=5, hsize_mm=597, vsize_mm=336, hsync_pos=True, vsync_pos=True):
    d = bytearray(18)
    struct.pack_into("<H", d, 0, pixclk_khz // 10)
    d[2] = hactive & 0xFF
    d[3] = hblank & 0xFF
    d[4] = ((hactive >> 8) << 4) | ((hblank >> 8) & 0x0F)
    d[5] = vactive & 0xFF
    d[6] = vblank & 0xFF
    d[7] = ((vactive >> 8) << 4) | ((vblank >> 8) & 0x0F)
    d[8] = hoff & 0xFF
    d[9] = hpulse & 0xFF
    d[10] = ((voff & 0x0F) << 4) | (vpulse & 0x0F)
    d[11] = (((hoff >> 4) & 0x03) << 6) | (((hpulse >> 4) & 0x03) << 4) | \
            (((voff >> 4) & 0x03) << 2) | ((vpulse >> 4) & 0x03)
    d[12] = hsize_mm & 0xFF
    d[13] = vsize_mm & 0xFF
    d[14] = ((hsize_mm >> 8) << 4) | ((vsize_mm >> 8) & 0x0F)
    d[17] = 0x18 | (0x02 if hsync_pos else 0) | (0x04 if vsync_pos else 0)
    return bytes(d)


def edid_name(text):
    d = bytearray(18)
    d[3] = 0xFC
    payload = text.encode()[:13]
    payload += b"\n" * (13 - len(payload))
    d[5:18] = payload
    return bytes(d)


def edid_dummy():
    return bytes([0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]) + b"\x00" * 0


def edid_dummy18():
    d = bytearray(18)
    d[3] = 0x10
    return bytes(d)


def make_edid(name, mfg=(ord("A") - 64, ord("P") - 64, ord("P") - 64), product=0x9211,
              dtds=None, checksum_ok=True):
    e = bytearray(128)
    e[0:8] = bytes([0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])
    m = (mfg[0] << 10) | (mfg[1] << 5) | mfg[2]
    e[8] = (m >> 8) & 0xFF
    e[9] = m & 0xFF
    struct.pack_into("<H", e, 10, product)
    struct.pack_into("<I", e, 12, 0x01020304)
    e[16] = 5          # week
    e[17] = 20         # 2010
    e[18] = 1          # EDID 1.3
    e[19] = 3
    e[20] = 0x80       # digital input
    e[21] = 60         # 60 cm wide
    e[22] = 34
    e[23] = 0x78       # gamma 2.2
    # established timings (byte 0x23 / 0x24): 640x480@60, 800x600@60, 1024x768@60
    e[35] = 0x21        # bit5 640x480@60, bit0 800x600@60
    e[36] = 0x08        # bit3 1024x768@60
    e[37] = 0x00
    descs = list(dtds or [])[:3]          # first detailed timing = preferred
    descs.append(edid_name(name))
    while len(descs) < 4:
        descs.append(edid_dummy18())
    for i in range(4):
        e[54 + i * 18:54 + (i + 1) * 18] = descs[i][:18]
    e[126] = 0
    e[127] = (-sum(e[:127])) & 0xFF
    if not checksum_ok:
        e[127] ^= 0xFF
    return bytes(e)


IMAC_PANEL_1080 = edid_dtd(148500, 1920, 280, 1080, 45)
IMAC_PANEL_1440 = edid_dtd(241500, 2560, 160, 1440, 35, hsize_mm=597, vsize_mm=336)


def variant_imac(out, which):
    """iMac11,2 (Radeon HD 4670, 21.5") or iMac11,3 (HD 5670, 27")."""
    vid, did, gpu_name = (0x1002, 0x9490, "RV730") if which == "imac11_2" else (0x1002, 0x68C1, "Redwood")
    panel, pw, ph, product = (IMAC_PANEL_1080, 1920, 1080, 0x9111) if which == "imac11_2" else \
                             (IMAC_PANEL_1440, 2560, 1440, 0x9211)
    sysroot = os.path.join(out, "sys")
    procroot = os.path.join(out, "proc")
    driver = "radeon"

    pci = os.path.join(sysroot, "bus/pci/devices/0000:01:00.0")
    wr(os.path.join(pci, "class"), "0x030000\n")
    wr(os.path.join(pci, "vendor"), "0x%04x\n" % vid)
    wr(os.path.join(pci, "device"), "0x%04x\n" % did)
    wr(os.path.join(pci, "mem_info_vram_total"), "%d\n" % (512 * 1024 * 1024))
    os.makedirs(os.path.join(pci, "drm/card0/card0-eDP-1"), exist_ok=True)
    symlink("../../../../bus/pci/drivers/%s" % driver, os.path.join(pci, "driver"))

    # class/drm entries point at the card's connector directories
    wr(os.path.join(sysroot, "class/drm/card0-eDP-1/status"), "connected\n")
    wr(os.path.join(sysroot, "class/drm/card0-eDP-1/enabled"), "enabled\n")
    wr(os.path.join(sysroot, "class/drm/card0-eDP-1/dpms"), "On\n")
    wr(os.path.join(sysroot, "class/drm/card0-eDP-1/modes"),
      "%dx%d\n1920x1080\n1280x720\n1024x768\n" % (pw, ph))
    wr(os.path.join(sysroot, "class/drm/card0-eDP-1/edid"),
      make_edid("iMac", product=product, dtds=[panel]))
    # a second connector, disconnected, to prove the probe picks the right one
    wr(os.path.join(sysroot, "class/drm/card0-DP-1/status"), "disconnected\n")
    wr(os.path.join(sysroot, "class/drm/card0-DP-1/modes"), "")
    # class/drm/card0 -> the device's own drm directory, as sysfs exposes it
    symlink("../../bus/pci/devices/0000:01:00.0/drm/card0", os.path.join(sysroot, "class/drm/card0"))
    # /sys/class/drm/card0/device -> the PCI device, as the kernel exposes it
    # from <sys>/bus/pci/devices/0000:01:00.0/drm/card0/ the device dir is up three
    symlink("../../../0000:01:00.0", os.path.join(pci, "drm/card0/device"))
    # /sys/block is the legacy alias of class/block; both exist on real kernels
    symlink("../class/block", os.path.join(sysroot, "block"))
    # render node + device nodes (regular empty files: access() is all we check)
    wr(os.path.join(pci, "drm/renderD128"), "")
    wr(os.path.join(out, "dev/dri/card0"), "")
    wr(os.path.join(out, "dev/dri/renderD128"), "")
    wr(os.path.join(out, "dev/fb0"), "")
    wr(os.path.join(out, "etc/resolv.conf"), "nameserver 192.168.1.1\nnameserver 8.8.8.8\n")

    # backlight: the DRM driver's own interface plus the fake ACPI one
    bl = os.path.join(sysroot, "class/backlight")
    wr(os.path.join(bl, "radeon_bl0/max_brightness"), "255\n")
    wr(os.path.join(bl, "radeon_bl0/brightness"), "140\n")
    wr(os.path.join(bl, "radeon_bl0/actual_brightness"), "140\n")
    wr(os.path.join(bl, "acpi_video0/max_brightness"), "10\n")
    wr(os.path.join(bl, "acpi_video0/brightness"), "5\n")
    wr(os.path.join(bl, "acpi_video0/actual_brightness"), "5\n")

    # DMI says Apple, and we are booted through EFI -> acpi_video0 is suspect
    wr(os.path.join(sysroot, "class/dmi/id/sys_vendor"), "Apple Inc.\n")
    wr(os.path.join(sysroot, "class/dmi/id/product_name"), which.replace("_", ",").replace("imac", "iMac") + "\n")
    os.makedirs(os.path.join(sysroot, "firmware/efi"), exist_ok=True)
    wr(os.path.join(procroot, "cmdline"),
      "root=LABEL=MACLITE_BASE ro quiet acpi_backlight=native radeon.uvd=1\n")

    # network: tg3 wired (cable plugged: link-layer facts are modellable from
    # sysfs) + b43 wireless with no association. Addresses are NOT modelled —
    # no fixture file can express one, so tools report them NOT TESTED.
    net = os.path.join(sysroot, "class/net")
    for name, kind in (("eth0", "tg3"), ("wlan0", "b43")):
        base = os.path.join(net, name)
        wired = name == "eth0"
        wr(os.path.join(base, "operstate"), "up\n" if wired else "down\n")
        wr(os.path.join(base, "address"), "00:26:bb:11:22:33\n" if wired else "00:26:bb:44:55:66\n")
        wr(os.path.join(base, "flags"), "0x1003\n" if wired else "0x1002\n")
        wr(os.path.join(base, "carrier"), "1\n" if wired else "0\n")
        wr(os.path.join(base, "statistics/rx_bytes"), "0\n")
        wr(os.path.join(base, "statistics/tx_bytes"), "0\n")
        symlink("../../../../bus/pci/drivers/%s" % kind, os.path.join(base, "device/driver"))
    os.makedirs(os.path.join(sysroot, "class/net/wlan0/wireless"), exist_ok=True)
    wr(os.path.join(sysroot, "class/net/wlan0/wireless/link"), "0\n")

    # storage: internal SSD with two partitions
    wr(os.path.join(sysroot, "class/block/sda/size"), "%d\n" % (500107862016 // 512))
    wr(os.path.join(sysroot, "class/block/sda/removable"), "0\n")
    wr(os.path.join(sysroot, "class/block/sda/device/model"), "APPLE SSD TS256C\n")
    wr(os.path.join(sysroot, "class/block/sda/device/vendor"), "ATA\n")
    for part in ("sda1", "sda2"):
        wr(os.path.join(sysroot, "class/block", part, "size"), "104857600\n")
        wr(os.path.join(sysroot, "class/block", part, "removable"), "0\n")
        wr(os.path.join(sysroot, "class/block", part, "partition"), "1\n")

    # USB: keyboard, mouse, mass storage
    usb = os.path.join(sysroot, "bus/usb/devices")
    for port, cls, proto, prod, drv, in (
            ("1-1", 0x03, 0x01, "Apple Internal Keyboard / Trackpad", "usbhid"),
            ("1-2", 0x03, 0x02, "Logitech USB Optical Mouse", "usbhid"),
            ("2-1", 0x08, 0x50, "SanDisk Cruzer Blade", "usb-storage")):
        base = os.path.join(usb, port)
        wr(os.path.join(base, "idVendor"), "0x05ac\n" if port.startswith("1") else "0x0781\n")
        wr(os.path.join(base, "idProduct"), "0x0245\n")
        wr(os.path.join(base, "product"), prod + "\n")
        wr(os.path.join(base, "manufacturer"), "Apple Inc.\n")
        wr(os.path.join(base, "speed"), "12\n" if port != "2-1" else "480\n")
        iface = os.path.join(base, "%s:1.0" % port)
        wr(os.path.join(iface, "bInterfaceClass"), "0x%02x\n" % cls)
        wr(os.path.join(iface, "bInterfaceProtocol"), "0x%02x\n" % proto)
        symlink("../../../../bus/usb/drivers/%s" % drv, os.path.join(iface, "driver"))

    # input devices (the HID layer above the USB devices)
    wr(os.path.join(procroot, "bus/input/devices"),
      "I: Bus=0011 Vendor=0001 Product=0001 Version=ab41\n"
      "N: Name=\"Apple Inc. Magic Keyboard\"\n"
      "P: Phys=usb-0000:00:1a.0-1.1/input0\n"
      "H: Handlers=sysrq kbd leds event3\n"
      "B: PROP=0\n"
      "B: EV=120013\n"
      "B: KEY=ffffffff ffffffff ffffffff ffffffff\n\n"
      "I: Bus=0011 Vendor=0002 Product=0007 Version=01b1\n"
      "N: Name=\"Apple Inc. Magic Trackpad\"\n"
      "H: Handlers=mouse0 event4\n"
      "B: EV=17\n"
      "B: KEY=70000 0 0 0 0\n\n")

    # audio: Realtek ALC889 on HDA
    wr(os.path.join(procroot, "asound/cards"),
      " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
      "                      HDA Intel PCH at 0xd0700000 irq 45\n")
    wr(os.path.join(procroot, "asound/card0/id"), "PCH\n")
    wr(os.path.join(procroot, "asound/card0/pcm0p/info"), "card: 0\ndevice: 0\nsubdevice: 0\nstream: PLAYBACK\n")
    wr(os.path.join(procroot, "asound/card0/pcm0c/info"), "card: 0\ndevice: 0\nstream: CAPTURE\n")
    wr(os.path.join(procroot, "asound/card0/pcm3p/info"), "card: 0\ndevice: 3\nstream: PLAYBACK\n")
    wr(os.path.join(procroot, "asound/card0/codec#0"),
      "Codec: Realtek ALC889\nAddress: 0\nAFG Function Id: 0x1 (unsol 1)\n")

    # fbdev fallback that exists but should NOT be preferred over KMS
    wr(os.path.join(sysroot, "class/graphics/fb0/virtual_size"), "1920,1080\n")
    wr(os.path.join(sysroot, "class/graphics/fb0/bits_per_pixel"), "32\n")
    wr(os.path.join(sysroot, "class/graphics/fb0/name"), "radeondrmfb\n")

    # power
    wr(os.path.join(sysroot, "power/state"), "freeze mem disk\n")
    wr(os.path.join(sysroot, "power/mem_sleep"), "s2idle [deep]\n")

    # system facts
    wr(os.path.join(procroot, "meminfo"), "MemTotal:        8192000 kB\nMemAvailable:    7100000 kB\n")
    wr(os.path.join(procroot, "cpuinfo"),
      "processor\t: 0\nmodel name\t: Intel(R) Core(TM) i5 CPU         650  @ 3.20GHz\n"
      "cpu MHz\t\t: 3200.000\nflags\t\t: fpu sse2 ssse3 sse4_1 sse4_2\n"
      "siblings\t: 4\ncpu cores\t: 2\n\n")
    wr(os.path.join(sysroot, "devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"), "3320000\n")
    wr(os.path.join(procroot, "net/dev"),
      "Inter-|   Receive                                                |  Transmit\n"
      " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets\n"
      "    lo:   10000      10    0    0    0     0          0         0    10000      10\n")
    wr(os.path.join(procroot, "net/route"),
      "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
      "eth0\t00000000\t0A000002\t0003\t0\t0\t0\t00000000\t0\t0\t0\n")
    wr(os.path.join(procroot, "net/if_inet6"), "")
    wr(os.path.join(procroot, "self/mountinfo"),
      "25 0 8:2 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n"
      "30 25 8:1 /boot /boot ro,relatime shared:2 - vfat /dev/sda1 rw\n")
    wr(os.path.join(procroot, "self/status"), "Name:\tmaclite\nVmRSS:\t  4096 kB\nVmHWM:\t  8192 kB\n")
    os.makedirs(os.path.join(out, "dev/snd"), exist_ok=True)


def variant_virtual(out):
    """A QEMU guest: virtio-gpu, no decode, no backlight, no sound, no USB."""
    sysroot = os.path.join(out, "sys")
    procroot = os.path.join(out, "proc")
    pci = os.path.join(sysroot, "bus/pci/devices/0000:00:01.0")
    wr(os.path.join(pci, "class"), "0x030000\n")
    wr(os.path.join(pci, "vendor"), "0x1af4\n")
    wr(os.path.join(pci, "device"), "0x1010\n")
    symlink("../../../../bus/pci/drivers/virtio-pci", os.path.join(pci, "driver"))
    os.makedirs(os.path.join(pci, "drm/card0"), exist_ok=True)
    symlink("../../bus/pci/devices/0000:00:01.0/drm/card0", os.path.join(sysroot, "class/drm/card0"))
    wr(os.path.join(out, "dev/dri/card0"), "")
    wr(os.path.join(out, "etc/resolv.conf"), "nameserver 10.0.2.3\n")
    wr(os.path.join(sysroot, "class/drm/card0-Virtual-1/status"), "connected\n")
    wr(os.path.join(sysroot, "class/drm/card0-Virtual-1/enabled"), "enabled\n")
    wr(os.path.join(sysroot, "class/drm/card0-Virtual-1/modes"), "1024x768\n800x600\n")
    wr(os.path.join(procroot, "meminfo"), "MemTotal:        2048000 kB\nMemAvailable:    1800000 kB\n")
    wr(os.path.join(procroot, "cpuinfo"),
      "processor\t: 0\nmodel name\t: QEMU Virtual CPU version 2.5+\nflags\t\t: fpu sse2\nsiblings\t: 2\ncpu cores\t: 1\n")
    wr(os.path.join(procroot, "cmdline"), "console=ttyS0\n")
    wr(os.path.join(procroot, "net/dev"), "Inter-| Receive\n    lo: 0 0 0 0 0 0 0 0 0 0\n")
    wr(os.path.join(sysroot, "class/net/eth0/operstate"), "unknown\n")
    wr(os.path.join(sysroot, "class/net/eth0/address"), "52:54:00:12:34:56\n")
    wr(os.path.join(procroot, "self/status"), "Name:\tmaclite\nVmRSS:\t  1024 kB\n")
    wr(os.path.join(procroot, "self/mountinfo"), "25 0 8:2 / / rw - ext4 /dev/vda1 rw\n")


def variant_bare(out):
    """Nothing at all: every probe must report UNSUPPORTED, never PASS."""
    os.makedirs(os.path.join(out, "sys"), exist_ok=True)
    os.makedirs(os.path.join(out, "proc"), exist_ok=True)
    os.makedirs(os.path.join(out, "dev"), exist_ok=True)
    wr(os.path.join(out, "proc/meminfo"), "MemTotal:        1024000 kB\nMemAvailable:     900000 kB\n")
    wr(os.path.join(out, "proc/cpuinfo"), "processor\t: 0\nmodel name\t: bare\nflags\t\t: fpu\n")
    wr(os.path.join(out, "proc/self/status"), "Name:\tmaclite\nVmRSS:\t  512 kB\n")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    variant = "imac11_2"
    if "--variant" in sys.argv:
        variant = sys.argv[sys.argv.index("--variant") + 1]
    os.makedirs(out, exist_ok=True)
    if variant in ("imac11_2", "imac11_3"):
        variant_imac(out, variant)
    elif variant == "virtual":
        variant_virtual(out)
    elif variant == "bare":
        variant_bare(out)
    else:
        print("unknown variant %s" % variant, file=sys.stderr)
        return 2
    print("wrote %s fixture to %s" % (variant, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
