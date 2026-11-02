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
# The cases are in tests/cli/cases/AREA.cases, one file per area (ls-tree,
# stat, cp, paths, ...).  A file is a list of blocks, one per case:
#
#   === NAME              starts a case (NAME: letters, digits and . _ : + -)
#   cmd ARGUMENTS         the rest of the line: the arguments of one rcs-cli
#                         invocation, split by the shell (quotes work) after
#                         $ROOT, $FIX and $TMP are replaced by the repository
#                         root, the fixture directory and a scratch directory
#                         (none of them may contain spaces or quotes)
#   rc STATUS             the exit status (0 when absent)
#   cksum SUM LENGTH      the cksum of stdout, instead of an "out" section
#   out                   the expected stdout: every line of it follows,
#   err                   the expected stderr (empty when absent), the same way
#   |TEXT                 one line of a section, "|" followed by the exact
#                         line; a section is the lines "|..." after its name
#   \                     (alone on a line, after the last "|" line of a
#                         section) the section does not end in a newline
#
# Every line of a section starts with "|", so no output can look like a
# marker, and the bytes are kept as they are.  Empty lines and lines starting
# with "#" between the blocks are ignored (UPDATE=1 does not keep them).
# A block "=== edit:NAME" with only an "out" section is the expected re-read
# listing of the write-back test "edit NAME" below.  The output is compared
# after the three directories are replaced back by $ROOT, $FIX and $TMP, so
# the expected files do not depend on where the tree is.  UPDATE=1 rewrites
# the sections and the rc of every block (the cmd lines are kept).

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

# split_cases DIR FILE...: writes every block of the .cases files as DIR/NAME.cmd, .rc, .cksum, .out
# and .err (a file exists when the block has the line or section), keeping the bytes
split_cases() {
    sdir=$1
    shift
    awk -v dir="$sdir" '
    function fail(msg) { print FILENAME ":" FNR ": " msg > "/dev/stderr"; bad = 1; exit 2 }
    function endsec(   f) {
        if (sec == "") return
        f = dir "/" name "." sec
        printf "%s", buf > f
        close(f)
        sec = ""
        buf = ""
    }
    function put(ext, text,   f) { f = dir "/" name "." ext; printf "%s\n", text > f; close(f) }
    FNR == 1 { endsec(); name = "" }
    /^\|/ {
        if (sec == "") fail("a line starting with | outside a section")
        buf = buf substr($0, 2) "\n"
        next
    }
    $0 == "\\" {
        if (sec == "" || buf == "") fail("\\ must follow the lines of a section")
        buf = substr(buf, 1, length(buf) - 1)
        endsec()
        next
    }
    { endsec() }
    /^$/ || /^#/ { next }
    /^=== / {
        name = substr($0, 5)
        if (name !~ /^[A-Za-z0-9._:+-]+$/) fail("bad case name")
        if (name in seen) fail("case " name " is there twice")
        seen[name] = 1
        next
    }
    name == "" { fail("text before the first === line") }
    /^cmd / { put("cmd", substr($0, 5)); next }
    /^rc [0-9]+$/ { put("rc", substr($0, 4)); next }
    /^cksum [0-9]+ [0-9]+$/ { put("cksum", substr($0, 7)); next }
    $0 == "out" || $0 == "err" { sec = $0; buf = ""; f = dir "/" name "." sec; printf "" > f; close(f); next }
    { fail("unknown line: " $0) }
    END { if (!bad) endsec() }
    ' "$@"
}

# emit_section TAG FILE: a section of a block, as split_cases reads it
emit_section() {
    [ -f "$2" ] || return 0
    printf '%s\n' "$1"
    awk '{ print "|" $0 }' "$2"
    if [ -n "$(tail -c 1 "$2")" ]; then printf '\\\n'; fi
}

