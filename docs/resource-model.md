# The resource model

For people who edit the sources in `src/rsc/`, and for the programs that will
use the library. Tracked by issue #29; this describes phases 1 and 2 and the editing additions of stage 3 (#32).

## What it is for

The library keeps a resource file in memory in the runtime form of the AES:
real pointers, no file offsets, no addresses held as integers. A file format
is handled only while reading and while writing. The first user will be
`rcs-cli`, later the GUI (the phases of #29). Nothing of the GUI program uses
it yet: the objects are compiled for m68k and ARM but not linked into
`RCS.PRG`, which stays byte-identical. The old arena and `RP()` of
`docs/addresses.md` are still what the program uses.

| File | Content |
|---|---|
| `src/rsc/rscmodel.h`, `rscmodel.c` | the types and the editing API of the model |
| `src/rsc/rscvalidate.c` | `rsc_validate` (declared in `rscfile.h`) |
| `src/rsc/rscfile.h` | the format selector (reserved for phase 3), the error type and codes, `RSC_MAX_CELL` |
| `src/rsc/rscatari_read.c` | the reader of the classic Atari big-endian `.RSC` format |
| `src/rsc/rscatari_write.c` | the writer of that format |
| `src/rsc/rscatari.h` | private to those two: record sizes, header words, error filling (also used by `rscnames.c`) |
| `src/rsc/rscnames.h`, `rscnames.c` | the names of a resource: the `.DFN`, `.DEF`/`.RSD` and `.HRD` readers, the check, the guesser, the lookups |
| `tests/rsc/` | `test_rsc.c`, `test_read.c`, `test_write.c`, `test_names.c`, `test_edit.c` |

The classic format is the 16-bit `RSHDR` with the AES 4 colour icon extension.
The extended format (`RSXHDR`) is not here yet.

## Building and testing

    gmake rsc-test
    gmake rsc-test RSC_SAN=1

builds each test program with the host compiler and runs it. `RSC_SAN=1` adds
AddressSanitizer and UBSan (and stops at the first undefined behaviour).
The `host-tests` job of `.github/workflows/build.yml` runs both, and
`make host-test`, with the system compiler of Ubuntu (gcc, where
LeakSanitizer is on as well) for every pull request. On
the host `rscmodel.h` defines the AES structures itself (`-DRSC_HOST`) with
the field names of gemlib; on m68k and ARM it includes `<gem.h>`. It does not
use `src/host/gem.h`.

- `test_rsc` tests the model: construction through the API, links, type
  changes, strings, the free lists, colour versions, `rsc_tree_preorder` and
  `rsc_tree_compact`, resizing, `OF_INDIRECT`, and every call with failing
  allocations.
- `test_edit` tests the editing calls of the section "Moving, copying,
  walking and validating": every call with its success and error paths, a
  failing allocation at every point of each (the model and the live block
  count must be as before), deep objects (20000 levels) and the object limit,
  `rsc_validate` on read goldens and on 23 hand-corrupted models, the names
  helpers and the `.DFN` writer, and a fuzz: 12 runs of 700 random edits
  (add, delete, move, copy within a model and from a second one, tree copy,
  move, add and delete, compact, free strings and images, with allocation
  failures injected into one edit in five), each compared after every
  step with a naive model kept in the test (a parent and an ordered child
  list per slot, no links) for the indices, links, `OF_LASTOB`, the content of
  every object, the preorder, `rsc_obj_parent`, the free lists and the names;
  every 40 edits the model is written, read back and written again (the
  bytes must be equal) and the names go through a `.DFN` and back.
- `test_read` tests the reader: the files of `tests/golden` checked against
  their bytes, built files, truncations, mutations, malformed files and
  failing allocations.
- `test_write` is the acceptance test: each of `tests/golden/{rcs,rcslow,cicon}.rsc`
  read and written again equals the matching `.out.rsc` byte for byte, with
  cell 8x16 and in file form. It also tests stability (write, read, write),
  the limits and errors, colour icons and a mini-fuzz of edits. With
  `RSC_TEST_EXTERNAL` set to a directory it also reads, writes, reads back
  and rewrites every `*.rsc` in it (real files are not part of the tree, and
  CI does not set it).
- `test_names` tests the names: `src/atari/rcs.dfn` and `rcslow.dfn` with
  their resources, small `.DFN`, `.DEF` and `.HRD` files built with the
  structure of EmuTOS' and TeraDesk's files, truncations, mutation sweeps,
  the budget, the entry limit, failing allocations, the guesser, the lookups,
  `rsc_names_check` and `rsc_names_renumber`. With `RSC_TEST_EXTERNAL` set to
  one or more directories (separated by `:`) it also reads every `*.dfn`,
  `*.def`, `*.rsd` and `*.hrd` in them with the `*.rsc` of the same base
  name, and requires that the check drops nothing.

## The model

A `struct rsc` is a list of trees, a list of free strings, a list of free
images, the header value `vrsn` and the character cell size. A tree is a
malloc'd array of `OBJECT`, linked by index as the AES links it (`ob_next`,
`ob_head`, `ob_tail`), with the root at index 0 and at most `RSC_MAX_OBJECTS`
(32767) objects. Objects that no root reaches stay in the array, so that
indices stay valid; the writer drops them and `rsc_tree_compact` removes them.

