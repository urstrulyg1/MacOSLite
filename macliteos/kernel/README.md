# kernel/

MacLiteOS ships a curated kernel configuration, not a forked kernel.
Rationale (spec §2, §52): a 2010 iMac needs mature radeon/b43/tg3/HDA drivers;
re-implementing them would trade reliability for ideology. Our contributions
live in userspace: compositor, window manager, shell, apps, tools.

- `configs/maclite-x86_64.defconfig` — the config we benchmark with.
- `patches/` — empty on purpose; we carry no out-of-tree kernel patches in v0.1.
  If a quirk needs one, it lands here with a measurement attached.
