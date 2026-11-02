# rcs-cli

A command line program that shows the structure of a GEM resource file
(`.RSC`) and its name file (`.DFN`, `.DEF`, `.RSD`, `.HRD`) through the
resource library (`docs/resource-model.md`), and writes it again with the
library's writer. Stage 2 of #27 (issue #31): read-only commands. The
editing commands are stage 3 (#32), icon images stage 4 (#33).

## Building and testing

    gmake host                  # build/host/rcs-cli
    gmake cli-test              # build it and run tests/cli/run.sh
    gmake cli-test RSC_SAN=1    # the same with build/host/rcs-cli-san (ASan, UBSan)

`rcs-cli` is built with the host compiler from `src/rsc/*.c` (without the
test hooks) and `src/cli/*.c`. It is not part of the m68k and ARM builds.

| File | Content |
|---|---|
| `src/cli/rcsnode.h`, `rcsnode.c` | the node layer: paths, children, the data of `ls -l` and `stat`; no I/O, no printing |
| `src/cli/rcscmd.h`, `rcscmd.c` | the commands and their output formats |
| `src/cli/rcs-cli.c` | options, loading, name file lookup, `-c`, exit status |
| `tests/cli/run.sh` | the test runner; `tests/cli/cases/` the cases |
| `tests/cli/mkfix.c` | writes the synthetic fixtures at test time |

## Command line

    rcs-cli [OPTIONS] FILE.rsc COMMAND [ARGS...]
    rcs-cli [OPTIONS] FILE.rsc -c 'COMMAND ARGS' -c 'COMMAND ARGS' ...

Options come before `FILE.rsc`; an unknown option is an error.

| Option | Effect |
|---|---|
| `--names FILE`, `--names=FILE` | read the names from FILE; the extension picks the format (`.dfn`, `.def`, `.rsd`, `.hrd`, any case) |
| `--no-names` | read no name file (excludes `--names`) |
| `--no-guess` | do not guess names and tree kinds for what the name file leaves unnamed |
| `--pixels`, `--pixels=WxH` | read the file with the character cell WxH (8x16 when not given; each 1 to 127): coordinates are shown in pixels and `convert` uses that cell |
| `--help` | the usage text on stdout, exit status 0 |

The file is read once, at most 64 MiB (`file too large`), with
`rsc_atari_read`. Without `--names` and `--no-names` the name file is looked
up next to it: the same base name (the file name without its last
extension) with `.dfn`, `.DFN`, `.def`, `.DEF`, `.rsd`, `.RSD`, `.hrd`,
`.HRD`, in this order; the first regular file that exists is used. A name
file is at most 16 MiB. A name file that cannot be read, or does not read
as its format, is an error (with the library's message and position). The
names are then checked against the resource (`rsc_names_check`); entries
that do not match are dropped with a warning on stderr that does not change
the exit status:

    rcs-cli: warning: N name entries do not match the resource and were ignored

Unless `--no-guess` is given, `rsc_names_guess` then names the unnamed
trees (`MENU<n>`, `DIALOG<n>`, `PANEL<n>`, `TREE<n>`, with a guessed kind) and
the unnamed alert-shaped free strings (`ALERT<k>`), also without a name file.
The number counter skips a number that any name in the list already takes,
whatever the kind of the node it names (an object named `DIALOG2` makes the
next dialog `DIALOG3`).
A name list holds at most 65535 entries; when the guesses do not fit (a name
file that is already full, and a tree or alert string that is still unnamed)
the run fails with `rcs-cli: FILE: cannot guess the names: the name list has no
room for them (at most 65535 entries) or memory ran out; use --no-guess`
(exit status 1). Use `--no-guess`, or name the remaining trees in the name
file.

With `-c`, every following argument pair must be `-c STRING`. The commands
run in order and stop at the first failure. A string is split into words:
spaces and tabs separate words; single quotes group literally; double
quotes group, and in them a backslash takes the next character literally;
outside quotes a backslash also takes the next character literally. An
unterminated quote, a trailing backslash, an empty string or more than 64
words is an error.

Exit status: 0 on success, 1 on any error. Errors go to stderr, one line
each, starting with `rcs-cli: `; the command's name follows for errors of a
command (`rcs-cli: ls: not found: 'X' in /`), the file name for errors of a
file (`rcs-cli: x.rsc: header truncated at offset 0x0`). Output goes to
stdout. `rcs-cli` writes no file but the one named by `convert -o`.

## Paths

A resource file is a filesystem. A path is `/` followed by components
separated by `/`. There are no relative paths: a path that does not start
with `/` is an error (`bad path 'x': paths start with /`), and so is the
empty path (`bad path '': paths start with /`). One trailing `/` is ignored;
an empty component is an error (`bad path '/a//b': empty component (//)`). A
component is at most 255 bytes, a path less than 512.

| Node | Children | Component |
|---|---|---|
| `/` | the trees in file order, then `.strings` and `.images` (always, also when empty) | |
| a tree | the children of its root object, in sibling order | tree name or `#N` (tree index) |
| an object | the entries of its colour icon (a `G_CICON` object), then its children in sibling order | object name or `#N` (index in the tree's object array; the root is `#0`) |
| `/.strings`, `/.images` | the free strings, the free images, by index | string or image name, or `#N` |
| a colour icon entry | none | `mono`, `<planes>`, `<planes>.sel` |

- At the root `.strings` and `.images` are matched first (also without
  case); a tree with such a name is addressed as `#N`.
- Below a tree, a component is looked up among **all** objects of the tree,
  whatever their depth, reachable or not: `/DIALOG/OK` finds `OK` anywhere
  in `DIALOG`, and `/DIALOG/#7` works for an object no root reaches (`stat`
  then says `reachable: no`). Below an object, a component must name one of
  its colour icon entries or one of its children: `/DIALOG/#3/#5` is object
  5 only when it is a child of object 3.
- `#N` (`#` and digits only, leading zeros allowed) always means the index,
  also when the node has a name; a name of that shape cannot be addressed by
  name.
- Names are matched exactly first, then with the ASCII letters compared
  without case (`/dialog` finds `DIALOG`); two matches for different nodes
  are `ambiguous name 'X' in P (use #N)` (P is the path as typed up to the
  component). No match is `not found: 'X' in P`. A component below a node
  that is not a directory is `not a directory: P`.
- Directories: the root, a tree, `.strings`, `.images`, an object the root
  reaches that has children or a colour icon. Everything else (a leaf
  object, an unreachable object, a free string or image, a colour icon
  entry) is not.
- Colour icon entries: `mono` (the monochrome icon), then per colour version
  in chain order `<planes>` and, when the version has a selected image,
  `<planes>.sel`. When an earlier version has the same plane count the
  entries are `<planes>#<v>` and `<planes>#<v>.sel`, v the 0-based version
  index (`4#2`); `<planes>#<v>` is accepted for every version. Entries are
  matched without case and before the object's children.
- The **canonical path** of a node, which `ls -R`, `tree` and `stat` show,
  is `/T`, `/T/O` (an object directly below its tree, whatever its depth),
  `/T/O/E` (a colour icon entry), `/.strings/K`, `/.images/K`, each
  component the node's name when that name finds the node again (unique in
  its scope, not `#digits`, no `/`, at the root not `.strings` or
  `.images`), else `#N`. Resolving a canonical path gives the node again.

## Text and names in the output

Every byte from the file is written as printable ASCII:

- **quoted text** (`"..."` in `ls -l`, `tree`, `stat`): `\\` for a backslash,
  `\"` for a double quote, `\n`, `\r`, `\t`, `\xnn` (two lower-case hex
  digits) for any other byte below 0x20, for 0x7f and for **every** byte
  from 0x80 up: the Atari character set is not shown as such, and 0x80 to
  0x9f are C1 control codes of a terminal (0x9b starts an escape sequence);
- **names** in columns and paths: the same, and a space as `\x20` (so a
  name is one word); a name is at most 32 bytes;
- **cat**: like quoted text, but `"` is not escaped and no quotes are added.

A character (`G_BOXCHAR`, an icon's letter) is shown as `'A'`, `'\''`,
`'\\'` or `'\xnn'` (below 0x20, 0x7f and up). Error messages that echo a
path, a component, a file name or a `-c` string escape it the same way (as
for cat), at most 64 to 200 bytes of it, then `...` when it is longer. Values such as flags and specs
are written `0xNNNN` with upper-case digits; only escaped bytes use
lower case.

A missing name is `-` in `ls -l`, `tree` and `stat`; short listings show the
component (the name or `#N`).

## Values

- **Coordinates** (`x`, `y`, `w`, `h`): in file form (the default) the low
  byte is in character cells and the high byte the pixel remainder; a high
  byte above 128 counts as remainder - 256, as `rsrc_obfix` has it. Shown as
  `3` (remainder 0), `3+4`, `3-2`. With `--pixels` the pixel value, signed
  decimal. In `ls -l` the geometry is `x,y,w,h`.
- **Type**: the AES name of the low byte of `ob_type` (`G_BOX` 20, `G_TEXT`
  21, `G_BOXTEXT` 22, `G_IMAGE` 23, `G_PROGDEF` 24, `G_IBOX` 25, `G_BUTTON`
  26, `G_BOXCHAR` 27, `G_STRING` 28, `G_FTEXT` 29, `G_FBOXTEXT` 30, `G_ICON`
  31, `G_TITLE` 32, `G_CICON` 33), or `0xNN` for another value; a non-zero
  high byte (the extended type) is appended as `:N` in decimal
  (`G_BUTTON:17`).
- **Flags**: a comma list, in this order: `SELECTABLE` `DEFAULT` `EXIT`
  `EDITABLE` `RBUTTON` `LASTOB` `TOUCHEXIT` `HIDETREE` `INDIRECT` (bits 0 to
  8), then for bits 9 and 10 `FL3DIND` (0x200), `FL3DBAK` (0x400) or
  `FL3DACT` (both), then the other set bits (0xF800) as one `0xNNNN`; `-`
  when no bit is set.
- **State**: `SELECTED` `CROSSED` `CHECKED` `DISABLED` `OUTLINED` `SHADOWED`
  (bits 0 to 5), then the other set bits (0xFFC0) as one `0xNNNN`; `-` for 0.
- **Tree kind**: `DIAL`, `MENU`, `PANL`, `UNKN` from the first tree entry of
  the names, with `~` appended when `rsc_names_guess` made it (`DIAL~`);
  `UNKN` (without `~`) when the tree has no entry (with `--no-guess`), the same
  as an entry of kind UNKN; `stat` tells the two apart by `kind-source`.
- **Box spec** (the 32-bit spec of `G_BOX`, `G_IBOX`, `G_BOXCHAR` without
  `INDIRECT`, from the high bit down): bits 31-24 the character
  (`G_BOXCHAR`), 23-16 the frame thickness (signed: positive inward,
  negative outward), 15-12 the frame colour, 11-8 the text colour, bit 7 the
  text mode (1 opaque, 0 transparent), 6-4 the fill pattern, 3-0 the fill
  colour. A TEDINFO's `te_color` is the low 16 bits of the same layout.

## Commands

### ls [-l] [-R] [PATH]

Lists the children of PATH (default `/`); for a node that is not a
directory, the node itself. Options may be combined (`-lR`); `--` ends them.

Short form: one component per line (the name, or `#N`, or the entry name).

Long form, one line per node, columns separated by spaces; the first three
columns are padded (C `printf` formats given):

| Node | Format | Columns |
|---|---|---|
| tree | `%-6s %-12s %-5s %d` | `#N`, name, kind, the number of objects in the array |
| `.strings`, `.images` | `%-6s %-12s %-5s %d` | `-`, `.strings`, `DIR`, the number of entries |
| object | `%-6s %-12s %-10s %s %s %s` | `#N`, name, type, flags, state, `x,y,w,h` |
| free string | `%-6s %-12s %5d %-5s "%s"` | `#N`, name, length in bytes, `ALERT` when the string has the shape of an alert (`[`, a character, `]`) else `-`, the text: at most its first 60 bytes, then `...` after the closing quote when it is longer |
| free image | `%-6s %-12s %dx%d` | `#N`, name, width in pixels (`bi_wb * 8`) x height (`bi_hl`) |
| colour icon entry | `%-8s %2d %s` | entry name, planes (1 for `mono`), `yes` for a selected image (`4.sel`), else `no` |

With `-R` the listing is in sections, one per directory, in preorder (a
directory, then the directories below it in order): a header line with the
canonical path and `:`, then the listing; an empty line before every header
but the first. A directory with no children has a header and no lines.

    $ rcs-cli tests/golden/cicon.rsc ls -lR
    /:
    #0     PANEL1       PANL~ 2
    -      .strings     DIR   0
    -      .images      DIR   0

    /PANEL1:
    #1     -            G_CICON    LASTOB - 2,1,4,4

    /PANEL1/#1:
    mono      1 no
    4         4 no

    /.strings:

    /.images:

### tree [PATH]

The canonical path of PATH (default `/`), then every node below it, one per
line, drawn with `|-- ` (a node with siblings after it), `` `-- `` (the last
one) and, for each level above, `|   ` (more siblings follow there) or four
spaces. The line of a node is its `ls -l` line with single spaces and
without padding and, for objects, without the geometry:

    tree:     #N NAME KIND OBJECTS
    dir:      - .strings DIR COUNT
    object:   #N NAME TYPE FLAGS STATE
    string:   #N NAME LEN ALERT|- "text"
    image:    #N NAME WxH
    entry:    NAME PLANES yes|no

Nodes deeper than 32 levels below PATH are not indented: their line starts
with the depth in brackets, then the connector (`[33] `-- #33 ...`), so a
32,767-level chain gives lines of bounded length.

    $ rcs-cli tests/golden/cicon.rsc tree
    /
    |-- #0 PANEL1 PANL~ 2
    |   `-- #1 - G_CICON LASTOB -
    |       |-- mono 1 no
    |       `-- 4 4 no
    |-- - .strings DIR 0
    `-- - .images DIR 0

### stat PATH

Every property of one node, one `key: value` per line, in this order.
Text values are quoted (or `-` for a NULL string), names as in columns
(`-` for none).

| Node | Keys |
|---|---|
| root | `path`, `trees`, `strings`, `images`, `cell` (`file`, or `WxH` with `--pixels`), `vrsn` (`0xNNNN`, without the format bits) |
| `.strings`, `.images` | `path`, `entries` |
| tree | `path`, `kind` (`DIAL` `MENU` `PANL` `UNKN`; `UNKN` also for no entry), `kind-source` (`file`, `guessed`, `none` when there is no entry), `name`, `index`, `objects`, `root-children` |
| object | `path`, `tree`, `index`, `name`, `reachable` (`yes`/`no`), `parent` (index, `-` for the root and unreachable objects), `children` (child objects, not colour icon entries), `next`, `head`, `tail` (the raw links), `type` (`0xNNNN` then the type as above), `flags` (`0xNNNN` then the list), `state` (likewise), `x`, `y`, `w`, `h`, then the spec below |
| free string | `path`, `index`, `name`, `length`, `text`, `alert` (`yes`/`no`) |
| free image | `path`, `index`, `name`, `wb`, `hl`, `bi-x`, `bi-y`, `color`, `data-bytes` |
| colour icon entry | `path`, `tree`, `object`, `entry`, `version` (`-` for mono), `planes`, `selected` (this entry is a selected image), `has-selected` (the version has one), `w`, `h`, `data-bytes` (all planes), `mask-bytes` |

The spec of an object, by the kind of spec (`rsc_obj_spec_kind`):

- a plain value (boxes, `G_PROGDEF`, unknown types, any type with
  `INDIRECT`): `spec` (`0xNNNNNNNN`); for `G_BOX`, `G_IBOX` and `G_BOXCHAR`
  without `INDIRECT` also `char` (`G_BOXCHAR` only), `frame`, `frame-color`,
  `text-color`, `text-mode` (`opaque`/`transparent`), `fill-pattern`,
  `fill-color`;
- a string (`G_BUTTON`, `G_STRING`, `G_TITLE`): `text`;
- a TEDINFO: `text`, `template`, `validation`, `font` (with ` (IBM)` for 3,
  ` (SMALL)` for 5), `fontid`, `fontsize`, `just` (with ` (left)`,
  ` (right)`, ` (center)` for 0 to 2), `color` (`0xNNNN`), `frame-color`,
  `text-color`, `text-mode`, `fill-pattern`, `fill-color`, `thickness`,
  `txtlen`, `tmplen`; `tedinfo: -` when there is none;
- an ICONBLK: `text`, `char`, `fg-color`, `bg-color` (bits 15-12 and 11-8 of
  `ib_char`), `char-pos` (`x,y`), `icon` and `text-box` (`x,y,w,h`),
  `data-bytes`, `mask-bytes` (one plane each); `iconblk: -` when none;
- a BITBLK: `wb`, `hl`, `bi-x`, `bi-y`, `color`, `data-bytes`; `bitblk: -`;
- a colour icon: the ICONBLK keys of the mono icon, then `versions` and per
  version `version-V: planes=P selected=yes|no`; `cicon: -` when none.

### cat [-v] PATH

The text of a node, then a newline: the string of a `G_STRING`, `G_BUTTON`
or `G_TITLE`; the text of a TEDINFO (with `-v` also the template and the
validation, a line each; a NULL string is an empty line); the text of an
icon or of a colour icon object; a free string. Any other node:
`rcs-cli: cat: PATH: no text`. A colour icon entry:
`rcs-cli: cat: PATH: not supported yet` (stage 4).

### convert -o OUT.rsc

Writes the resource with `rsc_atari_write` (classic format with the colour
icon extension), with the cell it was read with: the file form by default,
the `--pixels` cell otherwise (both give the same bytes for a file whose
coordinates convert back). The extension must be `.rsc` in any case;
anything else (`.rsh`, `.h`, ...) is `output format not supported yet`.
Without `-o OUT`, or with more arguments: `convert: usage: convert -o
OUT.rsc`. The resource is encoded in memory first, then OUT is written
directly; if writing fails, OUT is removed, so no partial file is left.
OUT may be the input file.

## Errors

| Message | When |
|---|---|
| `rcs-cli: missing FILE.rsc (try --help)`, `missing COMMAND (try --help)` | too few arguments |
| `rcs-cli: unknown option 'X' (try --help)` | an option before FILE that does not exist |
| `rcs-cli: bad --pixels value 'X' (WxH, each 1 to 127)` | |
| `rcs-cli: --names needs a FILE`, `--names and --no-names exclude each other` | |
| `rcs-cli: -c needs a command string, and only -c may follow it` | |
| `rcs-cli: -c 'S': unterminated quote` (`trailing backslash`, `empty command`, `too many words`) | |
| `rcs-cli: F: cannot open: REASON`, `F: not a regular file`, `F: file too large`, `F: cannot read: REASON` | the resource or the name file F |
| `rcs-cli: F: MESSAGE` | the library cannot read F; its message names the position (`header truncated at offset 0x0`) |
| `rcs-cli: unknown command 'X'` | |
| `rcs-cli: CMD: unknown option 'X'`, `CMD: too many arguments`, `CMD: missing PATH` | |
| `rcs-cli: CMD: not found: ...`, `ambiguous name ...`, `not a directory: ...`, `bad path ...` | path errors (above) |
| `rcs-cli: cat: P: no text`, `cat: P: not supported yet` | |
| `rcs-cli: convert: ...` | see convert |

## Limits

- Resource files up to 64 MiB, name files up to 16 MiB; the library's own
  limits and budgets apply (`docs/resource-model.md`).
- The walks of `ls -R` and `tree` keep their own stack on the heap, so a
  tree of 32,767 nested objects costs no C stack; time and memory are linear
  in the number of nodes. `tree` stops indenting below 32 levels.
- Names are at most 32 bytes (at most 128 characters escaped).

## The node layer

`src/cli/rcsnode.h` is written for reuse by the editing commands of stage 3:
a `struct rcs_node` holds indices only (kind, tree, object, index, sel), and
a `struct rcs_view` pairs a `const struct rsc *` with a `const struct
rsc_names *` (or NULL) and caches per tree the parent of every object (from
`rsc_tree_preorder`, built on first use; names are not copied: every name comes from the accessors and lookups
of `rscnames.h`). After an edit, `rcs_view_reset` drops the caches. `rcs_resolve`,
`rcs_children`, `rcs_describe` (a plain struct with borrowed pointers),
`rcs_node_component` and `rcs_node_path` do no I/O and print nothing.

## Tests

`tests/cli/run.sh` (POSIX sh, run from the repository root; `make cli-test`
runs it) builds the fixtures with `build/host/mkfix` into
`build/host/cli-fix`, then runs every `tests/cli/cases/NAME.cmd`: one line,
the arguments of one `rcs-cli` invocation, split by the shell after `$ROOT`
(the repository root) and `$FIX` (the fixture directory) are replaced. It
compares stdout with `NAME.out` (or, for very long outputs, its `cksum`
with `NAME.cksum`), the exit status with `NAME.rc` (0 when the file does
not exist) and stderr with `NAME.err` when that file exists, and prints
`ok NAME` or `FAIL NAME` with a diff. After the cases it runs the sweeps of
truncated and mutated files (every outcome must be exit status 0, or 1 with
exactly one `rcs-cli: ` line on stderr) and the `convert` byte comparisons.
`UPDATE=1 sh tests/cli/run.sh` rewrites the expected files of the cases
from the program's output; they are a specification, so review every change.
