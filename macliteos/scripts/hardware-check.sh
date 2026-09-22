#!/bin/sh
# MacLiteOS hardware check — run this ON THE iMAC (spec §16/§21/§24).
#
# It does three things and separates them strictly:
#
#   1. runs every automated check that can run on the machine and records the
#      real exit code of each one (0 PASS, 1 FAIL, 2 NOT TESTED, 3 UNSUPPORTED);
#   2. prints the checks a human has to confirm (picture on the panel, sound
#      from the speakers, Wi-Fi association, sleep/wake behaviour, video
#      smoothness) and takes the answer as an explicit y/n — an unanswered
#      human check is recorded NOT TESTED, never PASS;
#   3. writes one report file: out/hw-report-<date>.txt plus a machine-readable
#      out/hw-report-<date>.tsv that can be pasted into docs/testing.md.
#
# Nothing here is destructive: no mode change unless --set-mode, no suspend
# unless --allow-suspend, no writes outside /tmp and out/.
#
# Usage: sh scripts/hardware-check.sh [--quick] [--allow-suspend] [--set-mode]
#        sh scripts/hardware-check.sh --yes "description"   (scripted answers)
cd "$(dirname "$0")/.." || exit 1

QUICK=0
ALLOW_SUSPEND=0
SET_MODE=0
ANSWERS_FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
    --quick) QUICK=1 ;;
    --allow-suspend) ALLOW_SUSPEND=1 ;;
    --set-mode) SET_MODE=1 ;;
    --yes) shift
           [ -n "$ANSWERS_FILE" ] || ANSWERS_FILE=$(mktemp)
           printf '%s\n' "$1" >> "$ANSWERS_FILE" ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# The tier label must reflect where this actually ran: an iMac (DMI says Apple)
# gets IMAC, anything else is labelled for what it is. A check that ran in a
# container is not iMac evidence.
DMI_VENDOR=$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || echo unknown)
case "$DMI_VENDOR" in
*Apple*) TIER=IMAC ;;
*)       TIER=HOST ;;
esac

DATE=$(date -u '+%Y%m%d')
REPORT="out/hw-report-$DATE.txt"
TSV="out/hw-report-$DATE.tsv"
mkdir -p out
: > "$TSV"
FAILED=0
HUMAN_UNTESTED=0

log() { printf '%s\n' "$*" | tee -a "$REPORT"; }
section() { log ""; log "== $1 =="; }
rule() { log "----------------------------------------------------------------------------"; }
tsv() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$TSV"; }

{
    log "MacLiteOS hardware check"
    log "date      $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    log "machine   $(cat /sys/class/dmi/id/product_name 2>/dev/null || echo unknown)"
    log "vendor    $(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || echo unknown)"
    log "kernel    $(uname -srmo)"
    log "cmdline   $(cat /proc/cmdline)"
    log "hw tier   $TIER (IMAC = the Apple machine itself; HOST = any other machine,"
    log "          which makes these rows evidence about that host only)"
} 

# read an answer for a human check; default is NOT TESTED.
# --yes answers come from a file so that the queue survives command substitution.
ask() {
    prompt=$1
    if [ -n "$ANSWERS_FILE" ] && [ -s "$ANSWERS_FILE" ]; then
        ans=$(sed -n '1p' "$ANSWERS_FILE")
        sed -i '1d' "$ANSWERS_FILE"
        printf '  %s -> [scripted: %s]\n' "$prompt" "$ans" >&2
        printf '%s' "$ans"
        return
    fi
    printf '  %s [y/n/skip]: ' "$prompt" >&2
    read -r ans
    printf '%s' "$ans"
}

human() {
    name=$1; prompt=$2
    ans=$(ask "$prompt")
    case "$ans" in
    y|Y|yes) log "  $name: PASS (human confirmed: $prompt)"; tsv "$name" "$TIER" PASS "human confirmed" ;;
    n|N|no)  log "  $name: FAIL (human reported it did NOT work)"; tsv "$name" "$TIER" FAIL "human reported failure"; FAILED=1 ;;
    *)       log "  $name: NOT TESTED (not confirmed by a human)"; tsv "$name" "$TIER" "NOT TESTED" "not confirmed"; HUMAN_UNTESTED=1 ;;
    esac
}

# automated(name, command...)
auto() {
    name=$1; shift
    out=$(mktemp)
    "$@" > "$out" 2>&1
    rc=$?
    case $rc in
    0) verdict="PASS" ;;
    1) verdict="FAIL" ; FAILED=1 ;;
    2) verdict="NOT TESTED" ;;
    3) verdict="UNSUPPORTED" ;;
    4) verdict="USAGE ERROR"; FAILED=1 ;;
    *) verdict="FAIL (exit $rc)"; FAILED=1 ;;
    esac
    log ""
    log "-- $name  [$verdict  exit $rc]"
    sed 's/^/   /' "$out" >> "$REPORT"
    tsv "$name" "$TIER" "$verdict" "exit $rc"
    rm -f "$out"
}

