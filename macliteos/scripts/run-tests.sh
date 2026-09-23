#!/bin/sh
# MacLiteOS v0.2 test suite (spec §21).
#
# Runs every check that can actually run on this host and prints an honest
# result per row. Nothing here reports a tier it did not run in: QEMU and
# real-iMac rows are simply absent until someone runs scripts/hardware-check.sh
# on the machine (see docs/testing.md).
#
#   out/logs/            raw output of every row
#   out/test-summary.tsv machine-readable summary (name <TAB> tier <TAB> result)
#
# Usage: sh scripts/run-tests.sh [--quick] [--no-fixtures]
#   --quick         skip the scripted UI sessions (they are the slow rows)
#   --no-fixtures   skip the hardware fixture matrix
# exit: 0 every row passed, 1 at least one row failed
#
# Legend for the status column used below:
#   PASS  exercised here and verified
#   FAIL  exercised here and did not work        (keeps the suite red)
#   SKIP  could not be exercised on this host   (never counted as PASS)
cd "$(dirname "$0")/.." || exit 1

QUICK=0
FIXTURES=1
for a in "$@"; do
    case "$a" in
    --quick) QUICK=1 ;;
    --no-fixtures) FIXTURES=0 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 1 ;;
    esac
done

LOG=out/logs
SUMMARY=out/test-summary.tsv
mkdir -p "$LOG"
: > "$SUMMARY"
FAILED=0
ROWS=0

if [ ! -x out/mica-comp ]; then
    echo "out/mica-comp not built — run: sh scripts/build.sh" >&2
    exit 1
fi

# record NAME TIER RESULT [DETAIL]
record() {
    ROWS=$((ROWS + 1))
    printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "${4:-}" >> "$SUMMARY"
}

# row NAME TIER COMMAND...   (runs in the repo root, log in $LOG/NAME.log)
row() {
    name=$1; tier=$2; shift 2
    printf '%-26s %-8s ' "$name" "$tier"
    if "$@" > "$LOG/$name.log" 2>&1; then
        echo "PASS"
        record "$name" "$tier" PASS
        return 0
    fi
    echo "FAIL (see $LOG/$name.log)"
    record "$name" "$tier" FAIL "$(tail -1 "$LOG/$name.log" 2>/dev/null)"
    FAILED=1
    return 1
}

# skip NAME TIER REASON
skip() {
    printf '%-26s %-8s SKIP     %s\n' "$1" "$2" "$3"
    record "$1" "$2" SKIP "$3"
}

echo "MacLiteOS test suite — $(uname -s) $(uname -m), $(date -u '+%Y-%m-%dT%H:%MZ')"
echo

# ---------------------------------------------------------------- unit tests --
for t in test_render test_units test_idle test_leak; do
    [ -x "out/$t" ] || { skip "$t" SANDBOX "not built"; continue; }
    row "$t" SANDBOX "out/$t"
done

# ------------------------------------------------------- hardware fixture run --
# The fixture trees are the regression harness for the detection code: they
# reproduce a known machine (iMac11,2 / iMac11,3), a VM, and a bare host, and
# tests/test_hardware.c pins what must be detected, what must fall back, and
# which exit code each outcome gets.
FIXDIR=out/fixtures
if [ "$FIXTURES" = 1 ]; then
    if command -v python3 > /dev/null 2>&1; then
        for v in imac11_2 imac11_3 virtual bare; do
            if python3 tests/fixtures/make_sysfs.py "$FIXDIR/$v" --variant "$v" > "$LOG/fixture-$v.log" 2>&1; then
                printf '%-26s %-8s ' "fixture:$v" SANDBOX
                if ML_HW_FIXTURE="$FIXDIR/$v" ML_HW_VARIANT="$v" out/test_hardware > "$LOG/hw-$v.log" 2>&1; then
                    echo "PASS"
                    record "test_hardware:$v" SANDBOX PASS
                else
                    echo "FAIL (see $LOG/hw-$v.log)"
                    cat "$LOG/hw-$v.log"
                    record "test_hardware:$v" SANDBOX FAIL "$(tail -1 "$LOG/hw-$v.log")"
                    FAILED=1
                fi
            else
                skip "test_hardware:$v" SANDBOX "fixture generation failed"
            fi
        done
    else
        skip "test_hardware:*" SANDBOX "python3 absent: fixtures cannot be generated"
    fi
else
    skip "test_hardware:*" SANDBOX "--no-fixtures"
fi

