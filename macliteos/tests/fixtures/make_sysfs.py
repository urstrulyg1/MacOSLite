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


# The three CPU options the 21.5-inch Mid-2010 iMac shipped with (spec §1), and
# the exact CPUID values the kernels report for each. These are used to build
# fixture trees — they are *not* used by any detection path.
CPU_OPTIONS = {
    # name        family model step  brand                                     cores thr turbo base
    "i3_306":  (6, 0x25, 5, "Intel(R) Core(TM) i3 CPU         540  @ 3.07GHz", 2, 4, False, 3066),
    "i3_320":  (6, 0x25, 5, "Intel(R) Core(TM) i3 CPU         550  @ 3.20GHz", 2, 4, False, 3200),
    "i5_360":  (6, 0x25, 5, "Intel(R) Core(TM) i5 CPU         680  @ 3.60GHz", 2, 4, True, 3600),
    "i5_650":  (6, 0x25, 5, "Intel(R) Core(TM) i5 CPU         650  @ 3.20GHz", 2, 4, True, 3200),
    "i7_870":  (6, 0x1E, 5, "Intel(R) Core(TM) i7 CPU         870  @ 2.93GHz", 4, 8, True, 2933),
}


def microcode_blob(revision, date, signature, size=4096):
    """A valid-looking Intel microcode update blob (header + zero payload).
    Written so the loader/parser fixtures exercise real header parsing; the
    payload is not a working microcode update and the tests never load it."""
    hdr = bytearray(size)
    struct.pack_into("<I", hdr, 0, 1)                 # header version
    struct.pack_into("<I", hdr, 4, revision)
    struct.pack_into("<I", hdr, 8, date)              # BCD yyyymmdd
    struct.pack_into("<I", hdr, 12, signature)        # processor signature
    struct.pack_into("<I", hdr, 20, 1)                # loader revision
    struct.pack_into("<I", hdr, 28, size - 48)        # data size
    struct.pack_into("<I", hdr, 32, size)             # total size
    return bytes(hdr)


def cpu_sig(family, model, stepping):
    """CPUID-signature encoding used by Intel microcode blobs."""
    return (stepping & 0xF) | ((model & 0xF) << 4) | ((family & 0xF) << 8) | \
           (((model >> 4) & 0xF) << 16) | (((family >> 4) & 0xF) << 20)


def write_cpu(sysroot, procroot, cpu="i5_650", threads=4, cores=2, microcode=0x1A,
              governor="ondemand", cpufreq=True, turbo_knob=True, turbo_on=True,
              thermal=True, cpuidle=True):
    """Write a /proc/cpuinfo + cpufreq + hwmon + cpuidle tree for one CPU option."""
    fam, model, step, brand, ec, et, eturbo, base = CPU_OPTIONS[cpu]
    nthreads = threads if threads else et
    info = ""
    for i in range(nthreads):
        info += ("processor\t: %d\nvendor_id\t: GenuineIntel\ncpu family\t: %d\n"
                 "model\t\t: %d\nmodel name\t: %s\nstepping\t: %d\n"
                 "microcode\t: 0x%x\ncpu MHz\t\t: %d.000\n"
                 "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov "
                 "pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx "
                 "rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology "
                 "nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx smx "
                 "est tm2 ssse3 cx16 xtpr pdcm sse4_1 sse4_2 popcnt aes lahf_lm "
                 "ida arat epb dtherm tpr_shadow vnmi flexpriority ept vpid\n"
                 "siblings\t: %d\ncpu cores\t: %d\n\n"
                 % (i, fam, model, brand, step, microcode, base, et, ec))
    wr(os.path.join(procroot, "cpuinfo"), info)
    wr(os.path.join(procroot, "version"),
       "Linux version 6.6.30-maclite (build@macliteos) (gcc 12.2.0) #1 SMP PREEMPT\n")
    if cpufreq:
        # 4 logical CPUs share one policy directory in a fixture; cpu0 is what
        # every probe reads first
        for i in range(nthreads):
            cf = os.path.join(sysroot, "devices/system/cpu/cpu%d/cpufreq" % i)
            wr(os.path.join(cf, "scaling_driver"), "acpi-cpufreq\n")
            wr(os.path.join(cf, "scaling_governor"), governor + "\n")
            wr(os.path.join(cf, "scaling_available_governors"),
               "conservative ondemand userspace powersave performance schedutil\n")
            wr(os.path.join(cf, "scaling_cur_freq"), "%d\n" % (base * 1000))
            wr(os.path.join(cf, "scaling_min_freq"), "%d\n" % (1200 * 1000))
            wr(os.path.join(cf, "scaling_max_freq"), "%d\n" % (base * 1000))
            wr(os.path.join(cf, "cpuinfo_min_freq"), "%d\n" % (1200 * 1000))
            wr(os.path.join(cf, "cpuinfo_max_freq"), "%d\n" % (base * 1000))
        if turbo_knob:
            wr(os.path.join(sysroot, "devices/system/cpu/cpu0/cpufreq/boost"),
               "1\n" if turbo_on else "0\n")
    if cpuidle:
        idle = os.path.join(sysroot, "devices/system/cpu/cpu0/cpuidle")
        wr(os.path.join(idle, "current_driver"), "acpi_idle\n")
        for idx, name in enumerate(("POLL", "C1", "C3", "C6")):
            wr(os.path.join(idle, "state%d" % idx, "name"), name + "\n")
            wr(os.path.join(idle, "state%d" % idx, "latency"), "%d\n" % (idx * 40))
            wr(os.path.join(idle, "state%d" % idx, "residency"), "%d\n" % (idx * 5))
    if thermal:
        hw = os.path.join(sysroot, "class/hwmon/hwmon0")
        wr(os.path.join(hw, "name"), "coretemp\n")
        wr(os.path.join(hw, "temp1_label"), "Package id 0\n")
        wr(os.path.join(hw, "temp1_input"), "51000\n")
        wr(os.path.join(hw, "temp1_max"), "84000\n")
        wr(os.path.join(hw, "temp1_crit"), "100000\n")


