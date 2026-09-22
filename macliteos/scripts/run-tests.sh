#!/bin/sh
# Runs the whole suite; any failure exits non-zero.
cd "$(dirname "$0")/.."
fail=0
for t in test_units test_idle test_leak; do
    printf '%-12s ' "$t"
    if out/$t > "out/$t.log" 2>&1; then echo PASS; else echo FAIL; fail=1; fi
done
printf '%-12s ' headless-session
if (cd out && timeout 60 ./mica-comp --headless -W 1280 -H 800 --session --script demo.script > session.log 2>&1); then
    echo PASS
else
    echo FAIL; fail=1
fi
printf '%-12s ' ui-benchmark
out/maclite-ui-benchmark > out/ui-benchmark.log 2>&1 && echo PASS || { echo FAIL; fail=1; }
exit $fail