# rewrite_cases: UPDATE=1, the .cases files again from the split directory, in the order they have
rewrite_cases() {
    for cf in tests/cli/cases/*.cases; do
        {
            first=1
            for n in $(grep '^=== ' "$cf" | sed 's/^=== //'); do
                if [ $first = 1 ]; then first=0; else echo; fi
                printf '=== %s\n' "$n"
                if [ -f "$SPLIT/$n.cmd" ]; then printf 'cmd %s\n' "$(cat "$SPLIT/$n.cmd")"; fi
                if [ -f "$SPLIT/$n.rc" ]; then printf 'rc %s\n' "$(cat "$SPLIT/$n.rc")"; fi
                if [ -f "$SPLIT/$n.cksum" ]; then printf 'cksum %s\n' "$(cat "$SPLIT/$n.cksum")"; fi
                emit_section out "$SPLIT/$n.out"
                emit_section err "$SPLIT/$n.err"
            done
        } > "$WORK/rewrite.cases" && cp "$WORK/rewrite.cases" "$cf"
    done
}

# ---- the host image files of the image cases (tests/cli/cases/images.cases), made here byte by byte
# (not with the library under test): $TMP/pnm/NAME.  The 16 x 8 stripes a16: even rows
# 1111000011110000, odd rows 0000111100001111 (1 = black), as P1, P4, P2 (0 black), P5, P3, P6; thr.pgm:
# every row the grey values 0, 17, ... 255; c4.ppm: the pixel (x + y) & 15 of the 4 plane palette as
# colours; c4-off.ppm: the same with (5,2) and (1,3) a little off the palette; c2.ppm: (x + y) & 3
# at 2 planes; copy of the fixture as a second file
PN=$TMP/pnm
export PN
mkdir -p "$PN"
awk 'BEGIN { print "P1"; print "# stripes"; print "16 8"
    for (y = 0; y < 8; y++) { s = ""; for (x = 0; x < 16; x++) s = s ((int(x / 4) + y) % 2 == 0 ? "1" : "0"); print s } }' > "$PN/a16.pbm"
awk 'BEGIN { print "P2"; print "16 8"; print "255"
    for (y = 0; y < 8; y++) { s = ""; for (x = 0; x < 16; x++) s = s ((int(x / 4) + y) % 2 == 0 ? "0 " : "255 "); print s } }' > "$PN/a16.pgm"
awk 'BEGIN { print "P3"; print "# colours"; print "16 8"; print "255"
    for (y = 0; y < 8; y++) { s = ""; for (x = 0; x < 16; x++) s = s ((int(x / 4) + y) % 2 == 0 ? "0 0 0 " : "255 255 255 "); print s } }' > "$PN/a16.ppm3"
{ printf 'P4\n16 8\n'; for y in 0 1 2 3 4 5 6 7; do if [ $((y % 2)) = 0 ]; then printf '\360\360'; else printf '\017\017'; fi; done; } > "$PN/a16.pbm4"
{ printf 'P5\n16 8\n1\n'
  for y in 0 1 2 3 4 5 6 7; do
      for x in 0 1 2 3; do if [ $(((x + y) % 2)) = 0 ]; then printf '\000\000\000\000'; else printf '\001\001\001\001'; fi; done
  done; } > "$PN/a16.pgm5"
{ printf 'P6\n16 8\n255\n'
  for y in 0 1 2 3 4 5 6 7; do
      for x in 0 1 2 3; do if [ $(((x + y) % 2)) = 0 ]; then printf '\000\000\000\000\000\000\000\000\000\000\000\000'; else printf '\377\377\377\377\377\377\377\377\377\377\377\377'; fi; done
  done; } > "$PN/a16.ppm"
awk 'BEGIN { print "P2"; print "16 8"; print "255"
    for (y = 0; y < 8; y++) { s = ""; for (x = 0; x < 16; x++) s = s (x * 17) " "; print s } }' > "$PN/thr.pgm"
awk 'BEGIN {
    split("255 255 255 255 0 0 0 255 0 255 255 0 0 0 255 255 0 255 0 255 255 182 182 182 109 109 109 255 109 109 109 255 109 255 255 109 109 109 255 255 109 255 109 255 255 0 0 0", c, " ")
    for (v = 0; v < 2; v++) {
        name = v ? "c4-off" : "c4"
        f = ENVIRON["PN"] "/" name ".ppm"
        print "P3" > f; print "16 8" > f; print "255" > f
        for (y = 0; y < 8; y++) { s = ""
            for (x = 0; x < 16; x++) { p = (x + y) % 16
                if (v && x == 5 && y == 2) s = s "250 5 5 "
                else if (v && x == 1 && y == 3) s = s "3 250 3 "
                else s = s c[3 * p + 1] " " c[3 * p + 2] " " c[3 * p + 3] " " }
            print s > f }
        close(f) }
    split("255 255 255 255 0 0 0 255 0 0 0 0", d, " ")
    f = ENVIRON["PN"] "/c2.ppm"
    print "P3" > f; print "16 8" > f; print "255" > f
    for (y = 0; y < 8; y++) { s = ""; for (x = 0; x < 16; x++) { p = (x + y) % 4; s = s d[3 * p + 1] " " d[3 * p + 2] " " d[3 * p + 3] " " } print s > f }
    close(f) }'
cp "$FIX/images.rsc" "$FIX/images.dfn" "$PN/"
# where the export cases write (and two files that are there already)
mkdir -p "$TMP/exp"
echo old > "$TMP/exp/exists.pbm"
echo old > "$TMP/exp/exists2.pbm"

SPLIT=$WORK/cases
mkdir -p "$SPLIT"
split_cases "$SPLIT" tests/cli/cases/*.cases || { echo "run.sh: tests/cli/cases/*.cases are malformed" >&2; exit 2; }

for cmd in "$SPLIT"/*.cmd; do
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
# the block "edit:NAME" of tests/cli/cases/write-back.cases (UPDATE=1 writes it).  The file argument is $ED/NAME/in.rsc; with -o
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
    if [ -n "$UPDATE" ]; then cp "$d/result" "$SPLIT/edit:$name.out"; fi
    if cmp -s "$d/result" "$SPLIT/edit:$name.out"; then
        ok "edit $name"
    else
        bad "edit $name"
        diff "$SPLIT/edit:$name.out" "$d/result" | head -20
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
edit wb-img-import images "$ED/wb-img-import/in.rsc" -c "cp host:$PN/c4.ppm /ICONS/CIC0/4.sel" -c "cp --mask host:$PN/a16.pbm /ICONS/CIC0/4" -c "cp host:$PN/a16.pbm /ICONS/BM"
edit wb-img-versions images "$ED/wb-img-versions/in.rsc" -c 'touch /ICONS/CIC0/8.sel' -c 'rm /ICONS/CIC/4.sel' -c 'rm /ICONS/CIC/2' -c 'cp /ICONS/CIC/8 /ICONS/CIC0/2'
edit wb-img-set images "$ED/wb-img-set/in.rsc" -c 'set /ICONS/CIC iw=8 ih=4' -c 'set /ICONS/IC20 iw=40' -c 'set /.images/SMALL iw=16 ih=2' -c 'set /ICONS/BM iw=10'
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

# ---- images: host files, write-back, hostile input ----------------------------------------
IM=$TMP/img
rm -rf "$IM"
mkdir -p "$IM"
IMG=$FIX/images.rsc

# same_as FILE ARGS...: FILE equals the stdout of rcs-cli on images.rsc with ARGS
same_as() {
    f=$1
    shift
    "$CLI" "$IMG" "$@" > "$IM/same.out" 2> /dev/null && cmp -s "$f" "$IM/same.out"
}
# exported files (the cases of images.cases wrote them) are what cat prints
ck() { # LABEL FILE ARGS...
    lbl=$1
    shift
    if same_as "$@"; then ok "export $lbl equals cat"; else bad "export $lbl equals cat"; fi
}
E=$TMP/exp
ck ic16 "$E/e-ic16.pbm" cat --image /ICONS/IC16
ck ic16-ascii "$E/e-ic16.pbm1" cat --ascii /ICONS/IC16
ck ic16-mask "$E/e-ic16m.pbm" cat --mask --image /ICONS/IC16
ck ic20 "$E/e-ic20.pbm" cat --image /ICONS/IC20
ck bitmap "$E/e-bm.pbm" cat /ICONS/BM
ck free-image "$E/e-small.pbm" cat /.images/SMALL
ck cic4 "$E/e-cic4.ppm" cat /ICONS/CIC/4
ck cic4-ascii "$E/e-cic4.ppm3" cat --ascii /ICONS/CIC/4
ck cic4-sel "$E/e-cic4s.ppm" cat /ICONS/CIC/4.sel
ck cic2 "$E/e-cic2.ppm" cat /ICONS/CIC/2
ck cic8 "$E/e-cic8.ppm" cat /ICONS/CIC/8
ck cic4-mask "$E/e-cic4m.pbm" cat --mask /ICONS/CIC/4
ck mono "$E/e-mono.pbm" cat /ICONS/CIC/mono
ck second-file "$E/e-second.ppm" cat /ICONS/CIC/2
# the bytes of a bitmap, written by hand
printf 'P4\n16 4\n\377\000\017\360\125\252\200\001' > "$IM/bm.want"
if cmp -s "$IM/bm.want" "$E/e-bm.pbm"; then ok "export of the bitmap is P4 with the right bytes"; else bad "export of the bitmap is P4 with the right bytes"; fi
# round trips: export, import, export again: the same bytes
if cmp -s "$E/rt1.ppm" "$E/rt2.ppm" && cmp -s "$E/rt3.pbm" "$E/rt4.pbm"; then ok "export, import, export: identical files"; else bad "export, import, export: identical files"; fi
# an existing file stays unless -f; failed exports leave nothing
if [ "$(cat "$E/exists.pbm")" = old ] && cmp -s "$E/exists2.pbm" "$E/e-ic16.pbm"; then ok "export: an existing file is kept, -f replaces it"; else bad "export: an existing file is kept, -f replaces it"; fi
if [ ! -e "$E/never.pbm" ] && [ ! -e "$E/never.ppm" ] && [ ! -d "$E/nodir" ] && [ -d "$E" ] \
    && [ "$(ls -A "$E" | grep -c '\.[A-Za-z0-9]\{6\}$')" = 0 ]; then
    ok "failed exports leave no file and no temporary file"
else
    bad "failed exports leave no file and no temporary file"
fi
if [ "$(ls -l "$E/e-ic16.pbm" | cut -c1-10)" != "-rw-------" ]; then ok "an exported file has the mode of a new file, not the temporary file's"; else bad "an exported file has the mode of a new file, not the temporary file's"; fi
# -f keeps the mode of the file it replaces
echo old > "$IM/mode.pbm"
chmod 640 "$IM/mode.pbm"
"$CLI" "$IMG" cp -f /ICONS/IC16 "host:$IM/mode.pbm" && [ "$(ls -l "$IM/mode.pbm" | cut -c1-10)" = "-rw-r-----" ] \
    && cmp -s "$IM/mode.pbm" "$E/e-ic16.pbm" && ok "-f keeps the mode of the replaced file" || bad "-f keeps the mode of the replaced file"
# an export is no edit: the resource is not written, also not with the name file
d=$IM/exp-only
mkdir -p "$d"
cp "$FIX/images.rsc" "$FIX/images.dfn" "$d/"
"$CLI" "$d/images.rsc" cp /ICONS/CIC/4 "host:$d/x.ppm" 2> "$d/err"
if [ $? = 0 ] && [ ! -s "$d/err" ] && bytes_same "$d/images.rsc" "$FIX/images.rsc" && bytes_same "$d/images.dfn" "$FIX/images.dfn" && [ -f "$d/x.ppm" ]; then
    ok "an export writes the image and not the resource"
else
    bad "an export writes the image and not the resource"
fi

# write-back and re-read: the image survives a write, an identical import writes nothing
bytes_same() { cmp -s "$1" "$2"; }
d=$IM/wb
mkdir -p "$d"
cp "$FIX/images.rsc" "$FIX/images.dfn" "$d/"
"$CLI" --selfcheck "$d/images.rsc" -c "cp host:$PN/c4-off.ppm /ICONS/CIC0/4.sel" -c "cp --mask host:$PN/thr.pgm /ICONS/CIC0/4" \
    -c "cp host:$PN/a16.pbm /ICONS/IC16" -c "touch /ICONS/CIC0/2" -c "rm /ICONS/CIC/8" -c "set /ICONS/IC20 iw=8 ih=2" \
    -c "cp /ICONS/CIC/2 /ICONS/CIC0/8" -c 'cat --ascii /ICONS/CIC0/4' -c 'cat --ascii --mask /ICONS/CIC0/4' -c 'cat --ascii /ICONS/CIC0/4.sel' \
    -c 'cat --ascii /ICONS/IC16' -c 'cat --ascii /ICONS/IC20' -c 'cat --ascii /ICONS/CIC0/8' -c 'ls -l /ICONS/CIC0' > "$d/mem.out" 2> "$d/mem.err"
mrc=$?
"$CLI" "$d/images.rsc" -c 'cat --ascii /ICONS/CIC0/4' -c 'cat --ascii --mask /ICONS/CIC0/4' -c 'cat --ascii /ICONS/CIC0/4.sel' \
    -c 'cat --ascii /ICONS/IC16' -c 'cat --ascii /ICONS/IC20' -c 'cat --ascii /ICONS/CIC0/8' -c 'ls -l /ICONS/CIC0' > "$d/disk.out" 2>&1
if [ $mrc = 0 ] && [ -s "$d/mem.out" ] && cmp -s "$d/mem.out" "$d/disk.out" && ! bytes_same "$d/images.rsc" "$FIX/images.rsc"; then
    ok "image edits are written and read back with the same pixels"
else
    bad "image edits are written and read back with the same pixels"; cat "$d/mem.err"; diff "$d/mem.out" "$d/disk.out" | head
fi
# import of the picture the image has: nothing is written (the file and its time stay)
cp "$d/images.rsc" "$d/before.rsc"
touch -t 200001010000 "$d/images.rsc"
"$CLI" "$d/images.rsc" -c "cp host:$PN/a16.pbm /ICONS/IC16" -c "cp --mask host:$PN/thr.pgm /ICONS/CIC0/4" 2> "$d/err"
if [ $? = 0 ] && [ ! -s "$d/err" ] && bytes_same "$d/images.rsc" "$d/before.rsc" && [ "$(ls -l "$d/images.rsc" | grep -c 'Jan  1  2000')" = 1 ]; then
    ok "an import of the same picture does not write the file"
else
    bad "an import of the same picture does not write the file"; cat "$d/err"
fi
# the same with -o: no OUT, and the note
rm -f "$d/o.rsc"
"$CLI" -o "$d/o.rsc" "$d/images.rsc" cp "host:$PN/a16.pbm" /ICONS/IC16 2> "$d/err"
if [ $? = 0 ] && [ ! -e "$d/o.rsc" ] && grep -q 'no edit command ran' "$d/err"; then ok "-o: an import of the same picture writes no OUT"; else bad "-o: an import of the same picture writes no OUT"; fi
# a different picture is written, with -o the input stays
"$CLI" -o "$d/o.rsc" "$d/images.rsc" cp "host:$PN/thr.pgm" /ICONS/IC16 2> "$d/err"
if [ $? = 0 ] && [ -f "$d/o.rsc" ] && bytes_same "$d/images.rsc" "$d/before.rsc" && ! bytes_same "$d/o.rsc" "$d/before.rsc"; then
    ok "-o: an image import goes to OUT, the input stays"
else
    bad "-o: an image import goes to OUT, the input stays"
fi
# all or nothing: a later failure writes nothing, also for the earlier image edits
for seq in "cp host:$PN/c4-off.ppm /ICONS/CIC/4|cp host:$PN/nosuch /ICONS/IC16" "touch /ICONS/CIC0/4|rm /ICONS/CIC/mono" \
    "set /ICONS/CIC iw=8|cp /ICONS/IC20 /ICONS/IC16" "rm /ICONS/CIC/4|cat --ascii /ICONS/CIC/4"; do
    cp "$FIX/images.rsc" "$d/a.rsc"
    cp "$FIX/images.dfn" "$d/a.dfn"
    "$CLI" --selfcheck "$d/a.rsc" -c "${seq%%|*}" -c "${seq##*|}" 2> /dev/null
    if [ $? = 1 ] && bytes_same "$d/a.rsc" "$FIX/images.rsc" && bytes_same "$d/a.dfn" "$FIX/images.dfn"; then
        ok "all or nothing: ${seq%%|*} then a failing command writes nothing"
    else
        bad "all or nothing: ${seq%%|*} then a failing command writes nothing"
    fi
done
# a cp host:FILE into a second file is refused and the main file is not written either
cp "$FIX/images.rsc" "$d/a.rsc"
"$CLI" "$d/a.rsc" -c 'touch /ICONS/CIC0/4' -c "cp host:$PN/a16.pbm $PN/images.rsc:/ICONS/IC16" 2> /dev/null
if [ $? = 1 ] && bytes_same "$d/a.rsc" "$FIX/images.rsc"; then ok "an import into a second file fails, nothing written"; else bad "an import into a second file fails, nothing written"; fi

# hostile host files: refused with one line, never a crash
H=$IM/hostile
mkdir -p "$H"
hostile() { # NAME BYTES(printf format)
    printf "$2" > "$H/$1"
    "$CLI" --selfcheck -n "$IMG" cp "host:$H/$1" /ICONS/IC16 > /dev/null 2> "$H/err"
    hrc=$?
    if [ $hrc = 1 ] && [ "$(wc -l < "$H/err" | tr -d ' ')" = 1 ] && grep -q '^rcs-cli: cp: host:' "$H/err"; then
        ok "hostile file $1 is refused"
    else
        bad "hostile file $1 is refused (exit status $hrc)"; head -3 "$H/err"
    fi
}
hostile empty ''
hostile magic 'P7\n16 8\n255\n'
hostile magic1 'P'
hostile text 'hello world\n'
hostile nodims 'P4\n'
hostile onedim 'P4\n16\n'
hostile zerodim 'P4\n0 8\n'
hostile negdim 'P4\n-16 8\n'
hostile hugedim 'P6\n2147483648 2147483648\n255\n'
hostile hugedim2 'P4\n32767 32767\n'
hostile hugedim3 'P6\n32767 32767\n255\nabc'
hostile overbudget 'P4\n4096 4096\n'
hostile maxval0 'P5\n16 8\n0\n'
hostile maxval256 'P5\n16 8\n256\n'
hostile maxval65535 'P6\n16 8\n65535\n'
hostile sampleover 'P2\n16 8\n10\n11 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n'
hostile badchar 'P1\n16 8\n1x\n'
hostile short 'P4\n16 8\n\001\002'
hostile shortp6 'P6\n16 8\n255\n\001\002\003'
hostile onlyws 'P1\n16 8\n       \n\n\n'
hostile comment 'P1\n# 16 8\n'
hostile longnum "P4\n$(awk 'BEGIN { for (i = 0; i < 70000; i++) printf "9" }') 8\n"
# the wrong size
printf 'P4\n8 8\n\377\377\377\377\377\377\377\377' > "$H/small.pbm"
"$CLI" -n "$IMG" cp "host:$H/small.pbm" /ICONS/IC16 2> "$H/err"
if [ $? = 1 ] && grep -q 'the image is 8x8 but /ICONS/IC16 is 16x8 (use --resize' "$H/err"; then ok "a file of the wrong size is refused"; else bad "a file of the wrong size is refused"; fi
# a file too large is not read (a sparse file of 65 MiB), a directory, a fifo and an unreadable file are refused
dd if=/dev/null of="$H/big.pbm" bs=1 seek=68157440 2> /dev/null
"$CLI" -n "$IMG" cp "host:$H/big.pbm" /ICONS/IC16 2> "$H/err"
if [ $? = 1 ] && grep -q 'file too large' "$H/err"; then ok "a file above 64 MiB is refused"; else bad "a file above 64 MiB is refused"; fi
rm -f "$H/big.pbm"
mkfifo "$H/fifo" 2> /dev/null && {
    "$CLI" -n "$IMG" cp "host:$H/fifo" /ICONS/IC16 2> "$H/err"
    if [ $? = 1 ] && grep -q 'not a regular file' "$H/err"; then ok "a fifo is refused without opening it"; else bad "a fifo is refused without opening it"; fi
}
printf 'P4\n16 8\n' > "$H/noperm.pbm"
chmod 000 "$H/noperm.pbm"
if [ -r "$H/noperm.pbm" ]; then
    :   # running as root: nothing to test
else
    "$CLI" -n "$IMG" cp "host:$H/noperm.pbm" /ICONS/IC16 2> "$H/err"
    if [ $? = 1 ] && grep -q 'cannot open' "$H/err"; then ok "an unreadable file is refused"; else bad "an unreadable file is refused"; fi
fi
chmod 644 "$H/noperm.pbm"
# the same files fed to the sweeps: every prefix and every flipped byte of good files
sweep pnm-c4-ppm "$PN/c4.ppm" 1 trunc --selfcheck -n "$IMG" -c "cp host:$S.ppm /ICONS/CIC/4"
sweep pnm-c4-ppm "$PN/c4.ppm" 1 flip --selfcheck -n "$IMG" -c "cp host:$S.ppm /ICONS/CIC/4"
sweep pnm-c4-resize "$PN/c4-off.ppm" 3 flip --selfcheck -n "$IMG" -c "cp --resize --exact host:$S.ppm /ICONS/CIC/2"
sweep pnm-a16-p4 "$PN/a16.pbm4" 1 trunc --selfcheck -n "$IMG" -c "cp host:$S.pbm4 /ICONS/IC16"
sweep pnm-a16-p4 "$PN/a16.pbm4" 1 flip --selfcheck -n "$IMG" -c "cp --resize host:$S.pbm4 /ICONS/IC20"
sweep pnm-a16-p1 "$PN/a16.pbm" 1 trunc --selfcheck -n "$IMG" -c "cp --mask host:$S.pbm /ICONS/IC16"
sweep pnm-a16-p1 "$PN/a16.pbm" 1 flip --selfcheck -n "$IMG" -c "cp --mask host:$S.pbm /ICONS/CIC/4.sel"
sweep pnm-thr "$PN/thr.pgm" 1 flip --selfcheck -n "$IMG" -c "cp --threshold 30 host:$S.pgm /ICONS/CIC0/4"
sweep images-flip "$IMG" 3 flip --selfcheck -n --no-names $S.rsc -c 'cat --ascii /ICONS/CIC/4' -c 'cp /ICONS/CIC/4 /ICONS/CIC/2' \
    -c 'touch /ICONS/CIC0/4.sel' -c 'rm /ICONS/CIC/2' -c 'set /ICONS/CIC iw=8' -c 'cat --mask --ascii /ICONS/IC16'
sweep images-trunc "$IMG" 5 trunc --selfcheck -n --no-names $S.rsc -c 'cat --ascii /ICONS/CIC/8' -c "cp host:$PN/c4.ppm /ICONS/CIC/8"

# a terminal is not sent a binary image (needs script(1); skipped without)
if script -q /dev/null true > /dev/null 2>&1 < /dev/null; then
    if [ "$(uname)" = Darwin ]; then
        script -q /dev/null "$CLI" "$IMG" cat /ICONS/BM > "$IM/tty1" 2>&1 < /dev/null
        script -q /dev/null "$CLI" "$IMG" cat -f /ICONS/BM > "$IM/tty2" 2>&1 < /dev/null
        script -q /dev/null "$CLI" "$IMG" cat --ascii /ICONS/BM > "$IM/tty3" 2>&1 < /dev/null
    else
        script -qec "'$CLI' '$IMG' cat /ICONS/BM" /dev/null > "$IM/tty1" 2>&1 < /dev/null
        script -qec "'$CLI' '$IMG' cat -f /ICONS/BM" /dev/null > "$IM/tty2" 2>&1 < /dev/null
        script -qec "'$CLI' '$IMG' cat --ascii /ICONS/BM" /dev/null > "$IM/tty3" 2>&1 < /dev/null
    fi
    if grep -q 'not writing a binary image to a terminal' "$IM/tty1" && ! grep -q P4 "$IM/tty1" && grep -q P4 "$IM/tty2" && grep -q P1 "$IM/tty3"; then
        ok "cat refuses a binary image on a terminal unless -f or --ascii"
    else
        bad "cat refuses a binary image on a terminal unless -f or --ascii"
    fi
fi

if [ -n "$UPDATE" ]; then rewrite_cases; fi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