Every record is owned by its object: a TEDINFO, ICONBLK or BITBLK per object,
and strings and image data as separate malloc'd blocks. Nothing is shared;
the reader copies a record that the file shares between objects. Which member
of `ob_spec` the model owns depends on the type (`rsc_spec_kind`) and on
`OF_INDIRECT`, which makes the spec an opaque value (`rsc_obj_spec_kind`).
Image data is in the byte order of the machine; an icon plane is
`rsc_plane_bytes(w, h)` = `((w + 15) / 16) * 2 * h` bytes.

The free strings and free images are ordered lists. An alert is a way of
looking at a free string, not a separate kind, and nothing is reordered.

A colour icon is a `struct rsc_cicon` that a `G_CICON` object points to: the
mono ICONBLK with its data, mask and text, and a chain of
`struct rsc_cicon_version` (planes, colour data and mask, optional selected
data and mask), all as word arrays and in file order. Up to `RSC_MAX_PLANES`
(16) planes and up to `RSC_MAX_VERSIONS` (32) versions per icon: the AES uses
one version per plane count, and the cap bounds what a colour icon costs
(the model, the reader and the writer all refuse more). The pointer fields of
the file records are kept as opaque `uint32_t` fields (`file_head`,
`file_ptr`), by decision: kept as read and written as stored, except that the
writer makes `sel_data` agree with the planes and writes `next_res` as 1 or 0.
The icon text is the 12 bytes of the file up to the first NUL (bytes after an
embedded NUL are lost), zero-padded.

The spec of a `G_USERDEF` object and of any object with `OF_INDIRECT` is the
raw 32-bit value of the file, written back unchanged. In a file it is an
offset that loaders such as pTOS's relocate, but the writer lays the file out
anew and writes no USERBLK, so after a write such a value may point to a
stale place: an editor has to set these values itself.

`rsc_obj_add` accepts a parent that the root does not reach (a slot left by
`rsc_obj_delete`, say); the new object is then unreachable too and is
silently not written.

Coordinates: `rsc_cell` is 0, 0 when the objects are in file form (low byte
in character cells, high byte the pixel remainder), otherwise the cell size
and the objects are in pixels, as after `rsrc_obfix`. The model only records
which; the reader and the writer convert. The cell size is at most
`RSC_MAX_CELL` (127). `OF_LASTOB` is managed by the model: it sits on the
last object of the array, and the setters ignore it. `rsc_vrsn` is the
header version without the format bits (bit 2, the colour icon extension, is
set by the writer when it is needed).

## Ownership

Setters copy what they are given and the caller keeps its own memory.
Getters borrow: an `OBJECT` array, a record or a string stays valid until the
next call that changes the same tree or list, or `rsc_free`. A call that can
fail returns -1 or NULL and leaves the model as it was; nothing aborts. The
caller may write the links, `ob_state`, the coordinates and the non-pointer
fields of a record through a borrowed pointer, but not `ob_type`, `ob_spec`,
`ob_flags`, a pointer field, a field that sizes an owned block (`ib_wicon`,
`ib_hicon`, `bi_wb`, `bi_hl`, and `num_planes` of a colour version) or the
fields of `struct rsc_cicon` and `struct rsc_cicon_version` marked as the
model's: those go through the setters. The text of a colour icon must keep
its NUL in `text[12]`. As a safety net the writer refuses (`RSC_EARG`) a
colour version whose `num_planes`, or a colour icon whose size, does not
match the blocks the model allocated. An ICONBLK or BITBLK (AES structures,
with no room to record the allocated size) has to be trusted: if `ib_wicon`,
`ib_hicon`, `bi_wb` or `bi_hl` of a plain one is changed directly so that it
no longer matches the allocated data, the writer reads past that data (a heap
over-read: garbage in the file, or a crash; AddressSanitizer reports a
heap-buffer-overflow READ). Use `rsc_obj_resize_icon`,
`rsc_obj_resize_bitblk` or `rsc_frimg_resize` instead. The writer's output
buffer comes from `rsc_malloc` and is freed with `rsc_mfree`, not `free`.

