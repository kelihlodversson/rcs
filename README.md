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
screen), so keep the resource files next to it. The build is the one of
[TypeShelf](https://github.com/kelihlodversson/TypeShelf), and CI
(`.github/workflows/build.yml`) builds both targets on every pull request.

## State of the port

The sources include `<gem.h>` of gemlib and the C headers of libcmini; the
headers of the original GEM release (`portab.h`, `machine.h`, `obdefs.h`,
`gembind.h`, `rclib.h`, ...) are no longer used. What they defined is
replaced in the sources: the DRI types (`WORD`, `LONG`, `VOID`, `LOCAL`, ...)
by standard C, the `LWGET`/`LLSET`/`ADDR` family of memory macros by plain
expressions, and `TRUE`/`FALSE` by `<stdbool.h>`. The old object names
(`NORMAL`, `SELECTED`, ...) come from gemlib with `-D__GEMLIB_OLDNAMES`.

Both targets compile and link `RCS.PRG`, but it has never been run, and the
port is not finished:

- the code still treats addresses as `long` and reads resource trees through
  fixed byte offsets, which assumes 32-bit addresses and the in-memory layout of
  the resource structures; that has not been checked on ARM;
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
