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
    if [ "$src" = 0 ] && ! grep -v -q -e '^rcs-cli: warning: ' -e '^rcs-cli: note: ' "$WORK/serr"; then
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
# the edit commands on damaged files: read, edited (dry run), refused or failed cleanly
sweep kinds-edit-flip "$FIX/kinds.rsc" 29 flip --selfcheck -n --no-names $S.rsc -c 'cp -r /#0 /X' -c 'rm /#0/#3' -c 'mkdir Y' -c 'touch /Y/Z' -c 'mv /#1 /W' -c 'set /#0/#1 x=1 text=a'
sweep rcs-edit-flip tests/golden/rcs.rsc 97 flip --selfcheck -n --no-names $S.rsc -c 'cp -r /#0 /X' -c 'rm -r /#0/#2' -c 'chmod +EXIT /#0/#1'
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
# a failed convert never destroys a good OUT, and leaves no temporary file:
# OUT is a directory (the rename fails), or the resource cannot be encoded
rm -rf "$TMP/cv"
mkdir -p "$TMP/cv/dirout.rsc"
"$CLI" "$FIX/kinds.rsc" convert -o "$TMP/cv/dirout.rsc" 2> /dev/null
if [ $? = 1 ] && [ -d "$TMP/cv/dirout.rsc" ] && [ "$(ls -A "$TMP/cv" | wc -l | tr -d ' ')" = 1 ]; then
    ok "convert onto a directory: no temporary file"
else
    bad "convert onto a directory: no temporary file"
fi
cp "$FIX/kinds.rsc" "$TMP/cv/good.rsc"
"$CLI" "$FIX/deep.rsc" convert -o "$TMP/cv/good.rsc" 2> /dev/null
if [ $? = 1 ] && cmp -s "$FIX/kinds.rsc" "$TMP/cv/good.rsc" && [ "$(ls -A "$TMP/cv" | wc -l | tr -d ' ')" = 2 ]; then
    ok "convert too big: an existing OUT is kept"
else
    bad "convert too big: an existing OUT is kept"
fi
# the converted file lists as the original does, without name files (also the inputs that are
# not goldens), with and without guessing, in file form and in pixels
for f in "$FIX/kinds.rsc" tests/golden/rcs.rsc tests/golden/cicon.rsc; do
    b=$(basename "$f" .rsc)
    for mode in "" "--no-guess"; do
        rm -f "$TMP/cv/$b.rsc"
        "$CLI" --no-names $mode "$f" -c 'ls -lR' > "$WORK/orig.ls" 2>&1
        "$CLI" --no-names $mode "$f" convert -o "$TMP/cv/$b.rsc" 2> /dev/null && \
            "$CLI" --no-names $mode "$TMP/cv/$b.rsc" -c 'ls -lR' > "$WORK/conv.ls" 2>&1
        if [ -s "$WORK/orig.ls" ] && cmp -s "$WORK/orig.ls" "$WORK/conv.ls"; then
            ok "convert $b $mode: the listing is the same"
        else
            bad "convert $b $mode: the listing is the same"
        fi
    done
done
# an unreachable object is not written: kinds-lost.rsc has one in its last
# tree (4 objects), the converted file 3, and converting that again changes
# nothing
"$CLI" --no-names "$FIX/kinds-lost.rsc" convert -o "$TMP/cv/lost1.rsc" 2> /dev/null && \
    "$CLI" --no-names "$TMP/cv/lost1.rsc" convert -o "$TMP/cv/lost2.rsc" 2> /dev/null && \
    "$CLI" --no-names "$FIX/kinds-lost.rsc" -c 'ls -lR' > "$WORK/orig.ls" 2>&1 && \
    "$CLI" --no-names "$TMP/cv/lost1.rsc" -c 'ls -lR' > "$WORK/conv.ls" 2>&1
if [ $? = 0 ] && grep -q '^#6 .* PANL~ 4$' "$WORK/orig.ls" && grep -q '^#6 .* PANL~ 3$' "$WORK/conv.ls" \
    && cmp -s "$TMP/cv/lost1.rsc" "$TMP/cv/lost2.rsc"; then
    ok "convert kinds-lost: the unreachable object goes, then a fixed point"
else
    bad "convert kinds-lost: the unreachable object goes, then a fixed point"
fi
# OUT may be the input file
cp "$FIX/kinds.rsc" "$TMP/cv/self.rsc"
"$CLI" --no-names "$TMP/cv/self.rsc" -c 'ls -lR' > "$WORK/orig.ls" 2>&1
"$CLI" --no-names "$TMP/cv/self.rsc" convert -o "$TMP/cv/self.rsc" 2> /dev/null && \
    "$CLI" --no-names "$TMP/cv/self.rsc" -c 'ls -lR' > "$WORK/conv.ls" 2>&1
if [ -s "$WORK/orig.ls" ] && cmp -s "$WORK/orig.ls" "$WORK/conv.ls" && cmp -s "$FIX/kinds.rsc" "$TMP/cv/self.rsc"; then
    ok "convert onto the input file"