## Example

This reads a file for an 8x16 cell, changes the first string it finds in
tree 0 and writes the result. It was compiled and run against the library
(`cc -std=gnu99 -Wall -Wextra -DRSC_HOST -Isrc/rsc example.c src/rsc/*.c`)
on `tests/golden/rcs.rsc`.

```c
#include <stdio.h>
#include <stdlib.h>
#include "rscmodel.h"
#include "rscfile.h"

static uint8_t *slurp(const char *name, size_t *len)
{
    FILE *f = fopen(name, "rb");
    uint8_t *buf = NULL;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
        && (buf = malloc(n ? n : 1)) != NULL && fread(buf, 1, n, f) == (size_t)n)
        *len = n;
    else
    {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    struct rsc_error err;
    struct rsc *r;
    uint8_t *in, *out;
    size_t inlen, outlen;
    OBJECT *obj;
    int nobj, i;
    FILE *f;

    if (argc != 3)
        return 2;
    in = slurp(argv[1], &inlen);
    if (in == NULL)
        return 1;
    r = rsc_atari_read(in, inlen, 8, 16, &err);    /* 8x16 pixel cell */
    free(in);
    if (r == NULL)
    {
        fprintf(stderr, "read: %s (code %d, offset %lu)\n", err.msg, err.code,
            (unsigned long)err.offset);
        return 1;
    }
    obj = rsc_tree_objects(r, 0, &nobj);    /* tree 0, root at index 0 */
    for (i = 0; obj != NULL && i < nobj; i++)
        if (rsc_obj_spec_kind(&obj[i]) == RSC_SPEC_STRING)
        {
            printf("tree 0 object %d: \"%s\"\n", i, obj[i].ob_spec.free_string);
            if (rsc_obj_set_str(r, 0, i, RSC_STR_SPEC, "Changed") != 0)
                return 1;
            obj = rsc_tree_objects(r, 0, &nobj);    /* borrowed: fetch again */
            printf("now \"%s\"\n", obj[i].ob_spec.free_string);
            break;
        }
    if (rsc_atari_write(r, 8, 16, &out, &outlen, &err) != 0)
    {
        fprintf(stderr, "write: %s (code %d)\n", err.msg, err.code);
        rsc_free(r);
        return 1;
    }
    f = fopen(argv[2], "wb");
    if (f != NULL)
    {
        fwrite(out, 1, outlen, f);
        fclose(f);
    }
    printf("wrote %lu bytes\n", (unsigned long)outlen);
    rsc_mfree(out);        /* the writer's buffer: rsc_mfree, not free */
    rsc_free(r);
    return 0;
}
```

## Moving, copying, walking and validating

Added for the editing commands of `rcs-cli` (stage 3, #32). Every call here
returns -1 (or a negative code) and leaves the model as it was when it fails,
also for a failed allocation, and none of them is recursive: the work is
bounded by the object count and the scratch is a few arrays of `int` per
object (named below). In every call an object index must be in range and a
tree sound; a tree with broken links makes the calls that need the whole tree
fail (the walks report why: `enum rsc_walk_error`).

**Moving objects.** `rsc_obj_parent(r, t, i)` finds the parent as the AES does
(-1: none, the root or an unlinked slot; -2: a bad index or links that lead
nowhere). `rsc_obj_move(r, t, i, parent, pos)` makes `i`, with its subtree,
child number `pos` of `parent` (`pos` counts the children of `parent` without
`i`; 0 is first, -1 or the count last). Within one parent that reorders the
siblings. Only links change: the indices of `i` and of every other object,
the array and `OF_LASTOB` stay. It refuses the root, an object or a parent
that the root does not reach (a slot left by `rsc_obj_delete`), a parent that
is `i` or below it (a cycle), `pos` out of range and broken links (2n `int` of
scratch).

**Copying.** `rsc_obj_copy_subtree(dst, dt, dparent, pos, src, st, si, map)`
copies object `si` of tree `st` of `src` with everything below it as new
objects at the end of tree `dt` of `dst` and links the copy of `si` as child
number `pos` of `dparent` (the same rules for `pos`). `dst` and `src` may be
the same model, the same tree, even the copy's own parent (a snapshot is
copied). The copies are deep: strings, TEDINFO, ICONBLK, BITBLK and colour
icons with every version, `file_head` and `file_ptr` included (a TEDINFO keeps
its `te_txtlen` and `te_tmplen` as they were, unlike `rsc_obj_set_tedinfo`,
which recomputes them). The spec of an `OF_INDIRECT` object and plain values
are copied as they are. The copies keep the order of the source array, so the
new index of an object is the number of the new objects before it plus the old
object count of `dt`; flags (except `OF_LASTOB`, which moves to the new last
object), state and coordinates are copied. `map` (room for the object count of
`st`, may be NULL) gets the new index of each old object, -1 for the others;
the call returns the new index of `si`. `si` must be linked into its tree
(index 0 or an object with a parent); `dparent` must be reachable. Both
trees' links are checked (the whole of `dt`, the part below `si` of `st`).
It refuses another cell size in the two models (`rsc_cell`: the coordinates
would mean something else) and a result of more than `RSC_MAX_OBJECTS`.
Names are not copied: `rsc_names_copy_objs` does that with the same `map`.

