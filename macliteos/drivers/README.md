# drivers/

Userspace driver policy + probes. Real kernel drivers come from the curated
config in kernel/configs; this directory holds what MacLiteOS adds on top:

- `hwprobe` (implemented in ../hardware/hwprobe.c): GPU/CPU/RAM detection,
  VRAM sizing, acceleration-capability table and the blend benchmark used to
  pick a performance mode automatically at first boot.
- `udev-rules/maclite.rules`: permissions for /dev/dri, uinput-free input,
  and backlight on iMacs (radeon bl node), nothing else.

- `catalog/maclite-offline.cat`: every driver, firmware and microcode release
  this image ships, with the hardware ids each one covers and the SHA-256 of
  each file. It is data, not code, so a support decision is a diffable line —
  and so `maclite-drivers` can apply the spec's "newest *compatible*, never
  newest" rule mechanically instead of by hand.
- `maclite-drivers` (../diagnostics/maclite-drivers.c) reads that catalog:
  detect / status / check / update / verify / rollback, with the policy and the
  rollback model written down in ../docs/drivers.md.

No polling daemons live here or anywhere else (spec §13): status readers in
compositor/client/shellkit.c are event- or on-demand driven, and the driver
resolver runs only when a human runs it.
