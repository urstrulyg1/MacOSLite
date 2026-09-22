# drivers/

Userspace driver policy + probes. Real kernel drivers come from the curated
config in kernel/configs; this directory holds what MacLiteOS adds on top:

- `hwprobe` (implemented in ../hardware/hwprobe.c): GPU/CPU/RAM detection,
  VRAM sizing, acceleration-capability table and the blend benchmark used to
  pick a performance mode automatically at first boot.
- `udev-rules/maclite.rules`: permissions for /dev/dri, uinput-free input,
  and backlight on iMacs (radeon bl node), nothing else.

No polling daemons live here or anywhere else (spec §13): status readers in
compositor/client/shellkit.c are event- or on-demand driven.