def write_ucode(fwroot, cpu="i5_650", revision=0x25):
    """Ship a matching microcode blob (the revision is the interesting part)."""
    fam, model, step = CPU_OPTIONS[cpu][0], CPU_OPTIONS[cpu][1], CPU_OPTIONS[cpu][2]
    name = "%02x-%02x-%02x" % (fam, model, step)
    wr(os.path.join(fwroot, "intel-ucode", name),
       microcode_blob(revision, 0x20180807, cpu_sig(fam, model, step)))


# Real IDs for the parts the 21.5-inch Mid-2010 iMac ships (spec §2, §12, §14,
# §16, §17, §20). Used to build fixtures; the detection code must never assume
# them — it has to find the IDs on the machine and only then name the part.
PCI_PARTS = {
    # addr      class       vendor device driver          model
    "sata":   ("0000:00:1f.2", 0x010601, 0x8086, 0x3b22, "ahci",         "5 Series/3400 SATA AHCI"),
    "wifi":   ("0000:02:00.0", 0x028000, 0x14e4, 0x4353, "b43",          "BCM43224 AirPort Extreme"),
    "eth":    ("0000:03:00.0", 0x020000, 0x14e4, 0x1684, "tg3",          "BCM5764M Gigabit Ethernet"),
    "sdxc":   ("0000:04:00.0", 0x080501, 0x14e4, 0x16bc, "sdhci-pci",    "BCM57765 SDXC/MMC reader"),
    "fw":     ("0000:05:00.0", 0x0c0010, 0x11c1, 0x5901, "firewire_ohci", "LSI FW643 FireWire 800"),
}


def write_pci_device(sysroot, key, revision=0x00, subsystem=(0x106b, 0x00b6)):
    addr, cls, vid, did, driver, _ = PCI_PARTS[key]
    # the driver directory itself, so the symlink below resolves like on a kernel
    os.makedirs(os.path.join(sysroot, "bus/pci/drivers", driver), exist_ok=True)
    base = os.path.join(sysroot, "bus/pci/devices", addr)
    wr(os.path.join(base, "class"), "0x%06x\n" % cls)
    wr(os.path.join(base, "vendor"), "0x%04x\n" % vid)
    wr(os.path.join(base, "device"), "0x%04x\n" % did)
    wr(os.path.join(base, "revision"), "0x%02x\n" % revision)
    wr(os.path.join(base, "subsystem_vendor"), "0x%04x\n" % subsystem[0])
    wr(os.path.join(base, "subsystem_device"), "0x%04x\n" % subsystem[1])
    symlink("../../../../bus/pci/drivers/%s" % driver, os.path.join(base, "driver"))
    return base