section "1. automated checks (exit codes are the source of truth)"
auto "maclite-hardware" out/maclite-hardware
auto "maclite-gpu" out/maclite-gpu
auto "maclite-display" out/maclite-display --modes
auto "maclite-brightness" out/maclite-brightness status
auto "maclite-audio" out/maclite-audio list
auto "maclite-network" out/maclite-network
auto "maclite-usb" out/maclite-usb
auto "maclite-storage" out/maclite-storage --bench /tmp
auto "maclite-power" out/maclite-power
auto "maclite-gpu-benchmark (damage path)" out/maclite-gpu-benchmark --frames 300
auto "maclite-gpu-benchmark (fullscreen worst case)" out/maclite-gpu-benchmark --fullscreen --frames 300
auto "maclite-gpu-benchmark (1080p fullscreen)" out/maclite-gpu-benchmark --fullscreen --frames 300
if [ "$QUICK" = 0 ]; then
    auto "maclite-video-test (720p + 1080p, hw preferred)" sh -c \
        'out/maclite-video-test h264 mpeg2 --res 1080 --perf'
fi

section "2. resource goals (spec §25: idle 150-300 MB, <500 MB hard, CPU 0-2 %)"
if [ -x out/maclite-memory ]; then
    auto "maclite-memory (idle)" sh -c 'sleep 5; out/maclite-memory'
else
    log "  maclite-memory missing"; tsv "maclite-memory" "$TIER" "NOT TESTED" "binary missing"; HUMAN_UNTESTED=1
fi
if [ -x out/maclite-performance ]; then
    auto "maclite-performance (5 s idle CPU)" out/maclite-performance 5000
fi
log ""
log "  Budget check (read the two rows above):"
log "    - desktop RSS        must be within 150-300 MB at idle, and under 500 MB always"
log "    - CPU busy at idle   0-2 % with no animation running"
log "  A dev container never satisfies the second one (it is one busy core); the"
log "  iMac result is what goes in docs/testing.md."

section "3. checks only a human can make (unanswered = NOT TESTED)"
human "panel-picture"       "Is the picture on the panel correct, sharp, full-screen and free of tearing?"
human "brightness-keys"     "Do the brightness keys actually change the panel brightness (not just the number)?"
human "audio-speakers"      "Did you hear the test tone from the internal speakers during maclite-audio list? (run 'maclite-audio test' first)"
human "wifi-association"    "Did b43 associate with your access point and pass traffic (ping/curl)?"
human "ethernet-link"       "Did tg3 come up with a lease and pass traffic?"
human "usb-hotplug"         "Did a plugged USB stick appear (maclite-usb --watch 10) and mount read-only-safe?"
human "keyboard-mouse"      "Do the internal keyboard, trackpad and mouse all work, including volume keys?"
if [ "$ALLOW_SUSPEND" = 1 ]; then
    human "suspend-resume"  "After suspend/resume did the panel come back? (iMac11,x has a documented failure here)"
else
    log ""
    log "  skip: suspend/resume — not attempted (pass --allow-suspend to test it)."
    log "        On iMac Mid-2010 the panel is documented NOT to re-light: see docs/hardware.md."
    tsv "suspend-resume" "$TIER" "NOT TESTED" "not attempted (no --allow-suspend)"
    HUMAN_UNTESTED=1
fi
human "video-playback"      "Play a 1080p H.264 file fullscreen in mica-player: is it smooth, and do audio and video stay in sync?"
human "seek-behaviour"      "Seeking around a 1080p file: does the picture come back quickly without a long freeze?"

if [ "$SET_MODE" = 1 ]; then
    section "4. mode set (asked for explicitly with --set-mode)"
    auto "maclite-display --set 1280x720" out/maclite-display --set 1280x720
    auto "maclite-display --set 1920x1080" out/maclite-display --set 1920x1080
fi

section "4. summary"
log ""
log "  report      $REPORT"
log "  machine     $TSV"
if [ "$FAILED" = 0 ] && [ "$HUMAN_UNTESTED" = 0 ]; then
    log "  result      PASS (automated and human checks all passed)"
    rc=0
elif [ "$FAILED" = 0 ]; then
    log "  result      PARTIAL (automated checks passed; some human checks were not confirmed -> NOT TESTED)"
    rc=0
else
    log "  result      FAIL (see the FAIL rows above)"
    rc=1
fi
log ""
log "  Paste $TSV into docs/testing.md verbatim. Do not copy a NOT TESTED row into"
log "  a PASS column — the point of this file is that the two stay apart."

exit $rc