# ---------------------------------------------------------- honesty gates -----
# These are the rules the v0.2 work exists to enforce (spec §2/§5/§21). Each one
# must hold on every host, fixture or not:
#   1. maclite-gpu may never exit 0 without a verified hardware renderer.
#   2. a brightness write against fixture roots may never be reported verified.
#   3. the fixture runs of the diagnostics may never exit 4 (usage = harness bug)
#      and never report FAIL for checks that only a live kernel can answer.
imac_env() {
    ML_SYSFS_ROOT="$FIXDIR/imac11_2/sys" ML_PROC_ROOT="$FIXDIR/imac11_2/proc" \
    ML_DEV_ROOT="$FIXDIR/imac11_2/dev" ML_ETC_ROOT="$FIXDIR/imac11_2/etc" \
    ML_FW_ROOT="$FIXDIR/imac11_2/fw" ML_LIB_ROOT="$FIXDIR/imac11_2/lib" "$@"
}
if [ "$FIXTURES" = 1 ] && [ -d "$FIXDIR/imac11_2/sys" ]; then
    printf '%-26s %-8s ' "honesty:gpu" SANDBOX
    imac_env out/maclite-gpu > "$LOG/honesty-gpu.log" 2>&1
    rc=$?
    if [ "$rc" = 0 ]; then
        echo "FAIL (maclite-gpu reported acceleration without a GL query)"
        record "honesty:gpu" SANDBOX FAIL "exit 0 without verified renderer"
        FAILED=1
    else
        echo "PASS (exit $rc, acceleration never claimed)"
        record "honesty:gpu" SANDBOX PASS "exit $rc"
    fi

    printf '%-26s %-8s ' "honesty:brightness" SANDBOX
    imac_env out/maclite-brightness set 42 > "$LOG/honesty-brightness.log" 2>&1
    rc=$?
    if grep -qi "verified" "$LOG/honesty-brightness.log"; then
        echo "FAIL (claimed a verified brightness change on fixture files)"
        record "honesty:brightness" SANDBOX FAIL "verified claim under fixture roots"
        FAILED=1
    elif [ "$rc" != 2 ]; then
        echo "FAIL (refusal exited $rc; NOT TESTED must be 2, not FAIL=1)"
        record "honesty:brightness" SANDBOX FAIL "refusal exited $rc"
        FAILED=1
    else
        echo "PASS (exit 2: no write attempted, no change claimed)"
        record "honesty:brightness" SANDBOX PASS "exit 2, refused"
    fi

    printf '%-26s %-8s ' "honesty:audio-fixture" SANDBOX
    imac_env out/maclite-audio list > "$LOG/honesty-audio.log" 2>&1
    if grep -q "FAIL" "$LOG/honesty-audio.log"; then
        echo "FAIL (mixer reported FAIL where no ioctl could run)"
        record "honesty:audio-fixture" SANDBOX FAIL
        FAILED=1
    else
        echo "PASS (mixer row is NOT TESTED, not FAIL)"
        record "honesty:audio-fixture" SANDBOX PASS
    fi

    printf '%-26s %-8s ' "honesty:net-fixture" SANDBOX
    imac_env out/maclite-network > "$LOG/honesty-net.log" 2>&1
    if grep -qE "0\.21\.0\.0|borrow" "$LOG/honesty-net.log"; then
        echo "FAIL (fixture report contains this host's address)"
        record "honesty:net-fixture" SANDBOX FAIL
        FAILED=1
    else
        echo "PASS (no host address leaked into the fixture report)"
        record "honesty:net-fixture" SANDBOX PASS
    fi

    # An explicit --backend is a requirement, not a hint. With fixture roots that
    # contain no card and no /dev/fb0, --backend kms must fail loudly instead of
    # presenting through headless while the log says "ready".
    printf '%-26s %-8s ' "honesty:backend" SANDBOX
    ML_SYSFS_ROOT="$FIXDIR/bare/sys" ML_PROC_ROOT="$FIXDIR/bare/proc" \
    ML_DEV_ROOT="$FIXDIR/bare/dev" \
        timeout 20 out/mica-comp --backend kms -W 320 -H 200 \
        > "$LOG/honesty-backend.log" 2>&1
    rc=$?
    if [ "$rc" = 0 ] || grep -q "mica-comp ready" "$LOG/honesty-backend.log"; then
        echo "FAIL (explicit --backend kms degraded to headless)"
        record "honesty:backend" SANDBOX FAIL "kms request silently served by headless"
        FAILED=1
    else
        echo "PASS (exit $rc, explicit backend refused instead of degrading)"
        record "honesty:backend" SANDBOX PASS "exit $rc"
    fi

    # The performance mode must come from what was detected (§23/§24), and an
    # explicit MICA_MODE must beat the picker — a pin that silently loses to
    # auto-selection is exactly the sort of lie these gates exist for.
    printf '%-26s %-8s ' "honesty:mode" SANDBOX
    ML_SYSFS_ROOT="$FIXDIR/bare/sys" ML_PROC_ROOT="$FIXDIR/bare/proc" \
    ML_DEV_ROOT="$FIXDIR/bare/dev" ML_ETC_ROOT="$FIXDIR/bare/etc" \
        timeout 5 out/mica-comp --headless -W 320 -H 200 > "$LOG/honesty-mode-auto.log" 2>&1
    ML_SYSFS_ROOT="$FIXDIR/bare/sys" ML_PROC_ROOT="$FIXDIR/bare/proc" \
    ML_DEV_ROOT="$FIXDIR/bare/dev" ML_ETC_ROOT="$FIXDIR/bare/etc" MICA_MODE=beautiful \
        timeout 5 out/mica-comp --headless -W 320 -H 200 > "$LOG/honesty-mode-pinned.log" 2>&1
    auto_mode=$(sed -n 's/.*mode=\([a-z]*\).*/\1/p' "$LOG/honesty-mode-auto.log" | head -1)
    pin_mode=$(sed -n 's/.*mode=\([a-z]*\).*/\1/p' "$LOG/honesty-mode-pinned.log" | head -1)
    if [ "$auto_mode" != "performance" ]; then
        echo "FAIL (no KMS and no GL renderer must pick performance, got '${auto_mode:-nothing}')"
        record "honesty:mode" SANDBOX FAIL "auto-picked '$auto_mode'"
        FAILED=1
    elif [ "$pin_mode" != "beautiful" ]; then
        echo "FAIL (MICA_MODE=beautiful must beat the picker, got '${pin_mode:-nothing}')"
        record "honesty:mode" SANDBOX FAIL "pin ignored ('$pin_mode')"
        FAILED=1
    else
        echo "PASS (picker chose $auto_mode from detection; MICA_MODE pinned $pin_mode)"
        record "honesty:mode" SANDBOX PASS "auto=$auto_mode pinned=$pin_mode"
    fi

    # 4. the driver resolver must resolve (and must say which newer release it
    #    refused), and on a fixture it must never claim the running
    #    configuration was validated — validation needs a real reboot.
    printf '%-26s %-8s ' "honesty:drivers-check" SANDBOX
    imac_env out/maclite-drivers check --catalog drivers/catalog/maclite-offline.cat \
        > "$LOG/honesty-drivers-check.log" 2>&1
    rc=$?
    if [ "$rc" != 1 ]; then
        echo "FAIL (a machine missing its driver files must resolve to FAIL, got exit $rc)"
        record "honesty:drivers-check" SANDBOX FAIL "exit $rc"
        FAILED=1
    elif ! grep -q "25.0.0" "$LOG/honesty-drivers-check.log"; then
        echo "FAIL (the newer release that refuses this GPU was not reported)"
        record "honesty:drivers-check" SANDBOX FAIL "skipped-release reason missing"
        FAILED=1
    elif grep -qE "mesa-r600 +25\.0\.0" "$LOG/honesty-drivers-check.log"; then
        echo "FAIL (the newer, incompatible release was selected anyway)"
        record "honesty:drivers-check" SANDBOX FAIL "25.0.0 chosen"
        FAILED=1
    else
        echo "PASS (exit 1: files missing, 25.0.0 declined with a reason)"
        record "honesty:drivers-check" SANDBOX PASS "exit 1"
    fi

    printf '%-26s %-8s ' "honesty:drivers-verify" SANDBOX
    imac_env out/maclite-drivers verify --catalog drivers/catalog/maclite-offline.cat \
        > "$LOG/honesty-drivers-verify.log" 2>&1
    rc=$?
    if [ "$rc" != 2 ]; then
        echo "FAIL (validation on a fixture must be NOT TESTED, got exit $rc)"
        record "honesty:drivers-verify" SANDBOX FAIL "exit $rc"
        FAILED=1
    elif grep -qE "Running configuration +(PASS|FAIL)" "$LOG/honesty-drivers-verify.log"; then
        echo "FAIL (validated the running configuration from fixture roots)"
        record "honesty:drivers-verify" SANDBOX FAIL "claimed validation"
        FAILED=1
    else
        echo "PASS (exit 2, NOT TESTED)"
        record "honesty:drivers-verify" SANDBOX PASS "exit 2"
    fi

    printf '%-26s %-8s ' "honesty:drivers-write" SANDBOX
    imac_env out/maclite-drivers update --catalog drivers/catalog/maclite-offline.cat \
        > "$LOG/honesty-drivers-update.log" 2>&1
    rc=$?
    if [ -e /var/lib/maca-lite/driver-state ]; then
        echo "FAIL (an update run with fixture roots wrote the system state file)"
        record "honesty:drivers-write" SANDBOX FAIL "wrote to /var/lib/maca-lite"
        FAILED=1
    elif ! grep -q "refusing to install" "$LOG/honesty-drivers-update.log"; then
        echo "FAIL (fixture install was not refused: exit $rc)"
        record "honesty:drivers-write" SANDBOX FAIL "no refusal"
        FAILED=1
    else
        echo "PASS (refused: a fixture is never written to)"
        record "honesty:drivers-write" SANDBOX PASS "refused"
    fi

    printf '%-26s %-8s ' "honesty:decode" SANDBOX
    imac_env out/maclite-video-test h264 > "$LOG/honesty-decode.log" 2>&1
    rc=$?
    if [ "$rc" = 0 ]; then
        echo "FAIL (reported a verified decode without a decoder on this host)"
        record "honesty:decode" SANDBOX FAIL "exit 0 with no decoder available"
        FAILED=1
    else
        echo "PASS (exit $rc, nothing decoded is never PASS)"
        record "honesty:decode" SANDBOX PASS "exit $rc"
    fi
