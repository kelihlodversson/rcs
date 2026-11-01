# Addresses of resource data

For people who edit the sources in `src/`. The header is `src/rcsaddr.h`.

## Why `long` and `raddr`

RCS was written for a 32-bit machine and keeps the address of everything it
loads as a `long`: tree and object pointers, the entries of the tables in the
resource data, and the arguments of the library calls. `raddr` is that type
(`unsigned long`), used where a value is an address of RCS data. On m68k and ARM
an `raddr` is a real address. On a 64-bit host it is a 32-bit offset into one
arena, because a pointer does not fit in the 4 bytes the file image gives it.

## RP()

`RP(a)` turns an `raddr` into a pointer you can dereference.

- m68k and ARM: `((void *)(a))`, the identity. Nothing changes in the generated
  code (`tests/roundtrip/cmp-objs.sh` checks it).
- host (`-DRCS_HOST`): `rcs_base + a`, the arena base plus the offset.

## The arena

`rcs_alloc(bytes)` returns an `raddr` of zeroed memory (0 when there is no room)
and `rcs_release(a)` gives it back. On the Atari they are the system allocator;
on the host (`src/host/hostmem.c`) the arena is one reserved 1 GB region, blocks
are 8-aligned, offset 0 is never handed out (it means "none"), and
`rcs_release` does nothing. Allocate RCS data with these, never with `malloc`.
The working buffer of the program itself, `buff_size`, is 64 KB (0x10000), and
that is the limit on what one resource file can hold in memory; the test driver
uses the same size.

## Table entries and fields: rentry, RFIELD, RGET

An address stored inside the resource data (a table entry, a record field)
takes 4 bytes in the file image. The type is `rentry`: `long` on m68k and ARM,
a 4-byte `unsigned int` (2-aligned, as on the Atari) on the host. Use
`RS_ENTRY` (`sizeof(rentry)`) where the code used `sizeof(long)` for the step
between entries. `RFIELD(a)` is the `rentry` at address `a`; `RGET(a)` reads it
as a signed `long`, so that the `-1` meaning "none" is still -1 on the host.

## The rule

A dereference of an RCS address goes through `RP()` exactly once. A host
pointer never does.

Do not apply `RP()` to something that is already a pointer (it would add the
arena base twice on the host), and do not keep a pointer where the file or an
API expects an `raddr`. Compare against 0 or -1 on the `raddr`, not on the
pointer.

## Host pointers and `raddr` in the colour icon store

`rcscicon.c` keeps the colour versions of the icons it has read. In
`struct cicon_kept`, `head` and `versions` are ordinary host pointers (from
`malloc`), as is `block` in `cblk[]`: they are never passed through `RP()`. The
icon itself is RCS data: the `iconblk` argument and the return value of the
functions in `rcscicon.h` are `raddr`, and are dereferenced with `RP()`.

## Files not converted yet

Only the files in `HOST_SRC` in the `Makefile` follow the rule: `rcsread.c`,
`rcswrite.c`, `rcsdata.c`, `rcslib.c`, `rcsdisk.c` and `rcscicon.c`. The GUI
files are not converted: `rcsalert.c`, `rcsedit.c`, `rcsfiles.c`,
`rcsiedit.c`, `rcsifile.c`, `rcsiinit.c`, `rcsimain.c`, `rcsinit.c`,
`rcsintf.c`, `rcsload.c`, `rcsmain.c`, `rcsmenu.c`, `rcsmono.c`,
`rcsobjs.c`, `rcstrees.c` and `rcsvdi.c`. They are not built on the host, and on
m68k and ARM `RP()` is the identity, so converting them changes nothing there; it
would only keep one rule across the source. They are converted when a host tool
needs them. Until then, a function that moves from a GUI file into the host
build has to be converted first.
