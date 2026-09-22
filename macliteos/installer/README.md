# installer/

Installer flow (spec §51): live session -> partition -> copy base (read-only
squashfs) -> create writable user volume -> install boot chain.

v0.1 status: the flow is implemented as a guided shell program
(`maclite-install`) that runs *inside* a live MacLiteOS session. It refuses to
touch disks without an explicit, typed confirmation and prints every command
before running it. GUI installer pages come after the ISO boots (ROADMAP).

Layout it creates:
  MACLITE_BOOT   256 MB FAT32   kernel, initrd, grub (EFI + BIOS fallback)
  MACLITE_BASE   rest minus 4G  squashfs, mounted read-only
  MACLITE_DATA   4 GB+  ext4     /home, /etc/maca-lite (writable user data)
