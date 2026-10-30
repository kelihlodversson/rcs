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
`build/arm/SMOKE.PRG`. It needs kconfiglib, as the m68k build does.

## x86-64

Not built yet.