def write_target_peripherals(out, sysroot, procroot, fwroot):
    """Everything else the 21.5-inch Mid-2010 iMac has, so the new probes have
    something faithful to read. None of this is real hardware."""
    # --- PCI companions -------------------------------------------------
    write_pci_device(sysroot, "sata")
    write_pci_device(sysroot, "wifi")
    write_pci_device(sysroot, "eth")
    write_pci_device(sysroot, "sdxc")
    write_pci_device(sysroot, "fw")

    # --- firmware the image must ship -----------------------------------
    # b43 for a BCM43224 (PCIe N-PHY) needs the rev-29 MIMO ucode and the PCM
    # blob; these are the file names the Linux kernel actually asks for.
    for f in ("ucode29_mimo.fw", "ucode29_mimo_ps.fw", "pcm5.fw"):
        wr(os.path.join(fwroot, "b43", f), b"\x00" * 16)

    # --- Bluetooth 2.1 + EDR (Apple BCM2046B1 over USB) ----------------
    bt = os.path.join(sysroot, "class/bluetooth/hci0")
    wr(os.path.join(bt, "address"), "00:26:bb:aa:bb:cc\n")
    wr(os.path.join(bt, "name"), "hci0\n")
    wr(os.path.join(bt, "type"), "USB\n")
    wr(os.path.join(bt, "bus"), "2\n")

    # --- iSight camera (UVC, class 0x0e) -------------------------------
    v4l = os.path.join(sysroot, "class/video4linux/video0")
    wr(os.path.join(v4l, "name"), "Built-in iSight: Built-in iSight\n")
    wr(os.path.join(v4l, "index"), "0\n")
    wr(os.path.join(out, "dev/video0"), "")

    # --- SDXC: reader controller + a card in the slot ------------------
    card = os.path.join(sysroot, "class/mmc_host/mmc0/mmc0:0001")
    wr(os.path.join(card, "name"), "SD32G\n")
    wr(os.path.join(card, "type"), "SD\n")
    wr(os.path.join(card, "oemid"), "0x5344\n")
    wr(os.path.join(card, "manfid"), "0x000003\n")
    wr(os.path.join(card, "serial"), "0x1a2b3c4d\n")
    wr(os.path.join(card, "date"), "06/2019\n")
    wr(os.path.join(sysroot, "class/block/mmcblk0/size"), "%d\n" % (32 * 1000 * 1000 * 1000 // 512))
    wr(os.path.join(sysroot, "class/block/mmcblk0/removable"), "1\n")
    wr(os.path.join(sysroot, "class/block/mmcblk0p1/size"), "%d\n" % (31 * 1000 * 1000 * 1000 // 512))
    wr(os.path.join(sysroot, "class/block/mmcblk0p1/removable"), "1\n")
    wr(os.path.join(sysroot, "class/block/mmcblk0p1/partition"), "1\n")
    wr(os.path.join(out, "dev/mmcblk0"), "")
    wr(os.path.join(out, "dev/mmcblk0p1"), "")

    # --- slot-loading SuperDrive --------------------------------------
    sr = os.path.join(sysroot, "class/block/sr0")
    wr(os.path.join(sr, "removable"), "1\n")
    wr(os.path.join(sr, "size"), "0\n")           # no disc inserted in the fixture
    wr(os.path.join(sr, "device/model"), "DVDRW  GA32N\n")
    wr(os.path.join(sr, "device/vendor"), "HL-DT-ST\n")
    wr(os.path.join(sr, "device/type"), "5\n")
    wr(os.path.join(out, "dev/sr0"), "")

    # --- FireWire 800 --------------------------------------------------
    fwdev = os.path.join(sysroot, "bus/firewire/devices/fw0")
    wr(os.path.join(fwdev, "guid"), "0x0011223344556677\n")
    wr(os.path.join(fwdev, "vendor"), "0x000001\n")
    wr(os.path.join(fwdev, "model"), "0x000000\n")
    wr(os.path.join(out, "dev/fw0"), "")

    # --- CPU vulnerability reporting -----------------------------------
    vuln = os.path.join(sysroot, "devices/system/cpu/vulnerabilities")
    wr(os.path.join(vuln, "meltdown"), "Mitigation: PTI\n")
    wr(os.path.join(vuln, "spectre_v1"), "Mitigation: usercopy/swapgs barriers and __user pointer sanitization\n")
    wr(os.path.join(vuln, "spectre_v2"), "Mitigation: Retpolines\n")
    wr(os.path.join(vuln, "mds"), "Vulnerable: Clear CPU buffers attempted, no microcode\n")

    # --- an SD card the mount table knows about ------------------------
    wr(os.path.join(procroot, "self/mountinfo"),
       "25 0 8:2 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n"
       "30 25 8:1 /boot /boot ro,relatime shared:2 - vfat /dev/sda1 rw\n"
       "40 25 179:1 / /media/SDXC rw,relatime shared:3 - vfat /dev/mmcblk0p1 rw\n")


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
    for name, key in (("eth0", "eth"), ("wlan0", "wifi")):
        base = os.path.join(net, name)
        wired = name == "eth0"
        wr(os.path.join(base, "operstate"), "up\n" if wired else "down\n")
        wr(os.path.join(base, "address"), "00:26:bb:11:22:33\n" if wired else "00:26:bb:44:55:66\n")
        wr(os.path.join(base, "flags"), "0x1003\n" if wired else "0x1002\n")
        wr(os.path.join(base, "carrier"), "1\n" if wired else "0\n")
        wr(os.path.join(base, "speed"), "1000\n" if wired else "1\n")
        wr(os.path.join(base, "duplex"), "full\n")
        wr(os.path.join(base, "statistics/rx_bytes"), "0\n")
        wr(os.path.join(base, "statistics/tx_bytes"), "0\n")
        # /sys/class/net/<if> links to the PCI function, so the driver's own
        # vendor/device ids are reachable exactly as on a real kernel. A fixture
        # that stops at the interface name would let a probe claim a chipset it
        # never identified (spec §12: identify the exact chipset first).
        symlink("../../../bus/pci/devices/%s" % PCI_PARTS[key][0],
                os.path.join(base, "device"))
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

    # USB: keyboard, mouse, mass storage, the built-in iSight camera and the
    # Bluetooth controller — all five of the classes the probes must tell apart.
    usb = os.path.join(sysroot, "bus/usb/devices")
    for port, vid, pid, cls, proto, prod, drv, speed in (
            ("1-1", 0x05ac, 0x0245, 0x03, 0x01, "Apple Internal Keyboard / Trackpad", "usbhid", 12),
            ("1-2", 0x0781, 0x5567, 0x03, 0x02, "Logitech USB Optical Mouse", "usbhid", 12),
            ("1-3", 0x05ac, 0x8502, 0x0e, 0x00, "Built-in iSight", "uvcvideo", 480),
            ("1-4", 0x05ac, 0x8215, 0xe0, 0x01, "Apple Bluetooth", "btusb", 12),
            ("2-1", 0x0781, 0x5567, 0x08, 0x50, "SanDisk Cruzer Blade", "usb-storage", 480)):
        base = os.path.join(usb, port)
        wr(os.path.join(base, "idVendor"), "0x%04x\n" % vid)
        wr(os.path.join(base, "idProduct"), "0x%04x\n" % pid)
        wr(os.path.join(base, "product"), prod + "\n")
        wr(os.path.join(base, "manufacturer"), "Apple Inc.\n")
        wr(os.path.join(base, "speed"), "%d\n" % speed)
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

    # system facts: the CPU option this machine was configured with, with the
    # matching microcode blob in the firmware root, plus every other peripheral
    cpu_opt = "i5_650" if which == "imac11_2" else "i7_870"
    fwroot = os.path.join(out, "fw")
    wr(os.path.join(procroot, "meminfo"), "MemTotal:        8192000 kB\nMemAvailable:    7100000 kB\n")
    if which == "imac11_2":
        write_cpu(sysroot, procroot, cpu=cpu_opt, cores=2, threads=4, microcode=0x1A)
    else:
        write_cpu(sysroot, procroot, cpu=cpu_opt, cores=4, threads=8, microcode=0x0F)
    write_ucode(fwroot, cpu=cpu_opt, revision=0x1F)
    write_target_peripherals(out, sysroot, procroot, fwroot)
    wr(os.path.join(procroot, "net/dev"),
      "Inter-|   Receive                                                |  Transmit\n"
      " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets\n"
      "    lo:   10000      10    0    0    0     0          0         0    10000      10\n")
    wr(os.path.join(procroot, "net/route"),
      "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
      "eth0\t00000000\t0A000002\t0003\t0\t0\t0\t00000000\t0\t0\t0\n")
    wr(os.path.join(procroot, "net/if_inet6"), "")
    # mountinfo (including the SD card mount) is written by
    # write_target_peripherals() above, in one place only
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