`rsc_tree_copy(dst, pos, src, st, map)` copies a whole tree as tree number
`pos` of `dst` (-1 or `rsc_ntrees(dst)`: last; later trees move up). It copies
the objects the root reaches in the order of the array, so the copy is the
compacted tree and `map` is the map `rsc_tree_compact` would give.

**Trees and free items.** `rsc_tree_move(r, t, pos)`, `rsc_frstr_move(r, k,
pos)` and `rsc_frimg_move(r, k, pos)` move an item to the index `pos`
(-1: last); the items in between shift by one. They allocate nothing. Free
strings and images are named by their index, so the names follow with
`rsc_names_str_moved` and `rsc_names_img_moved` (and `rsc_names_tree_moved`
for trees). `rsc_frimg_set(r, k, bb)` replaces a free image with a copy of
`*bb` (data included) without changing its index.

**Colour versions by index.** `rsc_cicon_insert_version(c, v, ...)` makes the
new version number `v` (0 to the count; -1 last) and `rsc_cicon_set_version(c,
v, ...)` replaces version `v`; the planes and their checks are those of
`rsc_cicon_add_version`, the opaque `file_ptr` of the new version is 0.
`rsc_cicon_resize(c, w, h)` resizes a colour icon to `w` x `h` pixels (0 to
32767) and keeps its versions: every plane (mono data and mask, each colour
plane, mask and the selected ones) keeps its top left part, cropped at the
right and the bottom when it shrinks and padded with zero words when it grows,
exactly as `rsc_obj_resize_icon` does for an ICONBLK; NULL data stays NULL.
`ib_xicon`, `ib_xtext` and the other positions inside the mono ICONBLK are the
caller's to adjust. The result may hold at most `RSC_RESIZE_MAX_BYTES` (32
MiB) of image data (one plane of 32767 x 32767 pixels is 134 MB, and a
colour icon has dozens of planes); a colour icon whose recorded sizes do not
match its blocks (written directly, against the contract) is refused.

**Walking.** `rsc_walk(r, t, cb, ctx)` calls `cb(ctx, obj, depth, parent)` for
every object the root reaches, in preorder, with the depth (0 for the root)
and the parent (-1 for the root); `rsc_walk_from` starts at another object
(depth 0, parent -1 there). The callback returns `RSC_WALK_CONTINUE`,
`RSC_WALK_SKIP` (not the children of this object) or `RSC_WALK_STOP`; it must
not change the model. The links are checked first, so the callback sees a
sound tree; it takes 3n `int` of scratch. The result is the number of calls,
or an `enum rsc_walk_error` (and then there were none).

**Validating.** `rsc_validate(r, &err)` (declared in `rscfile.h`) checks what
the writer relies on and returns 0, or -1 with the code the writer would
report, the tree and object (or the free item) in the message and the index of
that object (or item) in `err.offset`. A tree has 1 to `RSC_MAX_OBJECTS`
objects, sound links, `OF_LASTOB` on the last object only; each object the
root reaches has coordinates that have a file form (with a cell size: -127 to
255 cells + 128), a TEDINFO text and template below 32767 characters, no
negative icon or image size and, for a colour icon, recorded sizes that match
its blocks, a version chain that does not loop and has at most
`RSC_MAX_VERSIONS` versions, a version count and tail that match the chain,
1 to `RSC_MAX_PLANES` planes per version, `text[12]` NUL and `ib_ptext`
pointing at the text; free strings and images are not NULL and have no
negative size; a cell size that is 0, 0 or 1 to `RSC_MAX_CELL` twice. What
it accepts the writer writes, except that the classic part may exceed 65535
bytes, which only a write can tell (`RSC_ETOOBIG`). Objects the root does not
reach are not looked at, as in the writer.

