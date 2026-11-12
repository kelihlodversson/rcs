# Toolchains

RCS needs a cross toolchain per target. The pTOS repository has the full,
tested instructions, including installing without sudo:
<https://github.com/kelihlodversson/pTOS/blob/master/.claude/skills/toolchain-install/SKILL.md>

## m68k (Atari TOS, EmuTOS and pTOS)

The `m68k-atari-mintelf-` toolchain. It links a hybrid executable: a GEMDOS PRG
that is also an ELF, so the same file loads on real TOS, EmuTOS and pTOS.

Ubuntu (this is what CI does):

    sudo add-apt-repository -y ppa:vriviere/mintelf
    sudo apt-get update
    sudo apt-get install -y cross-mintelf-essential

macOS: Thorsten Otto's archives from <https://tho-otto.m68k.eu/crossmint.php>
(`binutils-*-mintelf-*-bin-macos`, `gcc-*-mintelf-*-bin-macos` and
`mintlib-*-mintelf-*-dev`), extracted into `/opt/cross-mint`, then:

    export PATH=/opt/cross-mint/bin:$PATH

libcmini's configuration also needs Python 3 and kconfiglib:

    pip3 install kconfiglib

## ARM (pTOS)

    sudo apt-get install -y gcc-arm-none-eabi       # Ubuntu
    brew install gcc-arm-embedded                   # macOS

`make arm` builds libcmini and gemlib for ARM from the submodules and links
`build/arm/RCS.PRG`. It needs kconfiglib, as the m68k build does.

## x32 (pTOS with libaes)

Use a native x86-64 GCC and binutils with x32 support (`gcc -mx32`). A Linux
x32 libc/sysroot is not needed: RCS uses the pinned libcmini headers and
builds its library in `build/x32/libcmini`.

First initialize pTOS's submodules and build pTOS with the native GEMlib
binding split (pTOS#955) with `CONF_WITH_ELF_SHARED=y`, `CONF_WITH_LIBAES=y`, and the
external desktop enabled. Then:

    make -j4 x32 PTOS=/path/to/pTOS

`X32_CC` and `X32_OBJCOPY` override the native compiler and objcopy. The
default `PTOS` is `../pTOS`. The pTOS build must provide `obj/ldso/dyncrt.o`,
`obj/ldso/libptos.so`, `obj/libaes/libaes.so`, `obj/gemlib/libgem.a` and
`obj/x32/libptos-vdi.a`, plus its public headers and generated configuration.

Copy `build/x32/RCS.PRG`, `RCS.RSC`, `RCSLOW.RSC` and the two `.DFN` files
into one folder on the guest drive. Install the pTOS build's
`obj/ldso/runtime/GEMSYS` directory on that drive. Launch RCS from EmuDesk.
The ELF interpreter is `/GEMSYS/LIB/X32/LD.SO`; AES calls resolve to
`libaes.so` and the shared CRT uses `libptos.so`. libcmini supplies the
additional C functions that the small shared runtime does not yet export.

The application uses the same GEMlib headers and AES APIs as the other
targets. The VDI wrappers use GEMlib's portable parameter marshalling. A
small adapter converts its control words to pTOS's typed native VDI layout;
objcopy gives the native archive's `vdi` entry a private name so both
bindings can coexist. Resource disk conversion is the existing `rcsdisk.c`
code, including native structure alignment and big-endian image words.

The GUI regression uses the same pTOS build's desktop test harness (its
`pc-x86_64-desktop-tests-builtin-cli_defconfig`, with shared ELF and libaes
enabled). It requires QEMU, OVMF, mtools and dosfstools:

    make -C /path/to/pTOS CROSS_COMPILE= -j4 x32-desktop-test x32-desktop-window-test
    python3 tests/emulator/x32.py --ptos /path/to/pTOS \
        --ovmf-code /path/to/OVMF_CODE_4M.fd --ovmf-vars /path/to/OVMF_VARS_4M.fd

It opens RCS.RSC, compares a saved copy byte for byte with the ARM golden
output, deletes its menu tree, saves and reloads the result, and checks
clean process exit and the saved resource's tree/object counts. Screenshots,
serial logs and saved resources are kept in `build/emulator/x32`.
