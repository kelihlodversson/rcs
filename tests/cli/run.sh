#!/bin/sh
# The tests of rcs-cli (docs/rcs-cli.md, "Tests").  Run from the root of the
# repository, after `make host build/host/mkfix` (make cli-test does both):
#
#   sh tests/cli/run.sh              run the cases and the sweeps
#   UPDATE=1 sh tests/cli/run.sh     rewrite the expected files of the cases
#
# RCS_CLI names the program (default build/host/rcs-cli), MKFIX the fixture
# writer (default build/host/mkfix).
#
# A case is tests/cli/cases/NAME.cmd: one line, the arguments of one rcs-cli
# invocation, split by the shell (quotes work) after $ROOT, $FIX and $TMP
# are replaced by the repository root, the fixture directory and a scratch
# directory (none of them may contain spaces or quotes).  Expected: stdout
# in NAME.out (or its cksum in NAME.cksum), the exit status in NAME.rc (0
# when absent), stderr in NAME.err (empty when absent).  The output is
# compared after the three directories are replaced back by $ROOT, $FIX and
# $TMP, so the expected files do not depend on where the tree is.

if [ ! -f tests/cli/run.sh ] || [ ! -d tests/cli/cases ]; then
    echo "run.sh: run this from the root of the repository" >&2
    exit 2