## Errors

`rsc_atari_read` returns NULL and `rsc_atari_write` returns -1 on failure
and fill a `struct rsc_error` (`code`, `offset`, `msg[96]`); `err` may be
NULL. The offset is a position in the file (reader) or in the output
(writer), 0 for a problem of the model.

| Code | Meaning |
|---|---|
| `RSC_ENOMEM` | an allocation failed |
| `RSC_EARG` | bad argument: NULL buffer, bad or mismatched cell size, negative icon size, a version with both low bits set (writer: the reader would take it for `RSXHDR`) |
| `RSC_ETRUNC` | the file ends early (a header field that points past it is `RSC_EOFFSET` or `RSC_ETRUNC`) |
| `RSC_EHEADER` | a version 3 (`RSXHDR`) header, which is not supported yet (the rest is reserved for the `RSXHDR` reader of phase 3) |
| `RSC_EOFFSET`, `RSC_ESTRING` | an offset or count is outside the file, negative record size; a string has no NUL in the file |
| `RSC_ELINK`, `RSC_ECYCLE` | an object link is out of range, a root is listed twice; the links of a tree form a cycle |
| `RSC_ECICON` | a colour icon block or its table is bad |
| `RSC_ETOOBIG` | the data does not fit the format, or the reader's budget is exceeded |

## The reader and untrusted files

Every offset and count is checked against the buffer once, before it is
used, and sums are computed so that they cannot overflow. Every loop is
bounded by the buffer length or by a fixed limit (a colour icon block with
more than `RSC_MAX_VERSIONS` versions is refused from its count, before any
version is looked at), and the work per step is constant or charged to the
budget below, so the time of a read is linear in the length of the file. A
violation fails the read with a code and the position and frees what was
built. `rsh_rssize` is not trusted: for a
plain file the buffer length decides and trailing bytes are ignored.

Records are copied per object, so a small file could name one big record from
many objects. The reader therefore charges every allocation it makes (and the
checking of colour icon blocks) against a budget of `4 * len + 65536` bytes
and fails with `RSC_ETOOBIG` when it is exceeded. `rcs.rsc` uses 1.89 times
its length.

