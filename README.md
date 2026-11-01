# RCS - Resource Construction Set

The Resource Construction Set is the GEM resource editor: the program used to
draw the dialogs, menus, alerts and icons of a GEM application and save them as
a resource file (`.RSC`, with a `.DEF`/`.DFN` file of the object names).

This repository starts from the original source code of RCS, as released by
Caldera under the GNU General Public License (see [License](#license)).

## Plan

The goal is to build RCS again, as a native GEM program that runs on Atari TOS
and on [pTOS](https://github.com/kelihlodversson/pTOS), using:

- [gemlib](https://github.com/freemint/freemint) for the AES and VDI bindings,
  in place of the old `aes.h`/`vdi.h`/`gembind.h` and `rclib` of the original
  build, and
- [libcmini](https://github.com/freemint/libcmini) as the C library,

for Atari TOS and for the three architectures pTOS supports:

| Target | Notes |
| --- | --- |
| m68k | Atari TOS and pTOS on Atari ST, STe, TT and Falcon, and on other m68k machines |
| ARM | pTOS on the Raspberry Pi |
| x86-64 | pTOS on x86-64 |

Nothing of this is built yet. The first milestone is the m68k build for Atari
TOS; the other architectures follow once that works.

## What is in the repository

- `src/` - the C sources and headers of RCS (`rcs*.c`, `rcs*.h`), unchanged
  from the release.
- `src/atari/` - what the Atari ST version adds to it: `prostart.c`, the Pure C
  project file `rcs.prj`, and the resource files of the program itself
  (`rcs.rsc`, `rcslow.rsc` for low resolution, and their `.dfn` files).
- `src/pc/` - the files of the original MS-DOS build: assembler modules,
  linker and build scripts (`bldrcs.bat`, `rcs.lnk`) and its resource files.
  These are not used for the Atari and pTOS builds.

The original build of the Atari version is `src/atari/rcs.prj`, for Pure C
(Turbo C), linked with `RCLIB.LIB`.

## Building

    git submodule update --init
    make m68k      # build/m68k/RCS.PRG, with RCS.RSC, RCSLOW.RSC and the .DFN files
    make arm       # build/arm/RCS.PRG, with the same resource files
    make libs      # gemlib and libcmini for both targets only

On macOS use `gmake`. `docs/toolchains.md` says which toolchains are needed.
The libraries are the `kelihlodversson` forks of gemlib and libcmini, in
`third_party/`. The program loads `RCS.RSC` (`RCSLOW.RSC` on a low resolution
screen), so keep the resource files next to it. The `.DFN` files are only needed
when editing those resource files with RCS. The build is the one of
[TypeShelf](https://github.com/kelihlodversson/TypeShelf), and CI
(`.github/workflows/build.yml`) builds both targets on every pull request.

## Building on the host

    gmake host-test

builds `build/host/roundtrip` with the host compiler and, for each of
`tests/golden/rcs.rsc`, `rcslow.rsc` and `cicon.rsc`, loads the file, writes it
and compares the result with `tests/golden/NAME.out.rsc`. These golden files
are what the Atari program writes after loading the same input. They were made
by `tests/roundtrip/gen-golden.sh` on the pTOS `virt-arm` image in the emulator,
with the program built with `-DRCS_ROUNDTRIP` (it writes `C:\OUT.RSC` straight
after loading). Only ARM is used: the m68k build with that hook panics in
`write_files()` (Line F exception, not understood yet).

The host build compiles the load and save sources (`rcsread.c`, `rcswrite.c`,
`rcsdata.c`, `rcslib.c`, `rcsdisk.c`, `rcscicon.c`) unchanged against a shim in
`src/host/`: an arena for the resource data, and stubs for the GEM and GEMDOS
calls (alerts print to stderr and answer the first button). The files of the
GUI are not part of it. On a 64-bit host addresses of resource data are 32-bit
offsets into the arena; `docs/addresses.md` explains the rules for editing the
sources. `rcs-cli`, a command line tool on top of this, is the next stage.

On m68k and ARM the code is meant to be unchanged by all of this:
`tests/roundtrip/cmp-objs.sh` compares the objects of a baseline build with
those of the current tree and requires the disassembly of every function that
existed before to be identical (and the data sections).

## State of the port

The sources include `<gem.h>` of gemlib and the C headers of libcmini; the
headers of the original GEM release (`portab.h`, `machine.h`, `obdefs.h`,
`gembind.h`, `rclib.h`, ...) are no longer used. What they defined is
replaced in the sources: the DRI types (`WORD`, `LONG`, `VOID`, `LOCAL`, ...)
by standard C, the `LWGET`/`LLSET`/`ADDR` family of memory macros by plain
expressions, and `TRUE`/`FALSE` by `<stdbool.h>`. The old object names
(`NORMAL`, `SELECTED`, ...) come from gemlib with `-D__GEMLIB_OLDNAMES`.

Both targets compile and link `RCS.PRG`. It starts and loads, merges and writes
`RCS.RSC` on m68k (Hatari, pTOS) and on ARM (QEMU `virt`, pTOS); the files it
writes are byte for byte the same on both, and the same as the original. The
port is not finished:

- the files are read and written the same way on every architecture
  (`rcsdisk.c`): resource files are big-endian with records of 34 (ICONBLK) and
  14 (BITBLK) bytes, the `.DFN` files little-endian (the `.DEF`/`.RSD` files of
  EmuTOS and the original RCS are read too, when there is no `.DFN`), the `.INF`
  file big-endian, and in memory the records are the host's own (native byte order
  and padding, aligned). The words of the image data are converted to the words
  of the host when a file is read and written back big-endian, because that is
  what the AES and VDI expect; the strings stay as they are. The editor's images
  are one-plane images in that format and do not depend on the pixel format of
  the screen (`rcsmono.c`). Loading, merging, saving and the icon and image
  editor have been run on m68k and ARM (pTOS under QEMU); the editor has not
  been run on a planar screen. (Writing files from the guest to a QEMU `vvfat`
  directory corrupts a file after the first 8 KB: the second cluster is a copy
  of the first. It is QEMU, not RCS: on a real FAT16 disk image the files are
  right, so test with a disk image.)
- colour icons (`G_CICON`) are shown and edited as their monochrome icon, and
  their colour versions are kept as they are in the file and written back
  (`rcscicon.c`). They cannot be seen or edited, they are dropped if the
  mono icon is resized, and a copy of the icon (cut, paste, duplicate) has
  none. They are written after the extension, with 32-bit offsets, so they
  are not limited by the 16 bits of the file size in the header;
- the code still treats addresses as `long`, which assumes 32-bit addresses
  (m68k, ARM and x32);
- the flags that only ViewMAX (the DOS build) knew (`ESCCANCEL`, `BITBUTTON`,
  `SCROLLER`, `USECOLOURCAT`, `HIGHLIGHTED`, `UNHIGHLIGHTED`) are left out of
  the object flags editor, as they mean something else under TOS;
- the build has about a hundred compiler warnings (hence no `-Wextra`
  or `-Werror` yet).

## Origin and license

RCS was written at Digital Research (Pacific Grove, California) in 1984-85 as
part of GEM; the file headers credit Tim Oren for most of it, with the VDI
bindings converted from code by Lee Lorenzen. It was proprietary software: one
file of the MS-DOS build, `src/pc/gemovlc.c`, still carries the "Copyright (c)
1985 Digital Research Inc." notice. The Atari ST version in `src/atari/` is a
port of this code, to Pure C; the files do not say who made it.

Digital Research's assets went to Novell and then, in 1996, to Caldera. In April
1999 Caldera Thin Clients released the source of GEM, and with it RCS, under
the GNU General Public License (the code lives on as FreeGEM and OpenGEM). That
is the origin of this source: the headers read "Copyright 1999 by Caldera Thin
Clients, Inc. This software is licensed under the GNU Public License. Please see
LICENSE.TXT for further information."

The headers name no version, and section 9 of version 2 then lets a recipient
choose any version of the license ever published. This repository stays with the
original version 2: the `LICENSE` file here is the GNU General Public License,
version 2, as shipped with FreeGEM, which is the version of the 1999 release.
I could not find the original Caldera archive, so the `LICENSE.TXT` it came
with has not been compared with this one.
