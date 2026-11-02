# rcs-cli

A command line program that shows the structure of a GEM resource file
(`.RSC`) and its name file (`.DFN`, `.DEF`, `.RSD`, `.HRD`) through the
resource library (`docs/resource-model.md`), and writes it again with the
library's writer. Stage 2 of #27 (issue #31) added the read-only commands,
stage 3 (#32) the editing commands (`cp`, `mv`, `rm`, `mkdir`, `touch`,
`chmod`, `set`) with write-back and the name file; stage 4 (#33) made icon and
bitmap images files (`cat`, `cp`, `rm`, `touch`, `set`, `stat`: see "Images").

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
| `tests/cli/run.sh` | the test runner; `tests/cli/cases/AREA.cases` the cases, one file per command or topic |
| `tests/cli/mkfix.c` | writes the synthetic fixtures at test time; `mkfix merge A B OUT` (the merge of the library's calls) and `mkfix cicon-equal ...` (deep comparison of two colour icons) for the edit tests |

## Command line

    rcs-cli [OPTIONS] FILE.rsc COMMAND [ARGS...]
    rcs-cli [OPTIONS] FILE.rsc -c 'COMMAND ARGS' -c 'COMMAND ARGS' ...

Options come before `FILE.rsc`; an unknown option is an error.

| Option | Effect |
|---|---|
| `--names FILE`, `--names=FILE` | read the names from FILE; the extension picks the format (`.dfn`, `.def`, `.rsd`, `.hrd`, any case) |
| `--no-names` | read no name file and write none (excludes `--names` and `--names-out`) |
| `--no-guess` | do not guess names and tree kinds for what the name file leaves unnamed (also for the trees and alert strings the edit commands add) |
| `-o OUT.rsc` | write the result of the edit commands to OUT.rsc (and its names to OUT.dfn), leave the input alone; `OUT` must end in `.rsc` (`-o: X: output format not supported yet`) |
| `-n` | dry run: run the commands, check that the result could be written, write nothing (excludes `-o`) |
| `--names-out FILE` | write the names to FILE instead of next to the resource; FILE must end in `.dfn` (`--names-out: X: only .dfn name files can be written yet`); excludes `--no-names` |
| `--selfcheck` | a testing aid: before every edit command the whole model (the encoded resource and the names) is kept, and after a command that failed it is compared; a difference is `internal error: CMD failed but changed the model` |
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
stdout. A note (`rcs-cli: note: ...`, exit status unaffected) tells about what
the command line did not say: indices that shifted, names that were not
carried, a name file that was left as it is. `rcs-cli` writes a file only for
`convert -o` and for the edit commands (see Writing the result); while it
writes, its temporary file is next to it.

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

A node with an image (a `G_ICON` or `G_IMAGE` object, a free image, an entry of
a colour icon) ends with `image: WxH planes=P` (the size of the data and its
planes, whatever the depth), `mask: yes|no` (the image has a mask with data;
`no` for a bitmap, which has none, or a mask never set) and, for a colour
version only, `sel: yes|no` (the version has selected images). A `G_CICON`
object has no image of its own and no such lines.

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

### cat [-v] [--image] [--mask] [--ascii] [-f] PATH

The text of a node, then a newline: the string of a `G_STRING`, `G_BUTTON`
or `G_TITLE`; the text of a TEDINFO (with `-v` also the template and the
validation, a line each; a NULL string is an empty line); the text of an
icon or of a colour icon object; a free string. Any other node:
`rcs-cli: cat: PATH: no text`.

The image of a node instead (see "Images"): always for a `G_IMAGE`, a free
image and an entry of a colour icon, and for the others (`G_ICON`, whose text
`cat` still prints by default) with `--image`, `--mask` or `--ascii`. `--mask`
prints the mask, `--ascii` the text forms (`P1`/`P3`, for diff and tests).
Binary output to a terminal is refused unless `-f` (or `--ascii`):
`rcs-cli: cat: PATH: not writing a binary image to a terminal (use --ascii, or -f to force it)`.

### convert -o OUT.rsc

Writes the resource with `rsc_atari_write` (classic format with the colour
icon extension), with the cell it was read with: the file form by default,
the `--pixels` cell otherwise (both give the same bytes for a file whose
coordinates convert back). The extension must be `.rsc` in any case;
anything else (`.rsh`, `.h`, ...) is `output format not supported yet`.
Without `-o OUT`, or with more arguments: `convert: usage: convert -o
OUT.rsc`. The resource is encoded in memory first, then written to a
temporary file in the directory of OUT (`OUT.XXXXXX`, the mode `fopen` would
give, for the umask) and renamed over OUT once it is written and closed. If
anything fails the temporary file is removed and an OUT that existed is left
as it was, so no partial file is left and a good file is never lost. OUT may
be the input file (the input is read before anything is written).

## Editing

The edit commands change the model in memory; the file is written at the end
(see Writing the result). Every command checks everything before it changes
anything, so a command that fails leaves the model as it was (`--selfcheck`
verifies that in the tests; the exceptions are a failed allocation and a name
list that is full after a copy, which end the run anyway, and nothing is
written then. The undo of a copy or move that fails half way drops the
unreachable objects of the target tree too, as `rsc_tree_compact` does; the
writer would drop them anyway). The commands read what the edit commands before them in the
same `-c` sequence did: `-c 'mkdir X' -c 'ls /X'` works.

The edit commands are `cp`, `mv`, `rm`, `mkdir`, `touch`, `chmod`, `set`. `ls`,
`tree`, `stat`, `cat` take a `FILE.rsc:/PATH` too (below).

### A path in a second file: `FILE.rsc:/PATH`

An argument of the form `FILE.rsc:/PATH` addresses a node in another resource
file; FILE must end in `.rsc` (any case) **and name an existing regular file**,
otherwise the whole argument is a path of the main file (so `/a.rsc:/b` is a
path, and a mistyped file name gives the usual path error, `not found: 'dir' in
/` or `bad path`). The second file is read with the options of the run
(`--pixels`, `--no-names`, `--no-guess`; its name file is looked up next to it,
`--names` is for the main file only), once, when it is first named, and is
never written. It may be the main file itself (then it is the main file, with
the edits made so far; `stat` of the same file decides). At most 8 second files
per run. Where a second file is not allowed (the destination of `cp`, anything
but the source of `cp`, every other command) the error is `CMD: ARG: a second
file is read-only (it is never written)`. A file that cannot be read is the
error of the main file (`rcs-cli: FILE: cannot open: ...`).

### Names given to new nodes

A new name (the destination of a copy or a move, `mkdir`, `touch`, `set
name=`) must be 1 to 32 bytes, with no control character (below 0x20, 0x7f)
and no `/`; it must not be `#` and digits (that is an index: `NAME: a name
that is # and digits is an index, not a name`); for a tree it must not be
`.strings` or `.images` (`that name is reserved`). It must not exist in its
scope, the ASCII letters compared without case: the scope of a tree name is
the trees, of an object name its **tree** (names are looked up in the whole
tree whatever the parent), of a free string or image all of those. The error
is `CMD: the name 'X' is used by KIND #N already` (for an object: `... in that
tree (give the new object another name)`). No command overwrites anything.

`cp` and `mv` treat a destination that exists as a directory to put the node
in, and a destination that does not exist but whose parent does as the new
name (`cp /A/OK /A/OK2` copies OK as OK2, `cp /A/OK /A/BOX` copies it into
BOX). The destination is therefore resolved in two steps: `rcs_resolve` of the
whole path; when that fails because of the last component only, the parent
and the name. Errors in the parent part are the usual path errors.

### cp [-r] SRC DST

Copies a node, with its names. The position is always last (a tree at the end
of `/`, an object the last child, a string or image at the end of its list).

| SRC | DST (existing) | DST (new name, parent exists) |
|---|---|---|
| an object (`-r` when it has child objects; the colour icon with every version is part of the object) | a tree (below its root) or an object with children (as its last child): the copy keeps the name of SRC when it has one | below a tree or a reachable object: the copy is called NAME |
| a tree (`-r` always) | `/`: the copy keeps the name of SRC | at `/`: the copy is called NAME |
| a free string, a free image | `/.strings`, `/.images`: keeps the name | there: called NAME |

Names of the copy. The copy of the object or tree itself gets NAME, or the
first name of SRC; **a guessed name is not copied**: the copy is left unnamed
and named again by the guess (`PANEL8`; with `--no-guess` it stays unnamed). A
name of SRC that exists in the target is an error (`cp: the name 'X' is used by
...`); give the copy a new name. The objects below a copied object keep their
names (all names of one object); one that the target tree has already (always
the case when the tree is the source's) is left out and counted in a note: `rcs-cli: note: cp: 3
names of objects in the copy were not carried: the tree has them already`.
The copy of a whole tree carries all object names (the new tree has none yet).
Between files nothing is shared: strings, records, images and colour icons
are copied with all versions and planes.

Errors (besides the path errors and `cp: usage: cp [-r] SRC DST`,
`cp: unknown option 'X'`):

| Message | When |
|---|---|
| `cp: SRC: is a directory (use -r)` | an object with children, or a tree, without `-r` |
| `cp: SRC: not reachable from the root of its tree` | an object no root reaches |
| `cp: DST: an object can only go into a tree or an object` | an object to `/`, `/.strings`, a string... |
| `cp: DST: exists and is not a directory (it has no children; to put something inside, give a name: DST/NAME)` | an object onto an object without children |
| `cp: DST: not reachable from the root of its tree` | into an unreachable object |
| `cp: DST: a tree can only be copied to /` | a tree anywhere else |
| `cp: DST: a free string can only be copied to /.strings`, `... free image ... /.images` | |
| `cp: DST: already exists` | a tree onto a tree, a string onto a string... |
| `cp: SRC: cannot be copied` | `/`, `/.strings`, `/.images` |
| `cp: cannot copy: the tree would have more than 32767 objects, a link is broken or memory ran out` | |
| `cp: cannot add the names (the name list is full or memory ran out)` | the copy is undone |
| `cp: cannot name the copy: the name list has no room (use --no-guess)` | |

    $ rcs-cli app.rsc -c 'cp -r /DIALOG /ABOUT' -c 'cp /DIALOG/OK /ABOUT/CLOSE'
    $ rcs-cli app.rsc cp -r other.rsc:/MENU1 /          # merge a tree from another file
    $ rcs-cli app.rsc cp other.rsc:/PANEL1/ICON /DIALOG/ICON2   # a colour icon with its versions

`cp -r other.rsc:/#i /` for every tree, `cp other.rsc:/.strings/#k /.strings`
and `cp other.rsc:/.images/#k /.images` for the free items is what the merge
function of the GUI did (the test compares the listing and the file with the
merge made by the library's calls).

Images (`host:FILE`, entries of a colour icon, `--mask`, `--image`) are copied
as described under "Images".

### mv SRC DST

As `cp` for what goes where, then the source is gone. Within one tree an
object is relinked (`rsc_obj_move`; its index and those of its subtree stay,
and so do the names); between trees it is copied and the source is deleted (the
objects get new indices, all names of the subtree move with it, and a name that
the target tree has already is an error: `mv: the name 'X' of an object in SRC
is used in the target tree already`). A tree or free item moves with
`rsc_tree_move` and friends; the names follow (`rsc_names_tree_moved`...).
The source must be in the main file (`mv: SRC: a second file is read-only, so
nothing can be moved out of it (use cp)`).

- `mv /A/OK /A/FINE`: the destination has the same parent as SRC and does not
  exist: a **rename**, the position among the siblings stays.
- `mv /A/OK /A/BOX/OK2`, `mv /A/OK /A/BOX`: relinked as the last child of BOX
  (a rename when the name is new). `mv /A/OK /A` onto its own parent makes it
  the last child of the parent.
- `mv /DIALOG /ABOUT` renames a tree (it stays where it is), `mv /DIALOG /`
  moves it to the end; likewise `/.strings/K` and `/.images/K`.
- Errors beyond those of `cp`: `mv: SRC and DST are the same node`
  (`mv /A/OK /A/ok` is that too: use `set /A/OK name=ok`), `mv: DST: is inside
  SRC` (the destination is in the subtree of SRC, found without recursion),
  `mv: SRC: the root of a tree cannot be moved (it is the tree: mv /TREE
  /NEW)`, `mv: SRC: colour icon entries cannot be moved yet`, `mv: SRC:
  cannot be moved`, `mv: DST: a tree can only go to /` (and the same for
  strings and images), `mv: cannot move SRC (broken links or memory ran out)`,
  `mv: usage: mv SRC DST`.
- After a move between trees `rcs-cli: note: mv: N objects of the source tree
  were renumbered (from #I on)`.

### rm [-r] PATH

Removes an object (with `-r` when it has children), a tree (`-r` always), a
free string or a free image, from the main file. The unreachable objects of
the tree are dropped with an object (`rsc_tree_compact`; the writer would drop
them anyway), the indices behind it shift down and the names follow
(`rsc_names_renumber`, `rsc_names_tree_removed`, `rsc_names_str_removed`...);
the names of what is removed are removed. When any index shifted, a note says
how many and from where: `rcs-cli: note: rm: 18 objects of /DIALOG were
renumbered (from #2 on)`, `... 4 trees were renumbered (from #1 on)`, `... 4
free strings ...`.

Errors: `rm: SRC: is a directory (use -r)`, `rm: PATH: the root of a tree
cannot be removed (remove the tree: rm -r /TREE)`, `rm: PATH: not reachable
from the root of its tree (a write drops it)`, `rm: /: the root cannot be
removed`, `rm: /.strings: cannot be removed (remove its entries)`, `rm: PATH:
the mono image cannot be removed`, `rm: PATH: a second file is
read-only`, `rm: missing PATH`, `rm: too many arguments`, `rm: unknown option
'X'`, `rm: cannot remove PATH ...`.

### mkdir NAME [--type dialog|menu|panel|alert]

A new tree at the end of `/` (`--type` may come before or after NAME; NAME may
have one leading `/`; the default type is `dialog`). The tree gets the name and
the kind (`DIAL`, `MENU`, `PANL`) in the names.

- `dialog`: one `G_BOX` root, spec `0x00021100` (frame 2, colour 1), 32 x 8
  cells at 0,0. `panel`: the same with spec `0x00001100` (no frame).
- `menu`: the AES skeleton: a `G_IBOX` root of 80 x 25 cells; its first child
  the bar (`G_BOX`, 80 x 1) with the `G_IBOX` of the titles (7 x 1 at x 2)
  holding one `G_TITLE` `" Title "`; its second child the screen (`G_IBOX`,
  80 x 24 at y 1) holding one drop-down `G_BOX` (14 x 1) with one `G_STRING`
  `"  Item  "`.
- `alert`: a free string `[1][text][OK]`, named NAME (kind `ALRT`).

With `--pixels` the sizes are multiplied by the cell. Errors: `mkdir: missing
NAME`, `mkdir: too many arguments`, `mkdir: unknown option 'X'`, `mkdir:
--type needs dialog, menu, panel or alert`, `mkdir: bad type 'X' (dialog,
menu, panel or alert)`, the name errors, `mkdir: cannot create the tree
(memory ran out or too many trees)`.

### touch PATH [--type TYPE]

If PATH exists: nothing (and the file is not written). Otherwise a new node named by the last component:

- below a tree or a reachable object: an object, as last child of the parent
  (`/T/NEW` below the root, `/T/BOX/NEW` below BOX, also below an object that
  has no children). TYPE is an AES name (`G_BUTTON`, `button`, any case) or a
  number (decimal or `0x`, up to `0xFFFF`); the default is `G_BOX`. Spec:
  boxes `0x00001100` (`G_BOXCHAR` with the character space), strings the empty
  string, `G_TEXT`/`G_BOXTEXT`/`G_FTEXT`/`G_FBOXTEXT` a TEDINFO with empty text,
  template and validation, font 3 (IBM), colour `0x1100`, `G_ICON` an ICONBLK
  and `G_CICON` a colour icon (no versions) of 32 x 32 pixels with blank
  images, `G_IMAGE` a BITBLK of 16 x 16 pixels (`bi_wb` 2, `bi_hl` 16), blank.
  Size 8 x 1 cells at 0,0 (icons 4 x 2, images 2 x 1).
- `/.strings/NAME`: a free string with empty text; `/.images/NAME`: a free
  image of 16 x 16 pixels, blank.

Below a colour icon object, `N` and `N.sel` (N the planes, without a leading zero) create a colour version, see Images.

Names are looked up in the whole tree, so a name that another object of the
tree has, under another parent, is `touch: the name 'X' is used by object #N
already in that tree ...`. Errors: `touch: PATH: use mkdir to create a tree`
(`/NAME`), `touch: PATH: nothing can be created there` (below a string, a
colour icon entry, ...), `touch: the parent of PATH is not reachable from the
root of its tree`, `touch: bad type 'X' (an AES type name such as G_BUTTON, or
a number)`, `touch: --type needs a type`, `touch: the name list is full (at most 65535 entries)`, `touch: cannot create ...`, `touch: missing PATH`, `touch: too
many arguments`, `touch: unknown option 'X'`.

### chmod [--state] MODE... PATH

Changes the flags of an object, or with `--state` its state. Each MODE is `+NAME[,NAME...]` (set), `-NAME[,NAME...]` (clear) or
`=[NAME[,NAME...]]` (set exactly these: `=` alone clears everything but
`LASTOB`); they apply from the left. Names are the ones `ls` shows, in any
case, with or without the `OF_` / `OS_` prefix: flags `SELECTABLE DEFAULT EXIT
EDITABLE RBUTTON TOUCHEXIT HIDETREE INDIRECT FL3DIND FL3DBAK FL3DACT` (the
last is both 3D bits), states `SELECTED CROSSED CHECKED DISABLED OUTLINED
SHADOWED`. `LASTOB` is refused (`chmod: LASTOB is kept by the library and
cannot be changed`), also in `=`: `=` keeps it. Changing `INDIRECT` of an
object whose type has a pointer spec releases the old spec: `rcs-cli: note:
chmod: INDIRECT changed the kind of the spec: the old spec was released`.

Errors: `chmod: usage: chmod [--state] [+-=]NAME[,NAME...]... PATH`, `chmod:
bad mode 'X' (+NAME, -NAME or =NAME,... )` (a MODE not starting with `+ - =`),
`chmod: bad mode '+' (no names)`, `chmod: bad mode 'X' (empty name)`, `chmod:
unknown flag 'X'` / `unknown state 'X'`, `chmod: PATH: not an object`, `chmod:
PATH: a second file is read-only`.

### set PATH key=value...

Sets properties of an object. All the assignments are checked first, against
the type that the earlier ones leave (`type=G_FTEXT text=Hi` works, `type=G_BOX
text=Hi` is refused), and then applied in order; if one is refused nothing
changes.

| Key | Value | Applies to |
|---|---|---|
| `text` | any string | `G_STRING`/`G_BUTTON`/`G_TITLE`, the TEDINFO kinds (`te_txtlen` follows: `rsc_obj_set_str`; not trimmed to the template), `G_ICON`, `G_CICON` (at most 12 characters); for a free string (`set /.strings/K text=...`) its text, and the alert/other kind of its name follows the new shape |
| `template`, `valid` | any string | TEDINFO kinds (`te_tmplen` follows, `te_txtlen` not) |
| `x` `y` `w` `h` | file form: `3`, `3+4`, `3-2` (cells 0 to 255, remainder `+0..+128` or `-0..-127`); with `--pixels` a whole number of pixels from -127 to 255 * cell + 128 | any object |
| `color` | a hex word, `0x` optional, up to `FFFF` | TEDINFO kinds (`te_color`), `G_BOX`/`G_IBOX`/`G_BOXCHAR` without `INDIRECT` (the low 16 bits of the spec) |
| `thickness` | -128 to 127 | the same (`te_thickness`; bits 23-16 of the spec) |
| `font` | `IBM` (3), `SMALL` (5) or 0 to 32767 | TEDINFO kinds |
| `just` | `left`, `right`, `center` or 0 to 2 | TEDINFO kinds |
| `type` | an AES name or a number, as for `touch` | any object (a change of the kind of spec releases the old spec; a later `text=` makes a new record) |
| `name` | a new name | a tree, object, free string or free image (the same as `mv` within the parent) |
| `iw` `ih` | the width or height of the icon image in pixels, 1 to 32767 | `G_ICON`, `G_CICON`, `G_IMAGE`, free images: see Images |

Errors: `set: usage: set PATH key=value...`, `set: bad assignment 'X'
(key=value)`, `set: unknown key 'K'`, `set: bad value 'K=V'` (with, for the
coordinates, what is expected), `set: bad type 'X' ...`, `set: PATH: KEY does
not apply to a G_BOX object` (the type is the one the earlier assignments
leave, `with INDIRECT` when it has the flag), `set: PATH: the value of KEY is too long (at most 12 characters)` (32766 for a
TEDINFO string), `set: PATH: only name= applies to a tree` (free image;
`... a free string (and text=) or image`), `set: PATH: not an object`, `set:
PATH: a second file is read-only`, `set: too many assignments` (more than 64),
and the name errors.

## Images

Icon and bitmap images are files: PBM/PPM for `cat` and `cp`.

**Which image.** A path addresses an image when it is a `G_ICON` or `G_IMAGE`
object, a free image (`/.images/K`) or an entry of a colour icon (`mono`, `N`,
`N.sel`). `--mask` selects the mask instead of the data (always one plane; a
`G_IMAGE` and a free image have none: `no mask`). A `G_CICON` object itself,
any other object, a missing or non-existent selected image: `no image`. An
entry `N.sel` has its own mask, as in the file. `cp` treats a `G_ICON` or
`G_IMAGE` object or a free image as an image only with `--image` or `--mask`
(otherwise it copies the object, as in stage 3); an entry of a colour icon, on
either side, always makes it an image copy.

**Pixels and the palette.** One plane is black (1) on white (0), as the AES
draws icons: `P4`/`P1`, 1 is black. A colour version has pixel values that
index the standard VDI palette of its depth through the AES pen mapping: 2
planes use the pens 0, 2, 3, 1 (white, red, green, black), 4 planes the pens
0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1; 8 planes pixels 0-14 as
at 4 planes, 15 is pen 255 and 255 is pen 1, the others pens 16-255 of the TT
palette. The colours are the 16 VDI colours (white, black, red, green, blue,
cyan, yellow, magenta, light grey, grey, light red, green, blue, cyan,
yellow, magenta; light grey is 182,182,182 and grey 109,109,109 as 8 bits); see
`docs/resource-model.md`, "Images as pictures", for the tables and their source. It is exported as `P6` (`P3`
with `--ascii`), maxval 255. The 8-plane palette has duplicate colours; an
import chooses the lowest pixel value. Depths other than 2, 4, 8 are kept
untouched by everything but `cat`/`cp` of their data, which say `cmd: P: depth
N is not supported` (their masks, `ls`, `stat`, `rm` and copying the whole
object work).

**cp host:FILE PATH (import).** FILE is PBM/PGM/PPM (`P1` to `P6`, comments
allowed, maxval up to 255; bigger is refused). A mono image or mask thresholds
the darkness at 50% (`--threshold N`, 1 to 100, mono and masks only); a colour
version maps each pixel to the nearest palette colour (`--exact` refuses a
pixel that is not in the palette and names the first; colour only). The size
must equal the icon size (`ib_wicon` x `ib_hicon`, `bi_wb * 8` x `bi_hl` for a
bitmap), else `the image is WxH but PATH is WxH (use --resize to scale it)`;
`--resize` scales by the nearest neighbour (lossy). A version that does not
exist (`/ICON/4`, `/ICON/4.sel`, below a colour icon whose mono icon has the
size of the file) is created: blank data (or the file), mask a copy of the
mono mask (`N.sel`: also a copy of the normal image). Importing the same
picture as there already is changes nothing and writes nothing.

**cp PATH host:FILE (export).** As `cat`, to a new file; an existing file is
`host:F: exists already (use -f to replace it)`, `-f` replaces it. The file is
written to a temporary file next to it and renamed. `--ascii` writes the text
forms. An export does not change the resource; `-n` and `-o` concern the
resource file only, so a dry run still writes the exported file.

**cp PATH1 PATH2 (images).** Copies and converts between images, also of
another file (`FILE.rsc:/PATH`, read only) and also from a mono icon to a
version, a version to another one, a version to its `.sel`, a colour icon's to
another's: mono to N planes makes a set pixel pen 1 (black, value
`(1 << N) - 1`) and a clear pixel 0; N to M planes uses the nearest colour of
the new palette (`rcs-cli: note: cp: 64 pixels changed colour in the
conversion to 8 planes` when some pixel's colour is not the same afterwards);
the mask is copied too (not with `--mask`, which copies the masks only). The
sizes must be equal (`cp: SRC is WxH but DST is WxH (the sizes must be equal)`)
and the images different (`cp: A and B are the same image`). A destination
that does not exist is created like an import. Errors: `no image`, `no mask`,
`depth N is not supported`, `cp: DST: no image there (a new colour version is
named N or N.sel below a colour icon)`, `cp: DST: a colour icon has at most 32
versions`, `cp: --X applies to an import (cp host:FILE PATH) only` (and for
`--ascii`, `-f`: an export), `cp: host:F: not a regular file`, `cannot open:
REASON`, `file too large` (64 MiB), `not a PNM file (P1 to P6) or a damaged
one`, `the file ends before the image does`, `the image is too big (at most
32767 x 32767 pixels and 4194304 in all)`, `maxval above 255 is not
supported`, `the colour #rrggbb of the pixel at X,Y is not in the N-plane
palette`, `cp: --threshold applies to a monochrome image or a mask only`,
`cp: --exact applies to a colour image only`, `cp: --threshold needs a
number`, `cp: bad threshold 'X' (1 to 100)`, `cp: host: needs a file name`,
`cp: one of SRC and DST must be in the resource file`, `cp: host:F: cannot
write: REASON`.

**rm PATH/N, PATH/N.sel.** `N` removes the version with its selected image,
`N.sel` the selected image only; the last version may go (the icon stays a
mono-only colour icon). `mono`: `rm: P: the mono image cannot be removed`.

**touch PATH/N, PATH/N.sel.** A blank version (data colour 0, the mask a copy
of the mono mask; `N.sel`: copies of the data and mask of `N`, which is
created blank when missing). Existing: nothing, nothing written. Errors:
`touch: P: depth N is not supported` (not 2, 4, 8), `a colour icon has at most
32 versions`, `touch: --type does not apply to a colour version`.

**set PATH iw=W ih=H.** Resizes the icon image (not the object `w`/`h`), the
top left part kept and the rest zero: the mono icon and mask and every colour
version (`rsc_cicon_resize`), a `G_ICON` (`rsc_obj_resize_icon`), the bitmap of
a `G_IMAGE` or a free image (the width rounded up to a multiple of 8, with
`rcs-cli: note: set: iw=10 was rounded up to 16 (a bitmap is a whole number
of bytes wide)`). Both keys at once resize once. Errors: `set: bad value
'iw=0'`, `set: P: iw does not apply to a G_BOX object`, `set: P: only name=
applies to a tree`, `set: P: iw and ih cannot be set together with type`,
`set: P: the image would be too big` (a plane of more than 32 MiB),
`set: P: cannot resize the image (memory ran out or it would be too big)`
(all planes together above 32 MiB).

Examples:

    $ rcs-cli app.rsc cat --ascii /ICONS/IC16
    $ rcs-cli app.rsc cp /ICONS/CIC/4 host:cic4.ppm
    $ rcs-cli app.rsc cp --resize host:big.png.ppm /ICONS/CIC/4
    $ rcs-cli app.rsc -c 'touch /ICONS/CIC/8.sel' -c 'cp --mask host:m.pbm /ICONS/CIC/8.sel'

## Writing the result

After the last command, when **all** commands succeeded and **an edit command
ran**, the result is written. `touch` of a node that exists, and a `chmod` or
`set` that changes nothing (found by comparing the encoded resource and the
names before and after), are not edits: they write nothing and make no `.dfn`. A command
line without an edit command never writes (with `-o` it says so: `rcs-cli:
note: no edit command ran: OUT was not written`). The first failing command
ends the run with exit status 1 and nothing is written.

1. `rsc_validate` checks the model (`rcs-cli: FILE: the result is not valid:
   MESSAGE`, which names the tree and object); then `rsc_atari_write` encodes
   it with the cell it was read with (`FILE: cannot write the result:
   MESSAGE`, for instance `classic part exceeds 65535 bytes` for a result the
   format cannot hold). Nothing is written when either fails.
2. `-n`: that is all; `rcs-cli: note: dry run: nothing written (would write
   FILE, N bytes, and the names to NAMES)`.
3. The names (below), then the resource; each atomically (a temporary file
   `FILE.XXXXXX` in the target's directory, renamed over it once written and
   closed; the mode of an existing file is kept, a new file has the mode
   `fopen` gives). The target of the resource is the input file, or OUT with
   `-o`.
   Before anything is written the encoded bytes are read back with
   `rsc_atari_read` (`FILE: the library cannot read the result back (MESSAGE);
   nothing was written`: possible for a colour icon extension that is dense in
   versions, the reader's budget is smaller than the writer's limits), and a
   target that exists must be writable (`FILE: not writable; nothing was
   written`; `-n` does not mind). A symbolic link as the target stays a link:
   the file it points to is replaced (also by `convert -o`); the mode of an
   existing file is kept.
4. If writing the resource fails after the names were written: a name file
   that was new is removed again (`; the new name file was removed`), one that
   replaced a file stays replaced (`; the name file had already been
   replaced`). If the name file cannot be written nothing is written at all
   (`NAMES: cannot write the names: REASON; nothing was written`).

### The names

The names are written as a **DFN** file (the program's own format) when the
resource is written. Entries that `rsc_names_guess` made are not written (they
are a guess, made again when the file is read); everything that came from a
name file or was given by a command is: the names of the edited nodes follow
the edits as described for each command. Entries with a name file of another
format (`.def`, `.rsd`, `.hrd`) are converted. Where:

- with `--names-out FILE` there;
- with `-o OUT.rsc`: `OUT.dfn` (an existing one is replaced; the input's own
  name file is never touched);
- otherwise next to the input: when the names were read from a `.dfn` (found
  next to the input or given with `--names`, any case) that file is
  replaced; when they came from another format or from nowhere,
  `INPUT.dfn` is created, with a note `rcs-cli: note: the names were read from
  X.def, which is left as it is; they are written to X.dfn`, and if `X.dfn`
  exists already and is not the file the names came from the run fails before
  writing (`X.dfn: exists, but the names of X.rsc did not come from it; use
  --names X.dfn, --names-out FILE or --no-names`);
- with `--no-names` none (an existing name file next to the output is left
  as it is, and no longer fits).

A `.dfn` file with no entry is only written when the file is there already
(the old file would be stale otherwise), never newly.

The writer numbers the objects of every tree in preorder, whatever the order
of the array in memory (a `cp`, `touch` or cross-tree `mv` appends to the array
but links the object in the middle; `mv` within a tree keeps the indices).
Before the names are written they are renumbered the same way
(`rsc_tree_preorder`, `rsc_names_renumber`), so a name stays on its object when
the file is read again; the objects no root reaches are not written and their
names go. The `#N` numbers of one run therefore differ from those of the next
run after such edits.

When the `.dfn` that is replaced in place had entries that did not match the
resource (the load-time warning says how many), a second warning says they are
not written: `rcs-cli: warning: 3 entries of X.dfn that did not match the
resource are not written (the file is replaced)`.

The names are checked for the 8-byte limit of the `.dfn` only when the result
is written, so `mkdir LONGERTHAN8` succeeds and the run fails at the end
(`-n` shows it); nothing is written then.

The `.DFN` format holds names of 8 bytes at most and object numbers up to
255. Names that do not fit (`.hrd` names, objects behind 255) are not
dropped silently: the run fails before anything is written with `NAMES: N
names do not fit a .dfn file (names have at most 8 bytes, tree and object
numbers at most 255), the first is 'LONGNAME' (tree T, object O); nothing was
written; use --no-names to write no names, or --names-out FILE to write the
others`. With `--names-out FILE` the names that fit are written and a warning
says how many did not: `rcs-cli: warning: 2 names do not fit a .dfn file and
were not written (the first is ...)`.

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
| `rcs-cli: cat: P: no text`, `cat: P: no image`, `cat: P: no mask`, `cat: P: depth N is not supported` | see Images |
| `rcs-cli: convert: ...` | see convert |
| `rcs-cli: -o needs a file name`, `--names-out needs a FILE`, `-n and -o exclude each other`, `--names-out and --no-names exclude each other` | options of the edit commands |
| `rcs-cli: CMD: ...` | the errors of each edit command, in its section |
| `rcs-cli: FILE: the result is not valid: ...`, `FILE: cannot write the result: ...`, `FILE: cannot write: REASON` | writing the result |

## Limits

- Resource files up to 64 MiB, name files up to 16 MiB; the library's own
  limits and budgets apply (`docs/resource-model.md`).
- The walks of `ls -R` and `tree` keep their own stack on the heap, so a
  tree of 32,767 nested objects costs no C stack; time and memory are linear
  in the number of nodes. `tree` stops indenting below 32 levels.
- Names are at most 32 bytes (at most 128 characters escaped).

## The node layer

`src/cli/rcsnode.h` is written for reuse by the editing commands of stage 3
(which `rcscmd.c` now holds; `rcsnode.*` gained `rcs_node_entry` (the first name
entry of a node), `rcs_obj_parent`, `rcs_child_by_name` (a child lookup that
does not mind whether the parent is a directory), `rcs_name_taken` (a name in
its scope, without case) and `rcs_check_new_name`, and still reads no file and
prints nothing):
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
`build/host/cli-fix`, then runs every case of `tests/cli/cases/*.cases` (one file per area: `ls-tree`,
`stat`, `cat`, `cp`, `mv`, `paths`, `names`, `errors`, ...). A case is a block
that starts with `=== NAME` and has a `cmd` line (the arguments of one
`rcs-cli` invocation, split by the shell after `$ROOT`, the repository root,
`$FIX`, the fixture directory, and `$TMP` are replaced), an optional `rc N`
(the exit status, 0 when absent), and the sections `out` (stdout) and `err`
(stderr, empty when absent); a very long stdout is given as `cksum SUM LENGTH`
instead of `out`. Every line of a section is `|` followed by the exact line,
so output can never look like a marker, and a lone `\` after the last line of
a section says that it does not end in a newline. The runner prints
`ok NAME` or `FAIL NAME` with a diff. After the cases it runs the sweeps of
truncated and mutated files (every outcome must be exit status 0, or 1 with
exactly one `rcs-cli: ` line on stderr) and the `convert` byte comparisons.
The edit cases run with `--selfcheck -n` (a dry run: the note `dry run:
nothing written (would write FILE, N bytes ...)` is part of the expected
stderr, so the size of the result is checked too) and list what they changed
with `-c`. The cases that write files are in `run.sh`: `edit NAME SRC ...`
copies a fixture into a scratch directory, runs the edit with `--selfcheck`,
and compares the re-read listing (`ls -lR`, `tree`, `stat /`) with
the block `=== edit:NAME` of `tests/cli/cases/write-back.cases`; further checks cover that `-o` and `-n` leave the
input byte-identical, that a failing command line writes nothing, the names
(carried, `.def` left alone, `.hrd` names that do not fit, `--names-out`,
`--no-names`), failing writes (no temporary file, the new name file removed
again), round trips that return to the writer's output byte for byte (`mkdir`
then `rm`, `cp` then `rm`, `mv A B` then `mv B A`, with `--pixels` too), the
merge regression, colour icons that arrive intact, and a 32767-level chain and
65535 names. The sweeps of damaged files also run edit commands.
The image cases are in `images.cases`: the fixture `images.rsc` (`mkfix`: icons, a
bitmap and a colour icon with 2, 4 and 8 plane versions whose pixels follow a
formula, so that every `cat --ascii` output was checked by hand against the
palette tables) and host files made by `run.sh` itself with `printf` and `awk`
(`$TMP/pnm`). `run.sh` further checks that exported files equal what `cat`
prints, export-import-export round trips, that an import of the same picture
writes nothing, write-back with a re-read of the pixels, all-or-nothing in `-c`
sequences, hostile host files (wrong magic, huge dimensions, 70,000 digits,
maxval 0 / 256 / 65535, truncated, a fifo, a file above 64 MiB), sweeps over
every prefix and flipped byte of good PNM files, and (with `script(1)`) that
`cat` refuses a binary image on a terminal.

`UPDATE=1 sh tests/cli/run.sh` rewrites the sections and the exit status of
every block (also the `edit:` ones) from the program's output, keeping the
`cmd` lines and the order (a new case needs only its `=== NAME` and `cmd`
lines; comments between blocks are not kept); the expectations are a
specification, so review every change.
