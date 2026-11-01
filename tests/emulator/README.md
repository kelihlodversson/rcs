# tests/emulator

Files used by `qd.py` and `tests/roundtrip/gen-golden.sh` to run RCS under
QEMU (pTOS virt-arm and virt-m68k images).

- `qd.py`: the driver. It makes a FAT directory with the program, a launcher and
  the input resource, boots the image and drives the GUI.
- `launch.c`, `launch2.c`: the launcher sources. Each starts `C:\RCS.PRG` with
  `shel_write()` and the command tail `RCS.RSC` (`launch.c`) or `DESK.RSC`
  (`launch2.c`).
- `LAUNCH.PRG`, `LAUNCH2.PRG`: the m68k launchers built from them (Atari ST
  executables, 6974 bytes each).
- `LAUNCH-ARM.PRG`, `LAUNCH2-ARM.PRG`: the ARM launchers (ELF, 70036 bytes each).

The binaries are committed, not built by the Makefile, and the command used to
make them was not recorded.

ARM: the command below, run from the repository root after `make arm` (it
needs `build/arm/lib`), produces files that are byte for byte identical to
`LAUNCH-ARM.PRG` and `LAUNCH2-ARM.PRG` (checked). Repeat with `launch2` for the
other one.

    arm-none-eabi-gcc -std=gnu99 -O2 -ffreestanding -march=armv7-a -mtune=cortex-a7 \
        -marm -mfpu=neon-vfpv4 -mfloat-abi=hard -mword-relocations -D__GEMLIB_OLDNAMES \
        -Ithird_party/gemlib/include/arm -Ithird_party/gemlib -Ithird_party/libcmini/include \
        -c tests/emulator/launch.c -o launch.o
    arm-none-eabi-gcc -march=armv7-a -marm -mfpu=neon-vfpv4 -mfloat-abi=hard -nostdlib \
        -Wl,-q -Wl,-Ttext=0 -Wl,-e_start build/arm/lib/minicrt0.o launch.o \
        -Lbuild/arm/lib -lcmini -lgem -lgcc -lcmini -o LAUNCH-ARM.PRG

m68k (not verified, because the m68k toolchain was unavailable when this was
written): the same way as the m68k `RCS.PRG` in the `Makefile`, that is
`m68k-atari-mintelf-gcc -std=gnu99 -O2 -m68000 -fomit-frame-pointer` with the
include paths of `M68K_CFLAGS`, linked with `-m68000 -nostdlib
build/m68k/lib/crt0.o launch.o -Lthird_party/gemlib/.lib68000 -Lbuild/m68k/lib
-lcmini -lgem -lgcc -lcmini`. The flags the committed `LAUNCH.PRG` was built
with are not known.

`tests/golden/cicon.rsc` (1,076 bytes) is a colour icon resource file made by
the pTOS resource generator (a one-icon-per-size colour icon resource), with the
root object's `ob_next` patched. The generator's command line and the patch were
not recorded beyond that.