Colour icons are found as the AES and EmuTOS find them (pTOS's
`aes/rsload_legacy.c`): the extension at `rsh_rssize` gives the total length
(at most the buffer length) and the offset of the colour icon table (0 or
`0xFFFFFFFF`: none); the longs after it are ignored (TeraDesk's files have a
colour palette offset there, which is not kept; the writer writes 0). There
is one block per table entry before the `0xFFFFFFFF` terminator, the first
right after the terminator, each next right after the previous, as long as
its own head says; a `G_CICON` object's spec is the index of its block. The
values of the entries are ignored: real files have zeros, the writer (like
pTOS's resource generator) puts the real offsets. Bytes after the last block
are ignored. A table without its terminator, or with more entries than the
file holds blocks, is `RSC_ECICON`; a table with more entries than the rest
of the file could possibly hold is refused before any block is looked at, so
the work stays linear.

Refused: a header version with both low bits set (`rsh_vrsn & 3 == 3`, the
extended `RSXHDR` header of phase 3, detected as XaAES does), as `RSC_EHEADER`
at offset 0 ("version 3 (RSXHDR) resource files are not supported yet"); a
root listed twice in the tree index (the runs of objects of two
trees cannot overlap); negative ICONBLK and BITBLK sizes (`RSC_EOFFSET`); a
cell size above 127 (`RSC_EARG`); a `te_ptext` or template of 32767 characters or more
(`RSC_ETOOBIG`). Objects that a root does not reach are kept without their
specs being decoded, so a bad spec there does not fail the read. The reader
sets the root's `ob_next` to -1, drops NULL entries of the free tables and
sets `te_txtlen` and `te_tmplen` to `strlen + 1`, as the old reader did.
The model keeps them so: `rsc_obj_set_str` sets them to `strlen + 1` of the
new text or template (0 for NULL) and `rsc_obj_set_tedinfo` recomputes them
for non-NULL strings (keeping the given values for NULL ones, as the reader
keeps the file's); both refuse a string of 32767 characters or more. So a
model built through the API is a fixed point: writing, reading and writing
again gives the same bytes.

## The writer

The output is a function of the logical resource, laid out as the old writer
did. After the 36-byte header: strings (one pad byte if the offset is odd),
image data, the free string table, BITBLKs of the trees then of the free
images, the free image table, ICONBLKs, TEDINFOs, OBJECTs and the tree index.
Trees go in index order and objects in preorder, renumbered, with `OF_LASTOB`
only on the last object of each tree. Strings are never shared: a string used
twice is written twice. A NULL string is written as `0xFFFFFFFF`; NULL image
data is a blank image and is written as zero words behind a real pointer.
Image data of an odd number of bytes (a BITBLK whose `bi_wb * bi_hl` is odd;
icon planes are always even) is followed by one zero byte, so that every
block and record after it stays at an even offset.
`te_txtlen` and `te_tmplen` are written as stored; a `te_ptext` or
`te_ptmplt` of 32767 characters or more (which only a caller breaking the
contract can store) is `RSC_ETOOBIG` at the TEDINFO, since the reader would
refuse it.

The classic part may not exceed 65535 bytes (16-bit offsets; since every
section ends even, the largest is 65534): that is
`RSC_ETOOBIG` at the offset of the first item that ends beyond it, not a
silent wrap. Colour icons follow it, after a pad to an even offset, with
32-bit offsets (`rsh_rssize` is then the offset of the extension): the
extension (total length, table offset, 0), the table, then the blocks one
after the other from the byte after the terminator, the last ending the file.
The table entries are the real offsets of the blocks, so a loader that takes
them as offsets (pTOS's portable loader) and one that ignores them (the AES,
EmuTOS, this reader) find the same blocks. The whole file may not exceed
4 GB.

Coordinates in pixel form are converted back so that the reader reads them
as the same value: every value from -127 to `255 * cell + 128` is writable.
Any other value is `RSC_ETOOBIG` at the offset of the object, the message
naming the field, tree and object. The cell size passed to the writer must
be the model's (`rsc_cell`): another one, a mix of 0 and non-0, or a size
above `RSC_MAX_CELL` is `RSC_EARG`.

## Names

The names of the trees, objects, free strings and free images are kept apart
from `struct rsc`, in a `struct rsc_names` (`rscnames.h`): an ordered list of
entries in file order, each with a kind (`RSC_NAME_TREE`, `_OBJ`, `_FRSTR`,
`_FRIMG`), a tree kind (`RSC_TKIND_UNKN`, `_PANL`, `_MENU`, `_DIAL`, and
`_ALRT` for a free string that is an alert: there are no pseudo-trees), the
indices (`tree`, and `obj` for the object or the string or image index), the
name and a flag for guessed entries. A name has 1 to 32 bytes, no control
character (below 0x20, and 0x7f); bytes from 0x80 up are kept as they are
(the Atari character set), and so is the case. The list may hold several
entries for a node and one name for several nodes, as real files do; the
display name of a node is the name of its first entry.

Writing and following edits (stage 3): `rsc_names_write_dfn(n, r, buf, len,
&err)` encodes the list as a `.DFN` file (the format below) into `buf`
(returns the byte count, or -1; `buf` NULL measures; it allocates nothing). It
writes the entries that are not guessed (`rsc_names_guess` makes names an
editor shows but a file need not hold), not the ones of kind UNKNOWN and, when
`r` is given, not those whose node `r` does not have; duplicates and several
names for a node are kept in order, so reading the result gives the same
entries. What the format cannot hold is an error with the offset of the record
(`RSC_ETOOBIG`): a name over 8 bytes, a tree or object index over 255 in an
object entry. The following calls keep a list in step with the edits:
`rsc_names_tree_moved`, `rsc_names_str_moved`, `rsc_names_img_moved` (for
`rsc_tree_move`, `rsc_frstr_move`, `rsc_frimg_move`), `rsc_names_str_removed`
and `rsc_names_img_removed` (for `rsc_frstr_delete`, `rsc_frimg_delete`; for a
tree delete and compaction see `rsc_names_tree_removed` and
`rsc_names_renumber`), `rsc_names_remove` and `rsc_names_remove_obj` (the
entries of one node, nothing shifts; a rename is a remove and an
`rsc_names_add`), `rsc_names_copy_objs` (the entries of the copied objects of
`rsc_obj_copy_subtree` or `rsc_tree_copy`, from the same or another list, with
the call's `map`; all or nothing) and `rsc_names_move_objs` (re-keys the
entries of a subtree that moved from one tree to another as a copy plus a
delete; an object moved within a tree keeps its index, so nothing is needed).
Inserting a tree in the middle has no names call: copy it at the end and use
`rsc_tree_move` with `rsc_names_tree_moved`.

### The formats

| Format | Layout |
|---|---|
| `.DFN` (RCS) | a little-endian count, then records of 14 bytes: value, kind (little-endian words), 10 name bytes of which the first 8 count; kinds 0 UNKN, 1 PANL, 2 MENU, 3 DIAL (a tree), 4 ALRT (a free string), 0x100 an object (value `tree << 8 \| object`), 0x101 a free string, 0x102 a free image. The file is exactly `2 + 14 * count` bytes. |
| `.DEF`, `.RSD` (EmuTOS `tools/erd.c`, rscview) | big-endian records of 16 bytes; record 0 holds the count (bytes 0-1) and flags (2-3, ignored) and is an entry too; `[4]` tree, `[5]` object or index, `[6]` indicator, `[7]` type, `[8..15]` the name. Type 0 is an object with indicator 1 and a tree of kind UNKN with indicator 0; 1 PANL, 2 MENU, 3 DIAL, 4 ALRT, 5 free string, 6 free image. The size is a multiple of 16 and the count the number of records, or one less when the last record is all zero. |
| `.HRD` (Interface, ORCS, ResourceMaster) | an 8-byte header (big-endian version 1, flags, fillers), then records: type, filler, big-endian tree and object words, a NUL-terminated name (16 characters kept); types 0 DIAL, 1 MENU, 2 ALRT, 3 free string, 4 free image (the index in the tree word, the object word 0), 5 object, 6 the end (required; bytes after it are ignored), 7 prefix (ignored). |

A `.DFN` or `.DEF` name is the bytes up to the first NUL within its 8 bytes,
or all 8 (real files have stale bytes after the NUL, and names of 8 bytes
without one). A kind or type the readers do not know becomes an entry of
kind `RSC_NAME_UNKNOWN`, which `rsc_names_check` removes and counts: that
includes `.DFN` kinds 5 and 6, which rscview reads as a free string and a
free image but which `erd.c` and the old RCS never write. A `.DEF` or
`.HRD` record of an unknown type whose name is bad fails the read, where
rscview ignores such a record: deliberate. Bytes 0x80-0x9f are kept in names
(the Atari character set), so a program that prints names must escape them
(the CLI does). The old RCS reader refused a
`.DEF` file without the trailing zero record, so no names loaded for EmuTOS'
`gem.def` and `mform.def`; this reader takes both forms.
`rsc_names_read_by_ext` picks the reader from the extension, in any case
(`.dfn`, `.def`, `.rsd`, `.hrd`; anything else is `RSC_EARG`).

### Untrusted name files

As for the resource reader: every count and offset is checked against the
length, and a reader makes two passes, the first checking everything without
allocating, the second building the list. Errors carry a code, the position
and a message: `RSC_ETRUNC` (the file ends early, or a `.HRD` has no end
record or a name without its NUL), `RSC_EHEADER` (the size or the count does
not fit the format, a `.HRD` version other than 1), `RSC_EOFFSET` (a value
out of range for the format: a `.HRD` object word that should be 0, an object
index of 32767 or more), `RSC_ESTRING` (an empty name, a control character),
`RSC_ETOOBIG` (more than 65535 entries, or the budget), `RSC_ENOMEM`. Every
allocation is charged before it is made, at its size plus 4 bytes, against
`4 * len + 4096` bytes. Since every entry is made of its own bytes of the
file, no file can exceed it today (the densest, an `.HRD` of one-character
names, uses 30 of the 32 bytes each record brings); it keeps memory linear
should that change, and the tests pin the charges and the boundary.

### Checking, guessing, looking up

`rsc_names_check(names, rsc, &dropped)` removes the entries the resource has
no node for (a tree, object, string or image index beyond the model, an
unknown kind) and counts them. `rsc_names_guess(names, rsc)` names what the
files leave unnamed, as the old RCS did: first each unnamed free string with
the shape of an alert (`[`, a character, `]`) becomes `ALERT<k>` (tree kind
ALRT, `k` its 0-based index); then each tree without a tree entry gets a kind
(MENU if `head(head(head(root)))` is a `G_TITLE`; else DIAL if an object the
root reaches has `OF_EXIT`, `OF_DEFAULT`, `OF_TOUCHEXIT` or `OF_EDITABLE`;
else PANL if the root is a `G_BOX`, `G_IBOX` or `G_BOXTEXT`; else UNKN) and
the name `MENU<n>`, `DIALOG<n>`, `PANEL<n>` or `TREE<n>`, n = tree index + 1.
Both count up while the name is already in the list; unlike the old RCS the
comparison ignores the case, because the lookups do, and a number is taken by
a name of any kind: an object or a free image named `DIALOG2` makes the next
guess `DIALOG3`, because a name in the list is one namespace for the lookups
(`test_guess_desk` pins this). Other free strings and
free images stay unnamed. The new entries carry the guessed flag. A tree with
an entry keeps that entry's kind. The list never exceeds `RSC_NAMES_MAX`
(65535) entries: when the guesses do not fit, `rsc_names_guess` adds none and
returns -1 (as it does when out of memory), so a caller that read a full list
must not guess; the CLI reports this as an error and suggests `--no-guess`.

`rsc_names_find_tree`, `_find_obj` (in one tree), `_find_str` and
`_find_img` resolve a path component among `count` nodes: `#` followed by
digits only is always the index (leading zeros allowed, an index at or above
`count` is not found, also when it would overflow), so a node named `#3` is
reachable as `#3` only by its index; `#` alone or followed by anything else
is a name. A name is matched exactly first, then with the ASCII letters
compared without case; matches that name two or more nodes are
`RSC_NAMES_AMBIGUOUS`, none `RSC_NAMES_NOTFOUND` (two entries with the same
name for the same node are not ambiguous). `rsc_names_renumber` applies the
map of `rsc_tree_compact` to the object entries of a tree (entries mapped to
-1 go) and `rsc_names_tree_removed` follows `rsc_tree_delete`; neither
allocates.

The display-name accessors and the `rsc_names_find_*` functions use two
lazily built indexes inside the `struct rsc_names`: entry positions sorted by
(kind, tree, object) and by (kind, scope, name with the ASCII letters
folded), ties by position, so the first entry in file order wins and the
results are exactly those of a scan. An index is one `int` per entry (at most
256 KB each at 65535 entries), built in O(n log n) by a heap sort at the first
lookup after a change, and dropped by every change (add, check, guess,
renumber, tree_removed); a lookup then costs O(log n) plus the entries whose
name is equal without case. The indexes are made after the read and are not
charged to its budget. If one cannot be allocated the lookup scans the list:
the same result, slower.

## Differences from the old code

All deliberate.

- No arena and no 64 KB buffer: the limits apply when writing.
- A record shared by two objects is two records (the old writer read garbage).
- Icon planes follow the AES rule, correct for widths that are not a multiple
  of 16.
- A bad colour icon or a bad offset is an error with a position, not a silent
  loss (the old reader turned a bad colour icon into a box).
- More than 254 colour icons are allowed.
- The colour icon table's entries are not taken as offsets: the blocks are
  found one after the other, as the AES does. The old reader (and pTOS's
  portable loader) took them as offsets, which holds only for files written
  by RCS or pTOS's generator; a file with zero entries, such as TeraDesk's
  `cicons.rsc`, was refused as a whole (`RSC_ECICON`, "bad colour icon block
  offset"), because the old reader checked the block at the offset in table
  entry 0, which is 0.
- `rsh_frstr` and `rsh_frimg` are 0 for empty lists.
- Alert-shaped free strings are not moved to the end.
- Names are read from `.DFN`, `.DEF`/`.RSD` and `.HRD` files into a list of
  their own (see Names), not into the resource; a `.DEF` file without the
  trailing zero record is read; generated names avoid existing ones without
  regard to case.
- NULL image data is written as a real pointer with zeros, where the old
  writer kept NULL pointers.
- In a colour version `next_res` is written 1 when another version follows
  and 0 for the last, and `sel_data` is non-zero exactly when selected planes
  follow.
- Negative sizes are refused, by the reader and by the writer.
- Image data of an odd size is padded to an even size; the old writer packed
  it, which put every following block and record at an odd offset (a 68000
  takes an address error reading such a record).
- Coordinates that the old `unfix_chpos` wrapped are written so that they
  read back, or refused.
- The 4 GB limit of the whole file, and checked 32-bit offsets, are new.
