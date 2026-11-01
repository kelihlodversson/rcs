#!/bin/sh
# load each golden input with the host driver and compare with what the Atari program wrote
set -e
bin=build/host/roundtrip
status=0
for in in tests/golden/*.rsc; do
    case $in in *.out.rsc) continue;; esac
    out=build/host/$(basename "$in" .rsc).out.rsc
    "$bin" "$in" "$out"
    if cmp "$out" "${in%.rsc}.out.rsc"; then echo "ok   $in"; else echo "FAIL $in"; status=1; fi
done
exit $status