else
    bad "convert onto the input file"
fi

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

# ---- the edit commands that write files -----------------------------------------
ED=$TMP/ed
rm -rf "$ED"
mkdir -p "$ED"

# bytes_same A B: the files are equal
bytes_same() { cmp -s "$1" "$2"; }

# listing FILE: what a re-read shows (with the names next to the file)
listing() {
    "$CLI" "$1" -c 'ls -lR' -c tree -c 'stat /' 2>&1
}

# edit NAME SRC [OPTIONS] FILE -c ...: copies $FIX/SRC.rsc (and SRC.dfn, .def, .hrd when they exist) to
# $ED/NAME/in.rsc, runs rcs-cli on it with --selfcheck and compares the re-read result with
# tests/cli/edits/NAME.out (UPDATE=1 writes it).  The file argument is $ED/NAME/in.rsc; with -o
# $ED/NAME/out.rsc the re-read file is out.rsc.
edit() {
    name=$1 src=$2
    shift 2
    d=$ED/$name
    mkdir -p "$d"
    for ext in rsc dfn def hrd; do
        if [ -f "$FIX/$src.$ext" ]; then cp "$FIX/$src.$ext" "$d/in.$ext"; fi
    done
    cp "$d/in.rsc" "$d/orig.rsc"
    "$CLI" --selfcheck "$@" > "$d/run.out" 2> "$d/run.err.raw"
    erc=$?
    target=$d/in.rsc
    if [ -f "$d/out.rsc" ]; then target=$d/out.rsc; fi
    {
        echo "exit status $erc"
        cat "$d/run.err.raw"
        listing "$target"
        for f in "$d"/*.dfn; do
            if [ -f "$f" ]; then echo "file $(basename "$f") $(wc -c < "$f" | tr -d ' ') bytes"; fi
        done
    } | sed -e "s|$d|\$D|g" -e "s|$FIX|\$FIX|g" -e "s|$ROOT|\$ROOT|g" > "$d/result"
    if [ -n "$UPDATE" ]; then cp "$d/result" "tests/cli/edits/$name.out"; fi
    if cmp -s "$d/result" "tests/cli/edits/$name.out"; then
        ok "edit $name"
    else
        bad "edit $name"
        diff "tests/cli/edits/$name.out" "$d/result" | head -20
    fi
}

# write-back
edit wb-rm kinds "$ED/wb-rm/in.rsc" -c 'rm /DIALOG/#1' -c 'rm /.strings/HELLO'
edit wb-mkdir kinds "$ED/wb-mkdir/in.rsc" -c 'mkdir MENU2 --type menu' -c 'mkdir ALR --type alert' -c 'mkdir PNL --type panel'
edit wb-set kinds "$ED/wb-set/in.rsc" -c 'set /DIALOG/OK text=Done x=3+4 type=G_BUTTON name=FINE' -c 'chmod +TOUCHEXIT /DIALOG/FINE' -c 'chmod --state +DISABLED /DIALOG/FINE'
edit wb-touch kinds "$ED/wb-touch/in.rsc" -c 'touch /PANEL3/B1 --type button' -c 'touch /PANEL3/T1 --type G_FTEXT' -c 'touch /.strings/S1' -c 'touch /.images/I1'
edit wb-cp kinds "$ED/wb-cp/in.rsc" -c 'cp -r /DIALOG/BUTTONS /PANEL3/B2' -c 'cp -r /DIALOG /DLG2' -c 'cp /.strings/HELLO /.strings/HI'
edit wb-mv kinds "$ED/wb-mv/in.rsc" -c 'mv /DIALOG/BUTTONS /PANEL3' -c 'mv /MAINMENU /' -c 'mv /.images/LOGO /.images/PIC'
edit wb-cp-second kinds "$ED/wb-cp-second/in.rsc" -c "cp -r $ROOT/tests/golden/cicon.rsc:/PANEL1 /ICONS" -c "cp -r $ROOT/tests/golden/rcs.rsc:/#2 /"
edit wb-pixels kinds --pixels "$ED/wb-pixels/in.rsc" -c 'set /DIALOG/OK x=30 y=16 w=100 h=32' -c 'touch /PANEL3/B1'
# -o: the input stays
edit o-rm kinds -o "$ED/o-rm/out.rsc" "$ED/o-rm/in.rsc" -c 'rm -r /DIALOG/BUTTONS' -c 'mkdir NEWT'
if bytes_same "$ED/o-rm/in.rsc" "$ED/o-rm/orig.rsc" && bytes_same "$ED/o-rm/in.dfn" "$FIX/kinds.dfn" && [ -f "$ED/o-rm/out.dfn" ]; then
    ok "-o: the input and its name file are unchanged, OUT.dfn is written"
else
    bad "-o: the input and its name file are unchanged, OUT.dfn is written"
fi
# names are carried: the in-memory listing equals the re-read one
d=$ED/names-same
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$d/"
"$CLI" --no-guess "$d/kinds.rsc" -c 'rm /DIALOG/#1' -c 'mkdir NEWT' -c 'touch /NEWT/B1' -c 'mv /MAINMENU /MM' -c 'cp -r /DIALOG/BUTTONS /#2/B2' \
    -c 'set /DIALOG/OK name=FINE' -c 'touch /.strings/S1' -c 'mkdir ALR --type alert' -c 'ls -lR' > "$d/mem.ls" 2> "$d/mem.err"
"$CLI" --no-guess "$d/kinds.rsc" -c 'ls -lR' > "$d/disk.ls" 2>&1
if [ -s "$d/mem.ls" ] && cmp -s "$d/mem.ls" "$d/disk.ls"; then
    ok "names carried: .dfn written, re-read gives the same names"
else
    bad "names carried: .dfn written, re-read gives the same names"
    diff "$d/mem.ls" "$d/disk.ls" | head
fi
# a name file that is not a .dfn is left alone, a .dfn is created
d=$ED/names-def
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$FIX/kinds.def" "$d/"
"$CLI" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
rc1=$?
if [ $rc1 = 0 ] && bytes_same "$d/kinds.def" "$FIX/kinds.def" && [ -f "$d/kinds.dfn" ] && grep -q 'were read from .*kinds.def, which is left as it is' "$d/err" \
    && "$CLI" "$d/kinds.rsc" -c 'ls /' | grep -q '^NEWT$'; then
    ok "names from .def: the .def stays, a new .dfn is written, with a note"
else
    bad "names from .def: the .def stays, a new .dfn is written, with a note"
fi
# a .dfn that is there but not the names' source is not overwritten
"$CLI" --names "$d/kinds.def" "$d/kinds.rsc" mkdir NEW2 2> "$d/err2"
if [ $? = 1 ] && grep -q 'exists, but the names of' "$d/err2"; then ok "an unrelated .dfn is not overwritten"; else bad "an unrelated .dfn is not overwritten"; fi
# names that do not fit a .dfn (.hrd names): refused; --names-out writes the rest; --no-names writes none
d=$ED/names-hrd
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$FIX/kinds.hrd" "$d/"
"$CLI" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 1 ] && grep -q 'names do not fit a .dfn file' "$d/err" && bytes_same "$d/kinds.rsc" "$FIX/kinds.rsc" && [ ! -f "$d/kinds.dfn" ]; then
    ok "names that do not fit a .dfn: refused, nothing written"
else
    bad "names that do not fit a .dfn: refused, nothing written"
fi
"$CLI" --names-out "$d/some.dfn" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 0 ] && grep -q 'warning: 2 names do not fit' "$d/err" && [ -f "$d/some.dfn" ] && [ ! -f "$d/kinds.dfn" ] \
    && "$CLI" --names "$d/some.dfn" "$d/kinds.rsc" -c 'ls /' | grep -q '^NEWT$'; then
    ok "--names-out: the names that fit are written, the others reported"
else
    bad "--names-out: the names that fit are written, the others reported"
fi
cp "$FIX/kinds.rsc" "$d/kinds.rsc"
"$CLI" --no-names "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 0 ] && [ ! -s "$d/err" ] && [ ! -f "$d/kinds.dfn" ]; then ok "--no-names: no name file is read or written"; else bad "--no-names: no name file is read or written"; fi
# a name is written: s.dfn; no names: no .dfn
d=$ED/names-none
mkdir -p "$d"
cp "$FIX/disc/d1/x.rsc" "$d/s.rsc"
"$CLI" "$d/s.rsc" -c 'cp -r /#0 /' -c 'touch /#0/B'
if [ $? = 0 ] && [ -f "$d/s.dfn" ]; then ok "a name written: s.dfn"; else bad "a name written: s.dfn"; fi
cp "$FIX/disc/d1/x.rsc" "$d/t.rsc"
"$CLI" --no-guess "$d/t.rsc" -c 'cp -r /#0 /'
if [ $? = 0 ] && [ ! -f "$d/t.dfn" ]; then ok "no names, no .dfn"; else bad "no names, no .dfn"; fi

# -n writes nothing, all or nothing, no edit command, a failed write
d=$ED/safe
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$d/"
"$CLI" -n "$d/kinds.rsc" -c 'mkdir NEWT' -c 'rm -r /DIALOG' 2> "$ED/safe.err"
if [ $? = 0 ] && bytes_same "$d/kinds.rsc" "$FIX/kinds.rsc" && bytes_same "$d/kinds.dfn" "$FIX/kinds.dfn" && [ "$(ls "$d" | wc -l | tr -d ' ')" = 2 ] \
    && grep -q 'dry run: nothing written' "$ED/safe.err"; then
    ok "-n writes nothing"
else
    bad "-n writes nothing"
fi
"$CLI" "$d/kinds.rsc" -c 'mkdir NEWT' -c 'rm -r /DIALOG' -c 'rm /NOSUCH' 2> /dev/null
if [ $? = 1 ] && bytes_same "$d/kinds.rsc" "$FIX/kinds.rsc" && bytes_same "$d/kinds.dfn" "$FIX/kinds.dfn" && [ "$(ls "$d" | wc -l | tr -d ' ')" = 2 ]; then
    ok "a failing last -c writes nothing"
else
    bad "a failing last -c writes nothing"
fi
cp "$FIX/kinds-lost.rsc" "$d/lost.rsc"
"$CLI" "$d/lost.rsc" -c 'ls -lR' -c 'tree' -c 'cp /DIALOG/OK /x' > /dev/null 2>&1
if bytes_same "$d/lost.rsc" "$FIX/kinds-lost.rsc"; then ok "a failed edit command: the file is not rewritten"; else bad "a failed edit command: the file is not rewritten"; fi
"$CLI" "$d/lost.rsc" -c 'ls -lR' > /dev/null 2>&1
if bytes_same "$d/lost.rsc" "$FIX/kinds-lost.rsc"; then ok "no edit command: the file is not rewritten"; else bad "no edit command: the file is not rewritten"; fi
"$CLI" -o "$d/o.rsc" "$d/lost.rsc" ls / > /dev/null 2> "$d/err"
if [ $? = 0 ] && [ ! -e "$d/o.rsc" ] && grep -q 'no edit command ran' "$d/err"; then ok "-o without an edit command writes nothing, with a note"; else bad "-o without an edit command writes nothing, with a note"; fi
# a result the format cannot hold is not written
cp "$FIX/deep.rsc" "$d/deep.rsc"
"$CLI" "$d/deep.rsc" -c 'chmod +EXIT /#0/#5' 2> "$d/err" > /dev/null
if [ $? = 1 ] && grep -q 'cannot write the result' "$d/err"; then ok "a result the format cannot hold is refused"; else bad "a result the format cannot hold is refused"; fi
# write failure: the rename onto a directory fails; the new name file is removed again, an old one is kept
mkdir -p "$d/dirout.rsc"
"$CLI" -o "$d/dirout.rsc" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 1 ] && [ ! -e "$d/dirout.dfn" ] && grep -q 'the new name file was removed' "$d/err"; then
    ok "write failure: the new name file is removed"
else
    bad "write failure: the new name file is removed"
fi
echo old > "$d/dirout.dfn"
"$CLI" -o "$d/dirout.rsc" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 1 ] && [ -f "$d/dirout.dfn" ] && ! grep -q old "$d/dirout.dfn" && grep -q 'name file had already been replaced' "$d/err"; then
    ok "write failure: an old name file stays replaced, and the error says so"
else
    bad "write failure: an old name file stays replaced, and the error says so"
fi
"$CLI" -o "$d/nodir/x.rsc" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 1 ] && [ ! -e "$d/nodir" ] && grep -q 'cannot write' "$d/err"; then ok "write into no directory: nothing written"; else bad "write into no directory: nothing written"; fi
# names are written before the resource: a name file that cannot be written leaves the resource alone
mkdir -p "$d/dirdfn.dfn"
"$CLI" -o "$d/dirdfn.rsc" "$d/kinds.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 1 ] && [ ! -e "$d/dirdfn.rsc" ] && grep -q 'cannot write the names' "$d/err"; then ok "names write failure: no resource written"; else bad "names write failure: no resource written"; fi
# the mode of the replaced file is kept
cp "$FIX/kinds.rsc" "$d/m.rsc"
chmod 640 "$d/m.rsc"
"$CLI" --no-names "$d/m.rsc" mkdir NEWT 2> /dev/null
if [ "$(ls -l "$d/m.rsc" | cut -c1-10)" = "-rw-r-----" ]; then ok "write-back keeps the mode of the file"; else bad "write-back keeps the mode of the file"; fi
if [ "$(ls "$d" | grep -c '\.[A-Za-z0-9]\{6\}$')" = 0 ]; then ok "no temporary files are left"; else bad "no temporary files are left"; fi

# ---- round trips: back to the original, byte-identical to the writer's output
# rt LABEL FILE WANT [-c ...]: the options are in $RTOPT
RTOPT=
rt() {
    label=$1 file=$2 want=$3
    shift 3
    cp "$file" "$ED/rt.rsc"
    "$CLI" --selfcheck --no-names $RTOPT "$ED/rt.rsc" "$@" 2> /dev/null
    r1=$?
    if [ $r1 = 0 ] && cmp -s "$ED/rt.rsc" "$want"; then ok "round trip $label"; else bad "round trip $label"; fi
}
for g in rcs rcslow cicon; do
    f=tests/golden/$g.rsc
    w=tests/golden/$g.out.rsc
    LEAF=3
    if [ $g = cicon ]; then LEAF=1; fi
    rt "$g mkdir/rm" "$f" "$w" -c 'mkdir XX' -c 'rm -r /XX'
    rt "$g mkdir menu/rm" "$f" "$w" -c 'mkdir XX --type menu' -c 'rm -r /XX'
    rt "$g mkdir alert/rm" "$f" "$w" -c 'mkdir XX --type alert' -c 'rm /.strings/XX'
    rt "$g cp obj/rm" "$f" "$w" -c "cp /#0/#$LEAF /#0/XX" -c 'rm /#0/XX'
    rt "$g cp -r tree/rm" "$f" "$w" -c 'cp -r /#0 /XX' -c 'rm -r /XX'
    rt "$g touch/rm" "$f" "$w" -c 'touch /#0/XX --type button' -c 'rm /#0/XX'
    rt "$g touch string/rm" "$f" "$w" -c 'touch /.strings/XX' -c 'rm /.strings/XX'
    rt "$g touch image/rm" "$f" "$w" -c 'touch /.images/XX' -c 'rm /.images/XX'
    rt "$g mv tree A B, B A" "$f" "$w" -c 'mv /#0 /AA' -c 'mv /AA /BB'
    PAR=2
    if [ $g = cicon ]; then PAR=0; fi
    rt "$g mv obj A B, B A" "$f" "$w" -c "mv /#0/#$LEAF /#0/#$PAR/AA" -c "mv /#0/AA /#0/#$PAR/BB"
    rt "$g chmod and back" "$f" "$w" -c "chmod +EXIT /#0/#$LEAF" -c "chmod -EXIT /#0/#$LEAF"
    RTOPT=--pixels
    rt "$g pixels mkdir/rm" "$f" "$w" -c 'mkdir XX' -c 'touch /XX/B' -c 'rm -r /XX'
    RTOPT=
done
rt "kinds mv object away and back" "$FIX/kinds.rsc" "$FIX/kinds.rsc" -c 'mv /#0/#19 /#0/#5/XX' -c 'mv /#0/XX /#0'
rt "kinds cp cicon/rm" "$FIX/kinds.rsc" "$FIX/kinds.rsc" -c 'cp /#0/#14 /#2/XX' -c 'rm /#2/XX'
rt "kinds mv across trees and back" "$FIX/kinds.rsc" "$FIX/kinds.rsc" -c 'mv /#2/#1 /#3/XX' -c 'mv /#3/XX /#2/YY'
rt "kinds mv the last tree to the end" "$FIX/kinds.rsc" "$FIX/kinds.rsc" -c 'mv /#6 /'
cp "$FIX/kinds.rsc" "$ED/rt.rsc"
"$CLI" --no-names "$ED/rt.rsc" -c 'mkdir XX' 2> /dev/null && "$CLI" --no-names "$ED/rt.rsc" -c 'rm -r /#7' 2> /dev/null
if bytes_same "$ED/rt.rsc" "$FIX/kinds.rsc"; then ok "round trip across two runs (write-back, read, write-back)"; else bad "round trip across two runs (write-back, read, write-back)"; fi

# ---- the merge: cp -r of every tree of B into A equals the merge of the library
merge_case() {
    a=$1 b=$2 label=$3
    "$MKFIX" merge "$a" "$b" "$ED/merge.want.rsc" || { bad "merge $label (mkfix)"; return; }
    n=$("$CLI" --no-names "$b" ls / | grep -vc '^\.')
    cmds=""
    i=0
    while [ $i -lt "$n" ]; do
        cmds="$cmds -c 'cp -r $b:/#$i /'"
        i=$((i + 1))
    done
    ns=$("$CLI" --no-names "$b" ls /.strings | wc -l | tr -d ' ')
    j=0
    while [ $j -lt "$ns" ]; do
        cmds="$cmds -c 'cp $b:/.strings/#$j /.strings'"
        j=$((j + 1))
    done
    ni=$("$CLI" --no-names "$b" ls /.images | wc -l | tr -d ' ')
    j=0
    while [ $j -lt "$ni" ]; do
        cmds="$cmds -c 'cp $b:/.images/#$j /.images'"
        j=$((j + 1))
    done
    cp "$a" "$ED/merge.rsc"
    eval "\"$CLI\" --selfcheck --no-names \"$ED/merge.rsc\" $cmds" 2> /dev/null
    mrc=$?
    "$CLI" --no-names "$ED/merge.rsc" -c 'ls -lR' > "$ED/merge.got.ls" 2>&1
    "$CLI" --no-names "$ED/merge.want.rsc" -c 'ls -lR' > "$ED/merge.want.ls" 2>&1
    if [ $mrc = 0 ] && [ -s "$ED/merge.got.ls" ] && cmp -s "$ED/merge.got.ls" "$ED/merge.want.ls" && cmp -s "$ED/merge.rsc" "$ED/merge.want.rsc"; then
        ok "merge $label: cp -r of all trees equals the merged listing and file"
    else
        bad "merge $label: cp -r of all trees equals the merged listing and file (exit status $mrc)"
    fi
}
merge_case "$ROOT/tests/golden/rcs.out.rsc" "$ROOT/tests/golden/cicon.out.rsc" "rcs + cicon"
merge_case "$ROOT/tests/golden/cicon.out.rsc" "$ROOT/tests/golden/rcs.out.rsc" "cicon + rcs"
merge_case "$ROOT/tests/golden/rcs.out.rsc" "$ROOT/tests/golden/rcslow.out.rsc" "rcs + rcslow"
merge_case "$FIX/kinds.rsc" "$FIX/kinds.rsc" "kinds + kinds"
# a guessed tree name is not copied: the copy is named afresh
cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$ED/"
"$CLI" "$ED/kinds.rsc" -c 'cp -r /PANEL3 /' -c 'cp -r /PANEL3 /' -c 'ls /' > "$ED/g1" 2> /dev/null
if grep -q '^PANEL9$' "$ED/g1"; then ok "cp -r of a guessed tree is named afresh"; else bad "cp -r of a guessed tree is named afresh"; cat "$ED/g1"; fi

# ---- colour icons arrive intact, with every version and plane
cp "$FIX/kinds.rsc" "$ED/ci.rsc"
"$CLI" --no-names "$ED/ci.rsc" -c "cp $FIX/kinds.rsc:/#0/#14 /#2/ICONCOPY" -c "cp $ROOT/tests/golden/cicon.rsc:/#0/#1 /#2/ICONCOPY2" 2> /dev/null
if "$MKFIX" cicon-equal "$FIX/kinds.rsc" 0 14 "$ED/ci.rsc" 2 2 && "$MKFIX" cicon-equal "$ROOT/tests/golden/cicon.rsc" 0 1 "$ED/ci.rsc" 2 3 \
    && ! "$MKFIX" cicon-equal "$FIX/kinds.rsc" 0 14 "$ED/ci.rsc" 2 3; then
    ok "colour icons: every version and plane arrives intact (same file and a second file)"
else
    bad "colour icons: every version and plane arrives intact (same file and a second file)"
fi

# ---- hostile inputs are fast
t0=$(date +%s)
"$CLI" -n "$FIX/deep.rsc" -c 'mv /#0/#30000 /#0/#2' 2> "$ED/deep.err" > /dev/null
drc=$?
"$CLI" -n "$FIX/deep.rsc" -c 'cp -r /#0/#2 /#0/COPY' 2> "$ED/deep2.err" > /dev/null
drc2=$?
"$CLI" -n "$FIX/deep.rsc" -c 'mv /#0/#2 /#0/#32766/X' 2> "$ED/deep3.err" > /dev/null
drc3=$?
t1=$(date +%s)
if [ $drc = 1 ] && [ $drc2 = 1 ] && [ $drc3 = 1 ] && grep -q 'cannot copy' "$ED/deep2.err" && grep -q 'is inside' "$ED/deep3.err" && [ $((t1 - t0)) -lt 120 ]; then
    ok "32767-level chain: mv and cp refused or handled in $((t1 - t0)) s"
else
    bad "32767-level chain: mv and cp ($drc $drc2 $drc3, $((t1 - t0)) s)"
    cat "$ED/deep.err" "$ED/deep2.err" "$ED/deep3.err" | head
fi
t0=$(date +%s)
"$CLI" -n --names "$FIX/huge.hrd" "$FIX/huge.rsc" -c 'touch /#0/NEWOBJ' > /dev/null 2> "$ED/huge1.err"
h1=$?
"$CLI" --selfcheck -n --names "$FIX/huge.hrd" "$FIX/huge.rsc" -c 'cp /#0/#5 /#0/NEWOBJ' > /dev/null 2> "$ED/huge2.err"
h2=$?
"$CLI" -n --names "$FIX/huge.hrd" "$FIX/huge.rsc" -c 'rm /#0/#5' -c 'stat /#0/N6' > "$ED/huge3.out" 2> "$ED/huge3.err"
h3=$?
t1=$(date +%s)
if [ $h1 = 1 ] && [ $h2 = 1 ] && [ $h3 = 1 ] && grep -q 'name list is full' "$ED/huge1.err" && grep -q 'name list is full' "$ED/huge2.err" \
    && grep -q '^index: 5$' "$ED/huge3.out" && grep -q 'cannot write the result' "$ED/huge3.err" && [ $((t1 - t0)) -lt 300 ]; then
    ok "65535 names: touch and cp refused cleanly, rm renumbers, in $((t1 - t0)) s"
else
    bad "65535 names ($h1 $h2 $h3, $((t1 - t0)) s)"
    head -3 "$ED/huge1.err" "$ED/huge2.err" "$ED/huge3.err"
fi

# ---- review fixes: names follow the writer's numbering, files, symlinks, no-op edits
# names_follow LABEL -c ...: after the edits the in-memory listing, without the indices, equals the
# re-read listing of the written file with its .dfn
names_follow() {
    label=$1
    shift
    d=$ED/follow
    rm -rf "$d"
    mkdir -p "$d"
    cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$d/"
    "$CLI" --selfcheck --no-guess "$d/kinds.rsc" "$@" -c 'ls -lR' 2> /dev/null | sed -e 's/^#[0-9]* *//' -e 's/ LASTOB / - /' -e 's/LASTOB,//' -e 's/,LASTOB//' > "$d/mem.ls"
    "$CLI" --no-guess "$d/kinds.rsc" -c 'ls -lR' 2>&1 | sed -e 's/^#[0-9]* *//' -e 's/ LASTOB / - /' -e 's/LASTOB,//' -e 's/,LASTOB//' > "$d/disk.ls"
    if [ -s "$d/mem.ls" ] && cmp -s "$d/mem.ls" "$d/disk.ls"; then ok "names follow the objects: $label"; else bad "names follow the objects: $label"; diff "$d/mem.ls" "$d/disk.ls" | head -6; fi
}
names_follow "mv inside a tree" -c 'mv /DIALOG/OK /DIALOG/BUTTONS'
names_follow "mv to the front of a container" -c 'mv /DIALOG/EDIT /DIALOG/BUTTONS' -c 'mv /DIALOG/ICON /DIALOG/BUTTONS'
names_follow "cp into a nested container" -c 'cp /DIALOG/OK /DIALOG/BUTTONS/COPY1'
names_follow "cp -r into a nested container" -c 'cp -r /DIALOG/BUTTONS /DIALOG/BUTTONS/INNER'
names_follow "touch into a nested container" -c 'touch /DIALOG/BUTTONS/NEW1 --type button'
names_follow "mv across trees into a nested container" -c 'mv /DIALOG/EDIT /MAINMENU/#5/EDIT2'
names_follow "cp across trees" -c 'cp /DIALOG/OK /MAINMENU/#5/OK2'
names_follow "rename and touch" -c 'set /DIALOG/CANCEL name=NO' -c 'touch /DIALOG/BUTTONS/NEW1'
# the repro of the review: the name OK stays on the DEFAULT button
d=$ED/follow
cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$d/"
"$CLI" --no-guess "$d/kinds.rsc" mv /DIALOG/OK /DIALOG/BUTTONS 2> /dev/null
if "$CLI" --no-guess "$d/kinds.rsc" ls -l /DIALOG/BUTTONS | head -2 | tr '\n' ' ' | grep -q 'CANCEL .*SELECTABLE,EXIT SELECTED.* OK .*SELECTABLE,DEFAULT,EXIT'; then
    ok "mv OK to the end of BUTTONS: OK is still the default button after a re-read"
else
    bad "mv OK to the end of BUTTONS: OK is still the default button after a re-read"
fi

# entries of a .dfn that do not match are reported when the .dfn is replaced
d=$ED/dropped
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$d/a.rsc"
cp "$FIX/drop.dfn" "$d/a.dfn"
"$CLI" "$d/a.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 0 ] && grep -q 'warning: 3 name entries do not match' "$d/err" && grep -q 'warning: 3 entries of .*a.dfn that did not match the resource are not written' "$d/err"; then
    ok "a .dfn replaced in place: the entries that were dropped are reported"
else
    bad "a .dfn replaced in place: the entries that were dropped are reported"; cat "$d/err"
fi
# an explicitly named input .dfn is replaced in place, no INPUT.dfn appears
d=$ED/explicit
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$d/a.rsc"
cp "$FIX/kinds.dfn" "$d/n.dfn"
"$CLI" --names "$d/n.dfn" "$d/a.rsc" mkdir NEWT 2> "$d/err"
if [ $? = 0 ] && [ ! -e "$d/a.dfn" ] && [ ! -s "$d/err" ] && "$CLI" --names "$d/n.dfn" "$d/a.rsc" ls / | grep -q '^NEWT$'; then
    ok "--names X.dfn: that file is replaced in place"
else
    bad "--names X.dfn: that file is replaced in place"
fi
# the 8 / 9 byte limit of a .dfn name
d=$ED/limit
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$d/a.rsc"
"$CLI" "$d/a.rsc" mkdir ABCDEFGH 2> "$d/err8"
r8=$?
cp "$FIX/kinds.rsc" "$d/b.rsc"
"$CLI" "$d/b.rsc" mkdir ABCDEFGHI 2> "$d/err9"
r9=$?
cp "$FIX/kinds.rsc" "$d/c.rsc"
"$CLI" --names-out "$d/c.dfn" "$d/c.rsc" -c 'mkdir ABCDEFGH' -c 'mkdir ABCDEFGHI' 2> "$d/err9o" 
r9o=$?
if [ $r8 = 0 ] && [ $r9 = 1 ] && grep -q '1 names do not fit' "$d/err9" && [ ! -e "$d/b.dfn" ] && bytes_same "$d/b.rsc" "$FIX/kinds.rsc"; then
    ok "a .dfn name of 8 bytes is written, one of 9 refuses the write"
else
    bad "a .dfn name of 8 bytes is written, one of 9 refuses the write"; cat "$d/err9"
fi
if [ $r9o = 0 ] && grep -q 'warning: 1 names do not fit' "$d/err9o" && "$CLI" --names "$d/c.dfn" "$d/c.rsc" ls / | grep -q '^ABCDEFGH$'; then
    ok "--names-out: the 8 byte name is written, the 9 byte one reported"
else
    bad "--names-out: the 8 byte name is written, the 9 byte one reported"; cat "$d/err9o"
fi

# a symbolic link stays a link, its target is written, the mode stays
d=$ED/link
rm -rf "$d"
mkdir -p "$d/real"
cp "$FIX/kinds.rsc" "$d/real/r.rsc"
chmod 640 "$d/real/r.rsc"
ln -s real/r.rsc "$d/link.rsc"
"$CLI" "$d/link.rsc" mkdir ZZ 2> /dev/null
if [ -L "$d/link.rsc" ] && [ "$(ls -l "$d/real/r.rsc" | cut -c1-10)" = "-rw-r-----" ] \
    && "$CLI" "$d/link.rsc" ls / | grep -q '^ZZ$' && [ -f "$d/link.dfn" ]; then
    ok "write-back through a symbolic link writes the target and keeps the link and the mode"
else
    bad "write-back through a symbolic link writes the target and keeps the link and the mode"
fi
# a read-only input is refused before anything is written
d=$ED/readonly
rm -rf "$d"
mkdir -p "$d"
cp "$FIX/kinds.rsc" "$FIX/kinds.dfn" "$d/"
chmod 444 "$d/kinds.rsc"
"$CLI" "$d/kinds.rsc" mkdir ZZ 2> "$d/err"
rr=$?
chmod 644 "$d/kinds.rsc"
chmod 444 "$d/kinds.dfn"
"$CLI" "$d/kinds.rsc" mkdir ZZ 2> "$d/err2"
rr2=$?
chmod 644 "$d/kinds.dfn"
if [ $rr = 1 ] && [ $rr2 = 1 ] && grep -q 'not writable; nothing was written' "$d/err" && grep -q 'not writable; nothing was written' "$d/err2" \
    && bytes_same "$d/kinds.rsc" "$FIX/kinds.rsc" && bytes_same "$d/kinds.dfn" "$FIX/kinds.dfn"; then
    ok "a read-only input or name file is refused, nothing written"
else
    bad "a read-only input or name file is refused, nothing written"; cat "$d/err" "$d/err2"
fi
"$CLI" -n "$d/kinds.rsc" mkdir ZZ 2> /dev/null
# -n on a read-only file is fine
chmod 444 "$d/kinds.rsc"
"$CLI" -n "$d/kinds.rsc" mkdir ZZ 2> /dev/null && ok "-n does not mind a read-only input" || bad "-n does not mind a read-only input"
chmod 644 "$d/kinds.rsc"
# the result must read back
d=$ED/readback
mkdir -p "$d"
cp "$FIX/manyicons.rsc" "$d/m.rsc"
"$CLI" "$d/m.rsc" cp -r /#0 /X 2> "$d/err"
if [ $? = 1 ] && grep -q 'the library cannot read the result back' "$d/err" && bytes_same "$d/m.rsc" "$FIX/manyicons.rsc" && [ ! -e "$d/m.dfn" ]; then
    ok "a result the library cannot read back is refused, nothing written"
else
    bad "a result the library cannot read back is refused, nothing written"; cat "$d/err"
fi
# touch of an existing node, chmod and set that change nothing: no write, no name file
d=$ED/noop
rm -rf "$d"
mkdir -p "$d"
cp "$FIX/kinds-lost.rsc" "$d/k.rsc"
"$CLI" "$d/k.rsc" -c 'touch /#0/#6' -c 'touch /' -c 'chmod +SELECTABLE /#0/#6' -c 'chmod --state -SELECTED /#0/#6' -c 'set /#0/#6 text=OK x=0 type=G_BUTTON' 2> "$d/err"
if [ $? = 0 ] && [ ! -s "$d/err" ] && bytes_same "$d/k.rsc" "$FIX/kinds-lost.rsc" && [ ! -e "$d/k.dfn" ]; then
    ok "touch of an existing node and no-op chmod, set: the file is not written"
else
    bad "touch of an existing node and no-op chmod, set: the file is not written"; cat "$d/err"
fi
"$CLI" "$d/k.rsc" -c 'touch /#0/#6' -c 'chmod +TOUCHEXIT /#0/#7' 2> /dev/null
if [ $? = 0 ] && ! bytes_same "$d/k.rsc" "$FIX/kinds-lost.rsc"; then ok "a real change after no-op commands is written"; else bad "a real change after no-op commands is written"; fi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
