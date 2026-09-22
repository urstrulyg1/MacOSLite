# Drivers, firmware and microcode on MacLiteOS (spec §4, §5, §6, §25–§30)

MacLiteOS installs nothing it cannot name, verify and undo. This document is the
policy; `drivers/catalog/maclite-offline.cat` is the data it is applied to, and
`hardware/driver.c` is the code that applies it.

## The chain

For this machine the resolver walks exactly the chain the spec asks for:

    GPU (real PCI id) → compatible kernel DRM driver → Mesa driver
    → required firmware → OpenGL acceleration → video acceleration

and the same method for everything else the iMac has: Wi-Fi, Ethernet,
Bluetooth, audio, SATA, card reader, FireWire, camera, CPU microcode.

Two rules decide what gets installed:

1. **Newest compatible, never newest.** Every component may appear in the
   catalog several times. The resolver walks the releases, skips any whose
   `target` ids do not cover this machine, skips `status=testing` and
   `status=broken`, skips anything whose `requires` the machine fails, and picks
   the highest surviving version. The rejected releases and the reason are
   printed — a report that says only "installed 24.0.9" hides the important part.
2. **Nothing is written without a digest.** A file with `sha256=<hex>` is hashed
   from the repository copy and compared before it is copied. A file marked
   `sha256=unpinned` can be read and hashed but not installed unless the operator
   passes `--allow-unpinned`, and its integrity is reported **NOT TESTED** — an
   unpinned digest is not a pass.

## Commands

    maclite-drivers detect     identify the machine, component by component
    maclite-drivers status     what is recorded, next to what is true right now
    maclite-drivers check      what the resolver would do, with every skip explained
    maclite-drivers update     install the newest compatible releases
    maclite-drivers verify     validate the running configuration, record it
    maclite-drivers rollback   restore the previous known-good files

Exit codes follow the house contract: `0` satisfied, `1` a required check
failed, `2` not tested here (a reboot is pending, or a component is missing from
the catalog), `3` unsupported, `4` usage.

Useful flags: `--repo DIR`, `--catalog FILE`, `--root DIR` (install root, so a
test can use a throwaway tree), `--component NAME`, `--online`,
`--allow-unpinned`, `--auto-rollback`.

## Rollback (§30)

`update` snapshots every destination before it writes:

    <root>/var/lib/maca-lite/known-good/<path>          the previous contents
    <root>/var/lib/maca-lite/known-good/<path>.absent   "there was no file here"

The snapshot of a destination is taken **once** — a later install does not
overwrite it, because the record means "the configuration that was known good",
not "the state five minutes ago". `rollback` restores every file and deletes
every path that is marked absent, which is what makes "undo the driver" mean
something. Microcode is installed twice on purpose: into
`lib/firmware/intel-ucode/` and into `/boot/maclite-ucode/<signature>`, because
only the early copy can change the CPUID bits a newer revision adds.

`verify` runs the validation that the install could not run: is the DRM driver
bound, is KMS up, does the running microcode revision match what the image
ships, is the needed firmware present. It writes
`/var/lib/maca-lite/driver-state`:

    hardware-id, kernel, gpu-driver, mesa, firmware-digest,
    microcode, validation, timestamp, known-good=<name=version ...>

If validation fails and `--auto-rollback` was given, the previous known-good
files are restored and the tool exits non-zero.

Note what `verify` refuses to do: when fixture roots are set there is no running
configuration to validate, so the answer is **NOT TESTED** and the state file is
not written at all. A tool that records success it did not observe is worse than
no tool.

## Where driver code may come from (§28)

- The catalog that ships inside the image is the trust anchor (this tree's
  `drivers/catalog/maclite-offline.cat`).
- A catalog may name a signing key. If it does, `maclite-drivers` checks the
  signature with `gpgv` (or `openssl dgst -verify`) **before** anything is
  installed, and refuses to install if the signature fails.
- If no verifier is installed, provenance is reported **NOT TESTED** — not
  assumed good.
- If the catalog's key is a URL, it is refused. A key that arrives over the
  network cannot be the thing that authorises the network.
- `update` never fetches anything unless `--online` is given, and offline boot
  never needs it: the image ships what this machine needs to reach the desktop.

## What is deliberately absent

No NVIDIA driver, no unrelated Intel GPU support, no second AMD stack, no
firmware that this hardware never asks for, no development packages, no
compiler toolchain, no debug packages, no vendor daemons. Every installed file
exists because a `file` line in the catalog names it for a `target` this machine
reports. There is no updater process, no polling and no telemetry: `update` runs
when a human runs it.

## Adding a release

Append to the catalog and pin the digests:

    package mesa-r600 24.0.9
      kind mesa
      status stable
      provider mesa
      target gpu:1002:9490      # RV730 (HD 4670)
      target gpu:1002:68c1      # Redwood (HD 5670)
      requires mesa>=20.0.0
      install usr/lib/x86_64-linux-gnu/dri
      file r600_dri.so sha256=<64 hex digits>
      note "why this line exists"

Grammar, target forms and the meaning of every key are documented at the top of
the catalog file itself. `kind` decides the default install prefix (firmware →
`lib/firmware`, microcode → `lib/firmware/intel-ucode`, mesa → the DRI
directory, kernel/drm → `lib/modules`). `status=in-kernel` means "provided by
the running kernel": nothing is installed, and the resolver instead checks that
`driver <name>` is actually bound — a package cannot claim a driver is present.
