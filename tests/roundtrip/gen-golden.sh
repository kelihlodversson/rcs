#!/bin/sh
# Regenerate tests/golden/NAME.out.rsc: what the program writes after loading
# tests/golden/NAME.rsc, run under the pTOS virt-arm image.
#
#   sh tests/roundtrip/gen-golden.sh
#
# Only ARM is used. The m68k build with -DRCS_ROUNDTRIP panics (Line F,
# sr=0004 pc=004af361) during write_files() under the virt-m68k image, so it
# cannot produce the output and is not used here.
#
# Each input is run twice; the two outputs must be identical, or the script
# stops. Needs PTOS_ARM_ELF (see tests/emulator/qd.py), arm-none-eabi- on PATH,
# and qemu.  The program is built with -DRCS_ROUNDTRIP, which makes it write
# C:\OUT.RSC straight after loading; the objects of that build are removed
# afterwards so that the next normal build does not reuse them.
set -e

ARCH=arm
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

# the hooked objects must never survive this script, even when it is aborted
trap 'rm -rf "build/$ARCH/src" "build/$ARCH/RCS.PRG"' EXIT INT TERM
rm -rf "build/$ARCH/src" "build/$ARCH/RCS.PRG"
make "$ARCH" RCS_TEST_CFLAGS=-DRCS_ROUNDTRIP

run() {  # NAME RUN -> build/emulator/golden-NAME-RUN.rsc
    rm -f "build/emulator/qdir-$ARCH/out.rsc" "build/emulator/qdir-$ARCH/OUT.RSC"
    # rcs and rcslow are loaded with their own definition files. cicon.rsc has
    # none: RCS then shows the "Can't find definition (.DFN) file" alert and waits
    # for OK (at 678,455), after which it works the tree types out itself.
    case $1 in
    rcs)    DFN=src/atari/rcs.dfn ;;
    rcslow) DFN=src/atari/rcslow.dfn ;;
    cicon)  DFN= ;;
    esac
    if [ -n "$DFN" ]; then
        export QD_DFN="$DFN"
        ACTS="w50"
    else
        export QD_NODFN=1
        ACTS="w10 c678,455 w50"
    fi
    QD_RSC="tests/golden/$1.rsc" python3 tests/emulator/qd.py "$ARCH" "build/$ARCH/RCS.PRG" "$1-$2" $ACTS
    # vvfat stores the new file in lowercase
    OUT=$(ls "build/emulator/qdir-$ARCH" | grep -i '^out\.rsc$' || true)
    if [ -z "$OUT" ] || [ ! -s "build/emulator/qdir-$ARCH/$OUT" ]; then
        echo "gen-golden: no OUT.RSC written for $1 (run $2)" >&2
        exit 1
    fi
    cp "build/emulator/qdir-$ARCH/$OUT" "build/emulator/golden-$1-$2.rsc"
}

for NAME in rcs rcslow cicon; do
    run $NAME 1
    run $NAME 2
    if ! cmp -s "build/emulator/golden-$NAME-1.rsc" "build/emulator/golden-$NAME-2.rsc"; then
        echo "gen-golden: $NAME: the two runs differ" >&2
        exit 1
    fi
    cp "build/emulator/golden-$NAME-1.rsc" "tests/golden/$NAME.out.rsc"
    echo "gen-golden: $NAME -> tests/golden/$NAME.out.rsc ($(wc -c < "tests/golden/$NAME.out.rsc") bytes)"
done
