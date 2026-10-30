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

There is no build system yet.

## License

The source files carry the header "Copyright 1999 by Caldera Thin Clients,
Inc. This software is licensed under the GNU Public License". The file
[LICENSE](LICENSE) is the text of the GNU General Public License, version 3.
