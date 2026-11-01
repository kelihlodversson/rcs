#!/bin/sh
# cmp-objs.sh BASE_DIR NEW_DIR [OBJDUMP]
# Compare the objects in two build directories (e.g. build/m68k/src of a
# baseline and of a changed tree):
#  - the disassembly of every function that exists in the baseline, with
#    absolute addresses and branch targets masked (functions that exist only
#    in the new tree are listed as "added", not compared);
#  - the contents of the .rodata* and .data* sections, byte for byte.
# Exit status: 0 identical, 1 a difference, 2 usage or objdump failure.
base=$1
new=$2
od=${3:-objdump}
[ -d "$base" ] && [ -d "$new" ] || { echo "usage: $0 BASE_DIR NEW_DIR [OBJDUMP]" >&2; exit 2; }

tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT INT TERM

dump() {	# object -> "function<TAB>masked line" for every instruction
    "$od" -dr --no-show-raw-insn "$1" 2>"$tmp/err" > "$tmp/raw" || {
        echo "objdump failed on $1:" >&2; cat "$tmp/err" >&2; return 1; }
    awk '
        /^[0-9a-f]+ <.*>:$/ { fn = $2; sub(/^</, "", fn); sub(/>:$/, "", fn); next }
        /^[ \t]+[0-9a-f]+:/ && fn != "" {
            line = $0
            sub(/^[ \t]+[0-9a-f]+:[ \t]*/, "", line)          # own address
            gsub(/[ \t]+/, " ", line)
            gsub(/[0-9a-f]+ <[^>+]+\+0x[0-9a-f]+>/, "@ <&>", line)
            gsub(/@ <[0-9a-f]+ /, "@ <", line)                  # branch target address
            gsub(/[0-9a-f]+ </, "<", line)                      # call target address
            print fn "\t" line
        }' "$tmp/raw"
}

data() {	# object -> contents of its read-only and initialised data
    secs=$("$od" -h "$1" 2>/dev/null | awk '$2 ~ /^\.(rodata|data)/ { print $2 }')
    for s in $secs; do
        echo "== $s"
        "$od" -s -j "$s" "$1" 2>&1 | sed '1,/^Contents of section/{/^Contents of section/!d;}'
    done
}

status=0
for b in "$base"/*.o; do
    o=$(basename "$b")
    [ -f "$new/$o" ] || { echo "MISSING $o"; status=1; continue; }
    dump "$b" > "$tmp/b" || exit 2
    dump "$new/$o" > "$tmp/n" || exit 2
    if [ ! -s "$tmp/b" ]; then
        echo "no instructions found in baseline $o (wrong objdump?)" >&2; exit 2
    fi
    for f in $(cut -f1 "$tmp/b" | sort -u); do
        grep "^$f	" "$tmp/b" > "$tmp/bf"
        grep "^$f	" "$tmp/n" > "$tmp/nf"
        cmp -s "$tmp/bf" "$tmp/nf" || { echo "DIFF $o $f"; status=1; }
    done
    for f in $(cut -f1 "$tmp/n" | sort -u); do
        grep -q "^$f	" "$tmp/b" || echo "added $o $f"
    done
    data "$b" > "$tmp/bd"
    data "$new/$o" > "$tmp/nd"
    cmp -s "$tmp/bd" "$tmp/nd" || { echo "DIFF $o (data sections)"; status=1; }
done
[ $status = 0 ] && echo "all baseline functions and data identical"
exit $status
