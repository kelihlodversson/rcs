# rcs-cli: a command line tool for resource files

Issue #27. Done before the colour icon editing work (#14), which builds on it.

## Goal

`rcs-cli` is a host program, built with the native compiler, that reads and
writes resource files through the reader and writer of RCS itself. It reads
and writes the same files RCS does, byte for byte, and lets scripts and tests
dump and edit them.

## Making the shared code 64-bit clean

RCS reads a file into native `OBJECT`/`TEDINFO`/`ICONBLK` memory, patches the
pointers in place, and reaches fields through offsets (`ob_spec` at +12) and
casts of addresses. That only works with 32-bit pointers and 24-byte objects.

- **Accessor layer.** Field access moves behind small accessors, such as
  `obj_spec(o)`, `tedinfo_text(t)` and `icon_data(i)`. There are two
  implementations, chosen at build time:
  - Atari and ARM: the current native-layout code, with the same behaviour.
  - Host: an explicit struct layout that holds a 32-bit offset instead of a
    pointer, in a heap block that load and write index into.
- **Scope.** `rcsread.c`, `rcswrite.c`, `rcscicon.c`, and the parts of
  `rcsdata.c` and `rcslib.c` they use, move over first. GUI-only files
  (`rcsvdi.c`, `rcsimain.c` and so on) stay out of the host build.
- **No GUI types on the host.** `<gem.h>` is not used there; a small
  `rcshost.h` supplies the type names and constants.
- **Safety net.** Before the refactor a round-trip test loads and saves a set
  of resource files (including the colour icon ones and `rcs.rsc`) and
  compares bytes. It runs before and after, so nothing visibly changes in the
  GUI program.

## Path model

A resource file is a filesystem. `file.rsc:/PATH` addresses a node; commands
run as `rcs-cli file.rsc <command> ...`, working from `/`.

- `/` holds the trees. A tree is addressed by its name from the `.DEF` or
  `.DFN` file, or by `#N`, its index.
- Objects are directories of their children, addressed by name or by `#N`,
  the index of the object in its tree. `/DIALOG/#3/#5` is object 5 under
  object 3. Indexes are not stable across edits.
- **`#N` always works.** A node that has a name from a definition file can
  still be addressed as `#N`, and an index-based path and a name-based path
  address the same node.
- Free strings and free images live under `/.strings/` and `/.images/`, so
  they cannot clash with tree names.
- A colour icon is a directory of its variants: `mono`, `4`, `8` and so on,
  each a file. The selected-state image of a variant is `4.sel`. This is the
  structure the colour icon editor uses.

## Commands

| Command | Does |
|---|---|
| `ls [-l] [-R] PATH` | List children. `-l` adds type, flags, state, geometry and spec text. |
| `tree PATH` | Draw the tree. |
| `stat PATH` | Every property of one node. |
| `cat PATH` | Print an object's text, or an icon variant as PPM/PBM. |
| `cp [-r] SRC DST` | Copy a subtree within or between files. Copying from file to file replaces the merge function. When one side is a host file, it exports or imports an icon image. |
| `mv SRC DST` | Rename a node (its definition-file name) or move it to another parent or file. |
| `rm [-r] PATH` | Delete an object, tree or icon variant. |
| `mkdir NAME [--type dialog\|menu\|panel\|alert]` | New tree. |
| `touch PATH [--type G_BUTTON ...]` | New object at the end of its parent. |
| `chmod [+-]FLAG... PATH` | Change object flags, and state with `--state`: `chmod +selectable,+exit /DLG/OK`. |
| `set PATH key=value...` | Everything that is not a flag: `text=`, `x=`, `y=`, `w=`, `h=`, `color=`. |
| `convert [-o OUT.rsc\|.rsh\|.h]` | Write the file in another form through the writer of RCS. |

## Behaviour

- Writes go back to the input file on exit, unless `-o OUT` is given, or `-n`
  (dry run: print what would change). The write goes through the writer of
  RCS, so the output is what RCS would save.
- Several commands per run: `-c 'rm /DLG/#3' -c 'chmod +exit /DLG/OK'`, or a
  script on stdin. The file loads once and writes once.
- Exit status is 0 on success and 1 on an error (usage, missing node,
  unsupported edit, bad file). Errors go to stderr, one line each.
- Names are matched exactly. Wildcards can come later.
- The first version has no colour icon variant handling beyond what the
  loader keeps; `icon` variants as files for colour icons come with the
  colour model (#14, stage 1).

## Stages, one pull request each

1. Round-trip test and accessor layer. No behaviour change; still builds for
   m68k and ARM.
2. Host build (`make host`, producing `build/host/rcs-cli`) with `ls`, `tree`,
   `stat`, `cat` and `convert`.
3. Edits: `cp`, `mv`, `rm`, `mkdir`, `touch`, `chmod`, `set`.
4. Icon variants as files (`cp` to and from images, `rm` a variant): mono
   first, colour once the colour model exists.

## Testing

- The round-trip test of stage 1, and the same files through `rcs-cli
  convert` in stage 2, must give identical bytes.
- Each command has a script test with expected output, run on the host.
- Every refactoring step of the GUI program is checked with the QEMU runs for
  m68k and ARM.

## Risks

The accessor refactor touches the GUI program too. The round-trip test and
the emulator runs after each step are the safeguard.