fi
ROOT=$(pwd)
CLI=${RCS_CLI:-build/host/rcs-cli}
MKFIX=${MKFIX:-build/host/mkfix}
case $CLI in /*) ;; *) CLI=$ROOT/$CLI ;; esac
case $MKFIX in /*) ;; *) MKFIX=$ROOT/$MKFIX ;; esac
for p in "$CLI" "$MKFIX"; do
    if [ ! -x "$p" ]; then
        echo "run.sh: $p is missing: make host build/host/mkfix (or make cli-test)" >&2
        exit 2
    fi
done
FIX=$ROOT/build/host/cli-fix
TMP=$ROOT/build/host/cli-tmp
WORK=$ROOT/build/host/cli-work
rm -rf "$FIX" "$TMP" "$WORK"
mkdir -p "$FIX" "$TMP" "$WORK" || exit 2
"$MKFIX" all "$FIX" || { echo "run.sh: mkfix failed" >&2; exit 2; }

# a sanitizer report must not look like the exit status 1 of an error
ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}exitcode=86
UBSAN_OPTIONS=${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS
LC_ALL=C
export LC_ALL

pass=0
fail=0

ok() { pass=$((pass + 1)); echo "ok $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1"; }

# the directories in a file back to their names
unexpand() {
    sed -e "s|$TMP|\$TMP|g" -e "s|$FIX|\$FIX|g" -e "s|$ROOT|\$ROOT|g" "$1"
}

for cmd in tests/cli/cases/*.cmd; do
    name=${cmd%.cmd}
    base=$(basename "$name")
    args=$(sed -e "s|\$ROOT|$ROOT|g" -e "s|\$FIX|$FIX|g" -e "s|\$TMP|$TMP|g" "$cmd")
    eval "set -- $args"
    "$CLI" "$@" > "$WORK/out.raw" 2> "$WORK/err.raw"
    rc=$?
    unexpand "$WORK/out.raw" > "$WORK/out"
    unexpand "$WORK/err.raw" > "$WORK/err"
    if [ -n "$UPDATE" ]; then
        if [ -f "$name.cksum" ]; then
            cksum < "$WORK/out" > "$name.cksum"
        else
            cp "$WORK/out" "$name.out"
        fi
        if [ "$rc" -ne 0 ]; then echo "$rc" > "$name.rc"; else rm -f "$name.rc"; fi
        if [ -s "$WORK/err" ]; then cp "$WORK/err" "$name.err"; else rm -f "$name.err"; fi
    fi
    want_rc=0
    [ -f "$name.rc" ] && want_rc=$(cat "$name.rc")
    good=1
    if [ -f "$name.cksum" ]; then
        cksum < "$WORK/out" > "$WORK/out.sum"
        cmp -s "$WORK/out.sum" "$name.cksum" || { good=0; echo "  stdout cksum $(cat "$WORK/out.sum"), want $(cat "$name.cksum")"; }
    elif ! cmp -s "$WORK/out" "$name.out"; then
        good=0
        diff "$name.out" "$WORK/out" | head -40
    fi
    if [ "$rc" != "$want_rc" ]; then
        good=0
        echo "  exit status $rc, want $want_rc"
    fi
    if [ -f "$name.err" ]; then
        cmp -s "$WORK/err" "$name.err" || { good=0; diff "$name.err" "$WORK/err" | head -20; }
    elif [ -s "$WORK/err" ]; then
        good=0
        echo "  unexpected stderr:"
        head -20 "$WORK/err"
    fi
    if [ $good = 1 ]; then ok "$base"; else bad "$base"; fi
done

# ---- sweeps: every outcome is exit status 0 with nothing but warnings
# ("rcs-cli: warning: ") on stderr, or 1 with exactly one "rcs-cli: " line;
# anything else (a crash, a sanitizer report) fails

# outcome NAME ARGS...: runs rcs-cli and checks the rule; quiet when fine
outcome() {
    what=$1
    shift
    "$CLI" "$@" > /dev/null 2> "$WORK/serr"
    src=$?
    lines=$(wc -l < "$WORK/serr" | tr -d ' ')
    if [ "$src" = 0 ] && ! grep -v -q '^rcs-cli: warning: ' "$WORK/serr"; then
        sweep_ok=$((sweep_ok + 1))
        return 0
    fi
    if [ "$src" = 1 ] && [ "$lines" = 1 ] && grep -q '^rcs-cli: ' "$WORK/serr"; then
        sweep_err=$((sweep_err + 1))
        return 0
    fi
    bad "$what (exit status $src)"
    head -20 "$WORK/serr"
    return 1
}

# sweep LABEL FILE STEP KIND [ARGS...]: KIND trunc (every STEP-th length
# below the size) or flip (every STEP-th byte xor 0xff), the copy read
# with ARGS
sweep() {
    label=$1 file=$2 step=$3 kind=$4
    shift 4
    size=$(wc -c < "$file" | tr -d ' ')
    sweep_ok=0
    sweep_err=0
    sfail=$fail
    i=0
    ext=${file##*.}
    copy=$WORK/sweep.$ext
    while [ "$i" -lt "$size" ]; do
        if [ "$kind" = trunc ]; then
            if [ "$i" = 0 ]; then : > "$copy"; else head -c "$i" "$file" > "$copy"; fi
        else
            "$MKFIX" flip "$file" "$copy" "$i" 0xff
        fi
        outcome "$label $kind $i" "$@" || break
        i=$((i + step))
    done
    if [ "$fail" = "$sfail" ]; then
        ok "$label: $kind sweep ($sweep_ok read, $sweep_err refused)"
    fi
}

S=$WORK/sweep
sweep cicon-trunc tests/golden/cicon.rsc 1 trunc --no-names $S.rsc -c 'ls -lR' -c tree
sweep cicon-flip tests/golden/cicon.rsc 1 flip --no-names $S.rsc -c 'ls -lR' -c tree -c "convert -o $TMP/m.rsc"
sweep kinds-trunc "$FIX/kinds.rsc" 17 trunc --no-names $S.rsc -c 'ls -lR'
sweep kinds-flip "$FIX/kinds.rsc" 7 flip --no-names $S.rsc -c 'ls -lR' -c 'tree /DIALOG' -c "convert -o $TMP/m.rsc"
sweep rcs-trunc tests/golden/rcs.rsc 101 trunc --no-names $S.rsc -c 'ls -lR'
sweep rcs-flip tests/golden/rcs.rsc 23 flip --no-names $S.rsc -c 'ls -lR' -c "convert -o $TMP/m.rsc"
sweep dfn-trunc "$FIX/kinds.dfn" 1 trunc --names $S.dfn "$FIX/kinds.rsc" ls -lR
sweep dfn-flip "$FIX/kinds.dfn" 1 flip --names $S.dfn "$FIX/kinds.rsc" ls -lR
sweep def-trunc "$FIX/kinds.def" 1 trunc --names $S.def "$FIX/kinds.rsc" ls -lR
sweep def-flip "$FIX/kinds.def" 1 flip --names $S.def "$FIX/kinds.rsc" ls -lR
sweep hrd-trunc "$FIX/kinds.hrd" 1 trunc --names $S.hrd "$FIX/kinds.rsc" ls -lR
sweep hrd-flip "$FIX/kinds.hrd" 1 flip --names $S.hrd "$FIX/kinds.rsc" ls -lR
sweep rcsdfn-flip src/atari/rcs.dfn 13 flip --names $S.dfn src/atari/rcs.rsc -c 'ls -lR' -c tree

# ---- convert: each golden input written again equals its .out.rsc, in file
# form and with --pixels (8x16)
for g in rcs rcslow cicon; do
    for mode in file pixels; do
        rm -f "$TMP/conv.rsc"
        if [ $mode = pixels ]; then
            "$CLI" --pixels --no-names "tests/golden/$g.rsc" convert -o "$TMP/conv.rsc"
        else
            "$CLI" --no-names "tests/golden/$g.rsc" convert -o "$TMP/conv.rsc"
        fi
        if [ $? = 0 ] && cmp -s "$TMP/conv.rsc" "tests/golden/$g.out.rsc"; then
            ok "convert $g ($mode) equals $g.out.rsc"
        else
            bad "convert $g ($mode) equals $g.out.rsc"
        fi
    done
done
# the output of convert reads back and converts to the same bytes
"$CLI" "$FIX/kinds.rsc" convert -o "$TMP/k1.rsc" && "$CLI" "$TMP/k1.rsc" convert -o "$TMP/k2.rsc" \
    && cmp -s "$FIX/kinds.rsc" "$TMP/k1.rsc" && cmp -s "$TMP/k1.rsc" "$TMP/k2.rsc" \
    && ok "convert kinds.rsc is a fixed point" || bad "convert kinds.rsc is a fixed point"
# a failed convert leaves no file
rm -f "$TMP/none.rsc"
"$CLI" "$FIX/deep.rsc" convert -o "$TMP/none.rsc" 2> /dev/null
if [ $? = 1 ] && [ ! -e "$TMP/none.rsc" ]; then ok "convert too big: no file"; else bad "convert too big: no file"; fi
"$CLI" "$FIX/kinds.rsc" convert -o "$TMP/nodir/x.rsc" 2> /dev/null
if [ $? = 1 ] && [ ! -e "$TMP/nodir" ]; then ok "convert into no directory: no file"; else bad "convert into no directory: no file"; fi
"$CLI" "$FIX/kinds.rsc" convert -o "$TMP/x.rsh" 2> /dev/null
if [ $? = 1 ] && [ ! -e "$TMP/x.rsh" ]; then ok "convert .rsh: no file"; else bad "convert .rsh: no file"; fi

# ---- the largest name list: 65535 entries, a tree of 20000 objects
t0=$(date +%s)
"$CLI" --names "$FIX/huge.hrd" "$FIX/huge.rsc" ls -lR > "$WORK/huge.out" 2> "$WORK/huge.err"
hrc=$?
t1=$(date +%s)
hl=$(wc -l < "$WORK/huge.out" | tr -d ' ')
if [ $hrc = 0 ] && [ "$hl" = 20009 ] && [ ! -s "$WORK/huge.err" ] && [ $((t1 - t0)) -lt 600 ]; then
    ok "65535 names, 20000 objects: ls -lR in $((t1 - t0)) s"
else
    bad "65535 names, 20000 objects: ls -lR (exit status $hrc, $hl lines, $((t1 - t0)) s)"
fi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