else
    skip "honesty:*" SANDBOX "fixtures unavailable"
fi

# ------------------------------------------------------- scripted UI sessions --
# Each scripts/*.script drives a real headless session (render loop, input,
# window management, screenshots). A row passes when the session exits 0, the
# log has no FATAL, and every screenshot the script asked for exists.
session() {
    s=$1
    printf '%-26s %-8s ' "session:$s" SANDBOX
    log="$LOG/session-$s.log"
    if ! ( cd out && timeout 60 ./mica-comp --headless -W 1280 -H 800 --session \
             --script "../tests/scripts/$s.script" ) > "$log" 2>&1; then
        echo "FAIL (session exited non-zero: see $log)"
        record "session:$s" SANDBOX FAIL "$(tail -1 "$log")"
        FAILED=1
        return
    fi
    if grep -q "FATAL" "$log"; then
        echo "FAIL (FATAL in session log)"
        record "session:$s" SANDBOX FAIL "FATAL in log"
        FAILED=1
        return
    fi
    missing=""
    while IFS= read -r shot; do
        [ -n "$shot" ] || continue
        [ -f "out/$shot" ] || missing="$missing $shot"
    done <<EOF
$(grep -E '^[[:space:]]*shot ' "tests/scripts/$s.script" | awk '{print $2}')
EOF
    if [ -n "$missing" ]; then
        echo "FAIL (missing screenshots:$missing)"
        record "session:$s" SANDBOX FAIL "missing screenshots:$missing"
        FAILED=1
        return
    fi
    echo "PASS"
    record "session:$s" SANDBOX PASS "screenshots ok"
}

