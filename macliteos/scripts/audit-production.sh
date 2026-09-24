#!/bin/sh
# Static production-readiness audit. This does not modify disks or boot media.
set -eu
cd "$(dirname "$0")/.."
ISO=${1:-}
FAIL=0
pass(){ echo "PASS: $*"; }
fail(){ echo "FAIL: $*" >&2; FAIL=1; }
need_file(){ [ -e "$1" ] && pass "source present: $1" || fail "source missing: $1"; }
need_exec(){ [ -x "$1" ] && pass "executable present: $1" || fail "executable missing: $1"; }

for f in \
  boot/g1os-init boot/g1os-ui-health.c boot/g1os-failure-ui.c boot/grub-efi.cfg \
  boot/initrd.list compositor/comp.c installer/mica-installer.c installer/maclite-installer-backend \
  scripts/build.sh scripts/make-initrd.sh scripts/make-iso.sh scripts/validate-boot-artifacts.sh Makefile; do
  need_file "$f"
done

grep -F '/usr/bin/mica-comp --backend "$BACKEND"' boot/g1os-init >/dev/null \
  && pass "live installer starts compositor without desktop --session" \
  || fail "live installer still uses desktop session path"
grep -F '/usr/bin/mica-installer' boot/g1os-init >/dev/null \
  && pass "live installer explicitly launches mica-installer" \
  || fail "mica-installer launch is missing"
grep -F 'Graphical installer ready: compositor responsive and installer surface present' boot/g1os-init >/dev/null \
  && pass "graphical readiness requires compositor + installer health" \
  || fail "graphical readiness gate missing"
grep -F 'g1os-failure-ui' boot/g1os-init >/dev/null \
  && pass "startup failures use framebuffer diagnostic UI" \
  || fail "startup failure diagnostic UI missing"

grep -F 'video=efifb' boot/grub-efi.cfg >/dev/null && pass "Safe Graphics preserves EFI framebuffer" || fail "Safe Graphics disables/omits EFI framebuffer"
grep -F 'nomodeset' boot/grub-efi.cfg >/dev/null && pass "Safe Graphics requests conservative GPU mode" || fail "Safe Graphics command line missing"
grep -F 'panic=-1' boot/grub-efi.cfg >/dev/null && pass "kernel panic does not trigger automatic reboot" || fail "panic reboot behavior is unsafe"

for b in mica-comp mica-installer mica-shell g1os-ui-health g1os-failure-ui; do
  need_exec "out/$b"
done
for b in mica-comp mica-installer mica-shell g1os-ui-health; do
  grep -F "out/$b" scripts/make-initrd.sh >/dev/null || fail "initrd does not require current build output for $b"
done

grep -F 'Re-apply the current build outputs after rootfs/usr was copied.' scripts/make-iso.sh >/dev/null \
  && pass "ISO prevents rootfs binaries from overwriting fresh build outputs" \
  || fail "ISO may package stale rootfs binaries"
grep -F 'runtime-provenance.sha256' scripts/make-iso.sh >/dev/null \
  && pass "ISO records runtime binary provenance" \
  || fail "runtime provenance is missing"

for f in boot/g1os-init scripts/make-initrd.sh scripts/make-iso.sh; do
  if grep -nE '(^|;)[[:space:]]*(true|:)[[:space:]]*(#|$)' "$f" >/dev/null; then
    fail "suspicious unconditional success token in $f"
  else
    pass "no unconditional success token in $f"
  fi
done

if [ -n "$ISO" ]; then
  [ -s "$ISO" ] || fail "ISO is missing or empty: $ISO"
  if command -v xorriso >/dev/null 2>&1; then
    LIST=$(mktemp)
    trap 'rm -f "$LIST"' EXIT
    xorriso -indev "$ISO" -find / -exec report_lba >"$LIST" 2>/dev/null || fail "cannot inspect ISO"
    for p in boot/vmlinuz-maclite boot/initrd-maclite.img live/maclite-base.sqfs EFI/BOOT/BOOTX64.EFI; do
      grep -Fi "$p" "$LIST" >/dev/null && pass "ISO contains $p" || fail "ISO missing $p"
    done
  else
    fail "xorriso is required for ISO inspection"
  fi
fi

if [ "$FAIL" -ne 0 ]; then
  echo "PRODUCTION AUDIT: FAIL" >&2
  exit 1
fi
echo "PRODUCTION AUDIT: PASS"
