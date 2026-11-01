#!/bin/sh
# cmp-objs.sh BASE_DIR NEW_DIR [OBJDUMP]
# Compare, function by function, the disassembly of the objects in two
# build directories (e.g. build/m68k/src of a baseline and of a changed
# tree).  Absolute addresses and branch targets are masked.  Only the
# functions that exist in the baseline are compared; functions that exist
# only in the new tree are listed as added.  Exit status 1 on a difference.
base=$1
new=$2
od=${3:-objdump}
[ -d "$base" ] && [ -d "$new" ] || { echo "usage: $0 BASE_DIR NEW_DIR [OBJDUMP]" >&2; exit 2; }

dump() {	# object -> "function<TAB>masked line" for every instruction
    "$od" -dr --no-show-raw-insn "$1" 2>/dev/null | awk '
        /^[0-9a-f]+ <.*>:$/ { fn = $2; sub(/^</, "", fn); sub(/>:$/, "", fn); next }
        /^[ \t]+[0-9a-f]+:/ && fn != "" {
            line = $0
            sub(/^[ \t]+[0-9a-f]+:[ \t]*/, "", line)          # own address
            gsub(/[ \t]+/, " ", line)
            gsub(/[0-9a-f]+ <[^>+]+\+0x[0-9a-f]+>/, "@ <&>", line)
            gsub(/@ <[0-9a-f]+ /, "@ <", line)                  # branch target address
            gsub(/[0-9a-f]+ <[^>+]+>/, "&", line)
            gsub(/[0-9a-f]+ </, "<", line)                      # call target address
            gsub(/\[[0-9a-f]+\]/, "[X]", line)
            print fn "\t" line
        }'
}

status=0
for b in "$base"/*.o; do
    o=$(basename "$b")
    [ -f "$new/$o" ] || { echo "MISSING $o"; status=1; continue; }
    dump "$b" > /tmp/cmpobjs.$$.b
    dump "$new/$o" > /tmp/cmpobjs.$$.n
    for f in $(cut -f1 /tmp/cmpobjs.$$.b | sort -u); do
        grep "^$f	" /tmp/cmpobjs.$$.b > /tmp/cmpobjs.$$.bf
        grep "^$f	" /tmp/cmpobjs.$$.n > /tmp/cmpobjs.$$.nf
        if ! cmp -s /tmp/cmpobjs.$$.bf /tmp/cmpobjs.$$.nf; then
            echo "DIFF $o $f"; status=1
        fi
    done
    for f in $(cut -f1 /tmp/cmpobjs.$$.n | sort -u); do
        grep -q "^$f	" /tmp/cmpobjs.$$.b || echo "added $o $f"
    done
done
rm -f /tmp/cmpobjs.$$.*
[ $status = 0 ] && echo "all baseline functions identical"
exit $status
