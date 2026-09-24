# G1OS — Giving life to older machines.
### iMac Mid-2010 Operating System & Installation Guide

An ultra-lightweight, classic macOS-inspired operating system engineered natively in C11 specifically for legacy Apple hardware, with first-class support for the **iMac (21.5-inch & 27-inch, Mid-2010 — `iMac11,2` / `iMac11,3`)**.

---

## Table of Contents
1. [Target Machine Specifications](#target-machine-specifications)
2. [Prerequisites](#prerequisites)
3. [Step 1: Obtain or Build `MacLiteOS.iso`](#step-1-obtain-or-build-macliteosiso)
4. [Step 2: Flash the ISO to a USB Pendrive](#step-2-flash-the-iso-to-a-usb-pendrive)
   - [Method A: Rufus (Windows) — Recommended](#method-a-rufus-windows--recommended)
   - [Method B: BalenaEtcher (Windows / macOS / Linux)](#method-b-balenaetcher-windows--macos--linux)
   - [Method C: Linux / macOS Terminal (`dd`)](#method-c-linux--macos-terminal-dd)
5. [Step 3: Booting the USB Drive on your iMac](#step-3-booting-the-usb-drive-on-your-imac)
6. [Step 4: Testing Hardware in Live Mode](#step-4-testing-hardware-in-live-mode)
7. [Step 5: Installing to Internal Storage (HDD / SSD)](#step-5-installing-to-internal-storage-hdd--ssd)
8. [Step 6: Post-Installation & Fan Management](#step-6-post-installation--fan-management)
9. [Keyboard Shortcuts & Apple Controls](#keyboard-shortcuts--apple-controls)
10. [Troubleshooting & FAQ](#troubleshooting--faq)

---

## Target Machine Specifications

| Hardware Component | Reference Spec (iMac 21.5" Mid-2010 `iMac11,2`) | MacLiteOS Support & Driver |
|---|---|---|
| **CPU** | Intel Core i3-540 / i3-550 / i5-680 (Clarkdale 32nm) | Supported x86_64, `mitigations=off` for peak responsiveness |
| **RAM** | 4 GB to 16 GB DDR3-1333 | System idles at **< 250 MB RSS** (runs fast on 4 GB) |
| **GPU** | ATI Radeon HD 4670 (256MB) / HD 5670 (512MB) | Open-source `radeon` driver + KMS + Mesa `r600` Gallium GL |
| **Display** | 21.5" 1920 × 1080 LED-backlit IPS | Native 1080p 60Hz modesetting via DRM |
| **Backlight** | Radeon PWM hardware brightness | `acpi_backlight=native` routes directly to `radeon_bl0` |
| **Audio** | Realtek ALC889 / Cirrus CS4206 HD Audio | `snd_hda_intel model=imac27` (internal speakers + headphone sense) |
| **Fans & SMC** | 3 fans: Optical (ODD), Hard Drive (HDD), CPU | `maclite-fan` dynamically controls Apple SMC targets |
| **Wi-Fi** | Broadcom AirPort Extreme 802.11a/b/g/n (BCM43224) | `b43` driver (`b43.fwok=1` with firmware included) |
| **Ethernet** | Broadcom NetXtreme BCM5764M Gigabit | `tg3` kernel module |
| **Bluetooth** | Apple Broadcom BCM2046B1 USB (05ac:8215) | `btusb` module |
| **Boot Mode** | 64-bit Apple EFI 1.10 | GRUB `x86_64-efi` with `reboot=pci` |

---

## Prerequisites

1. **USB Flash Drive (Pendrive)**:
   - Minimum capacity: **4 GB** (8 GB recommended).
   - *Warning: All existing data on this USB drive will be erased.*
2. **Target Computer**:
   - Apple iMac 21.5-inch Mid-2010 (`iMac11,2`, Model A1311, EMC 2389) or iMac 27-inch (`iMac11,3`).
   - Wired USB keyboard and mouse (recommended for initial boot selection).
3. **Flashing Utility**:
   - [Rufus](https://rufus.ie/) (Windows) or [BalenaEtcher](https://etcher.balena.io/) (Windows/Mac/Linux).

---

## Step 1: Obtain or Build `MacLiteOS.iso`

If assembling the ISO from the codebase:

```bash
cd macliteos
sh scripts/build.sh
```
The ISO output will be generated at:
`macliteos/out/MacLiteOS.iso`

---

## Step 2: Flash the ISO to a USB Pendrive

### Method A: Rufus (Windows) — Recommended
1. Download and run **Rufus** (portable version works fine).
2. Insert your USB flash drive into your PC.
3. Under **Device**, select your USB flash drive.
4. Under **Boot selection**, click **SELECT** and choose `MacLiteOS.iso`.
5. Under **Partition scheme**, select **GPT**.
6. Under **Target system**, choose **UEFI (non CSM)**.
7. Click **START**.
8. If Rufus asks to write in *ISO Image mode* or *DD Image mode*, select **DD Image mode** (this ensures the Apple EFI hybrid partition structure is preserved intact).
9. Click **OK** to format the drive and write the image.

---

### Method B: BalenaEtcher (Windows / macOS / Linux)
1. Open **BalenaEtcher**.
2. Click **Flash from file** and select `MacLiteOS.iso`.
3. Click **Select target** and choose your USB flash drive.
4. Click **Flash!** and grant administrator permissions when prompted.

---

### Method C: Linux / macOS Terminal (`dd`)
1. Insert the USB drive and identify its device node:
   - Linux: `lsblk` (e.g. `/dev/sdb`)
   - macOS: `diskutil list` (e.g. `/dev/disk2`)
2. Unmount the drive:
   - Linux: `sudo umount /dev/sdX*`
   - macOS: `diskutil unmountDisk /dev/diskN`
3. Write the image:
   ```bash
   # Linux (replace /dev/sdX with your actual USB disk, NOT a partition)
   sudo dd if=MacLiteOS.iso of=/dev/sdX bs=4M status=progress conv=fsync

   # macOS (replace /dev/rdiskN with your actual raw disk node)
   sudo dd if=MacLiteOS.iso of=/dev/rdiskN bs=4m status=progress
   ```

---

## Step 3: Booting the USB Drive on your iMac

1. Turn off your iMac Mid-2010 completely.
2. Insert the prepared USB flash drive into one of the **rear USB 2.0 ports** (avoid external non-powered USB hubs).
3. Connect an Apple or standard USB keyboard directly to the iMac.
4. Press the **Power button** on the back of the iMac, and **immediately press and hold the `Option` (⌥) key** (or the `Alt` key on a standard Windows keyboard).
5. Keep holding `Option` until the grey Apple **Startup Manager** screen appears.
6. You will see icons for your internal drive and an orange/yellow external drive icon labelled **`EFI Boot`** or **`MacLiteOS`**.
7. Use the arrow keys to highlight the **`EFI Boot`** icon, then press **Enter** (Return).
8. The GRUB boot menu will load:
   - Select **`G1OS (Safe Graphics - EFI Framebuffer / Software Compositing)`** (Recommended for Mid-2010 iMacs to bypass Apple EFI VBIOS initialization).
   - Or select **`G1OS (Default - Radeon KMS)`** for hardware-accelerated modesetting.
   - For diagnostics, select **`G1OS (Safe Graphics + Verbose Debug)`** or **`G1OS Recovery Shell`**.
9. The native G1OS boot splash will display, followed directly by the **Mica Desktop Session**.

---

## Step 4: Testing Hardware in Live Mode

MacLiteOS includes built-in hardware diagnostics in the live environment so you can verify all hardware before touching your disk.

Open the terminal or switch to console and run:

```bash
# Quick hardware verification (~30 seconds)
sh scripts/hardware-check.sh --quick
```

This verifies:
- **GPU**: ATI Radeon HD 4670/5670 detection and open-source `radeon` KMS driver binding.
- **Display**: Native 1920×1080 panel resolution and 60Hz timing.
- **Brightness**: Writes and read-backs against `radeon_bl0`.
- **Sound**: ALSA audio tones through the internal stereo speakers.
- **Network**: Broadcom Gigabit Ethernet (`tg3`) and Wi-Fi (`b43`).
- **Thermal Sensors**: CPU core temperatures and SMC fan speeds.

---

## Step 5: Installing to Internal Storage (HDD / SSD)

The primary and recommended method to install MacLiteOS is directly through the **graphical user interface**—no terminal commands are required.

> [!WARNING]
> Installing MacLiteOS will wipe the selected target disk. Make sure you have backed up any important personal data from the iMac before proceeding.

### Primary Method: Click-to-Install macOS GUI Wizard

1. On the live desktop or Dock, click the prominent **“Install MacLiteOS”** icon.
2. **Stage 1 — Welcome Screen**:
   - Displays: **“Welcome to MacLiteOS”** · **“Give life to older machines.”**
   - Highlights MacLiteOS features for vintage iMacs, including Apple EFI 1.1 fallback support and automatic USB protection.
   - Click **Continue to Hardware Check**.
3. **Stage 1.5 — Hardware Pre-Flight Audit**:
   - Verifies CPU architecture (x86_64), RAM (>= 2GB), GPU mode (Safe Graphics / EFI GOP Framebuffer), internal SATA controller, USB write-protection, and 100% offline self-contained package readiness.
   - Click **Proceed to Select Destination**.
4. **Stage 2 — Select Installation Destination**:
   - The installer scans storage hardware and controller buses.
   - **Internal Storage Preferred**: Automatically selects internal SATA SSD/HDD (e.g. `Crucial CT500MX500SSD1` `/dev/sda`).
   - **Live USB Safeguard**: Automatically identifies the live boot device (e.g. `/dev/sdb`) and locks it with a *“LOCKED / LIVE USB”* badge to prevent accidental overwrites.
   - Displays disk model, capacity, device identifier, and existing partitions.
   - Click **Continue**.
4. **Stage 3 — Interactive Confirmation**:
   - Displays a prominent summary:
     - **Install MacLiteOS on:** Internal SSD/HDD
     - **Model:** `Crucial CT500MX500SSD1`
     - **Size:** `500.1 GB`
     - **Device:** `/dev/sda`
   - Explicit warning: *“This will erase the selected disk and all existing partitions.”*
   - Check the confirmation box: *“I understand that all data on /dev/sda will be permanently erased.”*
   - Click **Erase & Install MacLiteOS** (destructive red button).
5. **Stage 4 — Automated Installation & Progress**:
   - Smooth animated progress stages:
     **Preparing → Detecting Disk → Partitioning → Formatting → Installing → Configuring Boot → Finalizing**
   - Wipes legacy partition signatures (`wipefs -a` & `sgdisk --zap-all`).
   - Allocates Apple-compatible GPT partitions:
     - `MACLITE_BOOT` (256 MB FAT32 EFI partition)
     - `MACLITE_DATA` (4 GB+ ext4 persistent storage for `/var/data`)
     - `MACLITE_BASE` (Immutable base system `/`)
   - Configures Apple EFI fallback bootloader at `/EFI/BOOT/BOOTX64.EFI` and `.disk_label` ("MacLiteOS").
   - **UUID Boot Binding**: Binds GRUB configuration and `/etc/fstab` to unique filesystem UUIDs (`search --fs-uuid` and `root=UUID=...`) rather than device nodes or ambiguous labels, completely eliminating dependencies on the live USB.
   - Collapsible **“Installation Details”** log viewer available for advanced users.
6. **Stage 5 — Offline Pre-Flight Verification Pass**:
   - Before reporting completion, the installer runs an automated pre-flight audit on the internal disk:
     - ✓ Internal EFI partition exists and is mountable
     - ✓ Fallback EFI loader `BOOTX64.EFI` exists and is valid
     - ✓ `grub.cfg` references internal boot UUID and contains zero live USB references
     - ✓ Linux kernel (`vmlinuz-maclite`) and initramfs (`initrd-maclite.img`) exist
     - ✓ Apple Option Boot Picker label (`.disk_label`) exists
     - ✓ Root filesystem is resolvable without USB media
7. **Stage 6 — Installation Complete & First Boot**:
   - Displays:
     **“MacLiteOS Installation Complete”**
     **“Remove the USB drive and restart your iMac.”**
   - Displays verified component checklist.
   - Unplug your USB flash drive.
   - Click the large **Restart iMac Now** button.
   - On reboot, the iMac will directly load MacLiteOS from the internal drive! If holding the Option (⌥) key at the startup chime, *MacLiteOS* appears with its branded drive icon.

---

### Advanced / Scripted Fallback: Terminal-Based Installation

For headless, serial, or automated deployments, the CLI installer remains available and delegates to the same robust backend engine:
```bash
sudo maclite-install
```
Follow the interactive prompts or pass `--target /dev/sda --yes` for unattended execution.


---

## Step 6: Post-Installation & Fan Management

### The "Jet Engine" Fan Noise Solution
On 2010 iMacs, if the factory hard drive was upgraded to an aftermarket SATA SSD, Apple's proprietary hard drive temperature sensor is missing, causing Apple SMC firmware to run the HDD fan at full 6,000 RPM.

MacLiteOS solves this with the built-in [maclite-fan](file:///c:/Users/jeeva/Desktop/Jeevan/MacOSLite/macliteos/diagnostics/maclite-fan.c) utility:

```bash
# Check fan RPMs and current temperatures:
maclite-fan --status

# Regulate fans to quiet baseline (ODD: 1000 RPM, HDD: 1100 RPM, CPU: 1200 RPM):
maclite-fan --quiet

# Run automatic thermal governor daemon:
maclite-fan --daemon
```

---

## Keyboard Shortcuts & Apple Controls

| Shortcut | Function |
|---|---|
| `Super` (Command ⌘ or Windows Key) | Toggle App Launcher |
| `Alt + Tab` | Switch active windows |
| `F1` / `F2` | Brightness down / up |
| `F10` / `F11` / `F12` | Mute / Volume down / Volume up |
| `Super + Space` | Spotlight / Quick Search |
| `Super + Q` | Close focused window |
| `Super + Return` | Open Terminal |

*Note: In [maclite-apple.conf](file:///c:/Users/jeeva/Desktop/Jeevan/MacOSLite/macliteos/rootfs/etc/modprobe.d/maclite-apple.conf), `fnmode=2` and `swap_opt_cmd=1` are preset so the keyboard layout behaves naturally on standard Apple keyboards.*

---

## Troubleshooting & FAQ

### 1. iMac doesn't show the USB drive on the Startup Manager screen
- Ensure you used **DD Image mode** in Rufus or used **BalenaEtcher**.
- Plug the USB drive directly into the rear USB ports on the iMac, not into an unpowered hub or keyboard USB port.
- Reset the iMac PRAM/NVRAM: Turn on the iMac and immediately hold `Option + Command + P + R` until you hear the second boot chime, then release and hold `Option`.

### 2. Screen goes black after GRUB
- The kernel command line includes `acpi_backlight=native radeon.modeset=1`. If your panel stays black, reboot into the second menu option:
  `MacLiteOS (safe graphics: software compositing)`.

### 3. iMac hangs on shutdown or restart
- MacLiteOS incorporates `reboot=pci` into the bootloader to resolve Apple's EFI 1.10 ACPI shutdown quirk. Verify that `reboot=pci` is present in `/boot/grub/grub.cfg`.