if [ "$QUICK" = 1 ]; then
    skip "session:*" SANDBOX "--quick"
else
    for s in demo interact dockhide menuclick cc hardware settings; do
        if [ -f "tests/scripts/$s.script" ]; then
            session "$s"
        else
            skip "session:$s" SANDBOX "tests/scripts/$s.script missing"
        fi
    done
fi

# ------------------------------------------------------------- benchmarks ----
[ -x out/maclite-ui-benchmark ] && row ui-benchmark SANDBOX out/maclite-ui-benchmark

# --------------------------------------------------- live host diagnostics ---
# Reported, not gated: on a dev container these reflect the container, and the
# only place they mean anything is the machine itself (docs/testing.md).
for b in maclite-memory maclite-performance; do
    [ -x "out/$b" ] || continue
    printf '%-26s %-8s ' "$b" SANDBOX
    if "out/$b" > "$LOG/$b.log" 2>&1; then
        echo "PASS (host numbers: out/logs/$b.log)"
        record "$b" SANDBOX PASS
    else
        echo "FAIL"
        record "$b" SANDBOX FAIL "$(tail -1 "$LOG/$b.log")"
        FAILED=1
    fi
done

echo
echo "$ROWS row(s); summary in $SUMMARY"
if [ "$FAILED" = 0 ]; then
    echo "Result: PASS — every row that ran on this host passed."
    echo "        Rows marked SKIP (and the QEMU/iMac tiers in docs/testing.md) are NOT TESTED."
else
    echo "Result: FAIL — see the rows above; logs in $LOG/"
    echo "--- Summary of $SUMMARY ---"
    cat "$SUMMARY"
fi
exit $FAILED
