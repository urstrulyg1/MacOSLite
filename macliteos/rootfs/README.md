# rootfs/

Read-only base + writable user data (spec §12).

- `etc/` ships the machine-wide defaults: performance mode, dock prefs,
  autostart list (empty by default — nothing starts that you didn't ask for).
- At runtime the base squashfs is mounted read-only at /; user writes go to
  MACLITE_DATA (ext4) mounted at /home and /etc/maca-lite (overlay).
- No package ever writes to the base volume.
