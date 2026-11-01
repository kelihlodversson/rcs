/* RSCMODEL.H	a resource file in memory, in the native runtime form of the
 *		AES: real pointers, no file offsets
 *
 * The model holds
 *  - an ordered list of trees; each tree is a malloc'd array of OBJECT,
 *    linked by index the way the AES links it (ob_next, ob_head, ob_tail),
 *    with the root at index 0;
 *  - an ordered list of free strings and one of free images (BITBLKs);
 *  - the header values the writer needs (rsh_vrsn without the format bits)
 *    and the character cell size the coordinates are in.
 *
 * Ownership.  The model owns everything it points to and frees it exactly
 * once (rsc_free, rsc_tree_delete, rsc_obj_delete, a replacing setter, a
 * type change).  Every record an object points to (TEDINFO, ICONBLK,
 * BITBLK, struct rsc_cicon) belongs to that object alone, and every string
 * and image block is a separate malloc'd block belonging to one record:
 * nothing is shared.  Which member of ob_spec is a pointer the model owns
 * depends only on the low byte of ob_type, through rsc_spec_kind(), and on
 * one flag: with OF_INDIRECT set the spec is an opaque value whatever the
 * type (kind RSC_SPEC_VALUE, never owned); rsc_obj_spec_kind() combines the
 * two and is what every ownership decision of the model uses.
 *
 * Every pointer passed in is copied, never adopted: the caller keeps what
 * it passes.  Every pointer handed out (an OBJECT array, a record, a
 * string) is borrowed: valid until the next call that changes that tree or
 * list, or rsc_free.
 *
 * Direct access.  The caller may read everything and may write, through
 * the pointers it gets, the links (ob_next, ob_head, ob_tail), ob_state,
 * the coordinates and the non-pointer fields of the records, with these
 * exceptions.  It must not write ob_type, ob_spec, ob_flags or any pointer
 * field of a record (use the setters: they keep ownership right;
 * OF_INDIRECT changes the kind of spec and OF_LASTOB is the model's), nor
 * a field that defines the size of an owned block: ib_wicon and ib_hicon
 * of an ICONBLK or of the mono icon of a colour icon, bi_wb and bi_hl of a
 * BITBLK (see Image data), num_planes of a colour version; nor the fields
 * of struct rsc_cicon and struct rsc_cicon_version marked as the model's.
 * The text of a colour icon may be written in place but must keep a NUL
 * in text[12].  The writer checks what it can: a colour version whose
 * num_planes, or a colour icon whose size, does not match the blocks the
 * model allocated is refused (RSC_EARG); for an ICONBLK or a BITBLK the
 * model has no place to record the allocated size (they are the AES
 * structures), and the writer must trust the size fields.  A caller that
 * writes the links should keep them in
 * range or -1: rsc_tree_preorder checks them, and the calls that follow
 * links (rsc_obj_add, rsc_obj_delete, rsc_tree_compact) fail cleanly on
 * broken ones.
 *
 * Errors.  A call that can fail returns -1 (or NULL) and leaves the model
 * as it was: nothing half built, nothing leaked.  Nothing aborts.  A bad
 * tree or object index, or a spec setter called on an object whose type has
 * another kind of spec, is a failure as well.
 *
 * Coordinates.  cell_w and cell_h are the character cell size: 0 means the
 * object coordinates are in file form (low byte in character cells, high
 * byte the pixel remainder), as the file has them; otherwise they are in
 * pixels, as after rsrc_obfix.  The model only records which: the reader
 * and the writer convert.
 *
 * Image data is in the byte order of the machine, as the AES draws it.
 * An icon plane is rsc_plane_bytes(w, h) bytes, a BITBLK bi_wb * bi_hl.
 * The size fields (ib_wicon and ib_hicon of an ICONBLK or of the mono icon
 * of a colour icon, bi_wb and bi_hl of a BITBLK) define how many bytes the
 * owned blocks have, for the colour versions too: the writer and
 * rsc_cicon_add_version compute sizes from them.  They are changed only
 * through the setters (rsc_obj_resize_icon, rsc_obj_resize_bitblk,
 * rsc_frimg_resize, or a new record); a colour icon is resized by setting
 * a new one.  A data pointer (ib_pmask, ib_pdata, bi_pdata, the mono data
 * and mask of a colour icon) may be NULL with a non-zero size: that is a
 * blank image of that size (all zero words), and the writer writes zero
 * words for it.
 *
 * Free strings and free images.  Their lists have no NULL entries (a free
 * image may have NULL data, see above); the reader drops entries of the
 * file that are NULL (0xFFFFFFFF), as the old reader effectively did.
 */
#ifndef RSCMODEL_H
#define RSCMODEL_H

#include <stddef.h>
#include <stdint.h>

#ifndef RSC_HOST

#include <gem.h>

#else /* RSC_HOST */

/* The host test build: the AES structures with the names, field names and
 * member types of gemlib's mt_gem.h, and native pointers. */

#define G_BOX		20
#define G_TEXT		21
#define G_BOXTEXT	22
#define G_IMAGE		23
#define G_USERDEF	24
#define G_PROGDEF	G_USERDEF
#define G_IBOX		25
#define G_BUTTON	26
#define G_BOXCHAR	27
#define G_STRING	28
#define G_FTEXT		29
#define G_FBOXTEXT	30
#define G_ICON		31
#define G_TITLE		32
#define G_CICON		33

#define OF_NONE		0x0000
#define OF_SELECTABLE	0x0001
#define OF_DEFAULT	0x0002
#define OF_EXIT		0x0004
#define OF_EDITABLE	0x0008
#define OF_RBUTTON	0x0010
#define OF_LASTOB	0x0020
#define OF_TOUCHEXIT	0x0040
#define OF_HIDETREE	0x0080
#define OF_INDIRECT	0x0100

#define OS_NORMAL	0x0000
#define OS_SELECTED	0x0001
#define OS_CROSSED	0x0002
#define OS_CHECKED	0x0004
#define OS_DISABLED	0x0008
#define OS_OUTLINED	0x0010
#define OS_SHADOWED	0x0020

typedef struct text_edinfo
{
	char		*te_ptext;
	char		*te_ptmplt;
	char		*te_pvalid;
	short		te_font;
	short		te_fontid;
	short		te_just;
	short		te_color;
	short		te_fontsize;
	short		te_thickness;
	short		te_txtlen;
	short		te_tmplen;
} TEDINFO;

typedef struct icon_block
{
	short		*ib_pmask;
	short		*ib_pdata;
	char		*ib_ptext;
	short		ib_char;
	short		ib_xchar;
	short		ib_ychar;
	short		ib_xicon;
	short		ib_yicon;
	short		ib_wicon;
	short		ib_hicon;
	short		ib_xtext;
	short		ib_ytext;
	short		ib_wtext;
	short		ib_htext;
} ICONBLK;

typedef struct bit_block
{
	short		*bi_pdata;
	short		bi_wb;
	short		bi_hl;
	short		bi_x;
	short		bi_y;
	short		bi_color;
} BITBLK;

typedef struct cicon_data
{
	short		num_planes;
	short		*col_data;
	short		*col_mask;
	short		*sel_data;
	short		*sel_mask;
	struct cicon_data *next_res;
} CICON;

typedef struct cicon_blk
{
	ICONBLK		monoblk;
	CICON		*mainlist;
} CICONBLK;

typedef struct
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned	interiorcol :  4;
	unsigned	fillpattern :  3;
	unsigned	textmode    :  1;
	unsigned	textcol     :  4;
	unsigned	framecol    :  4;
	signed		framesize   :  8;
	unsigned	character   :  8;
#else
	unsigned	character   :  8;
	signed		framesize   :  8;
	unsigned	framecol    :  4;
	unsigned	textcol     :  4;
	unsigned	textmode    :  1;
	unsigned	fillpattern :  3;
	unsigned	interiorcol :  4;
#endif
} BFOBSPEC;

struct user_block;

typedef union obspecptr
{
	long		index;
	union obspecptr	*indirect;
	BFOBSPEC	obspec;
	TEDINFO		*tedinfo;
	BITBLK		*bitblk;
	ICONBLK		*iconblk;
	CICONBLK	*ciconblk;
	struct user_block *userblk;
	char		*free_string;
} OBSPEC;

typedef struct object
{
	short		ob_next;
	short		ob_head;
	short		ob_tail;
	unsigned short	ob_type;
	unsigned short	ob_flags;
	unsigned short	ob_state;
	OBSPEC		ob_spec;
	short		ob_x;
	short		ob_y;
	short		ob_width;
	short		ob_height;
} OBJECT;

#endif /* RSC_HOST */

/* ---- colour icons ------------------------------------------------------ */

/* One colour version of a colour icon.  c is the AES CICON, so a chain of
 * these is the mainlist of a CICONBLK as the AES draws it: c.next_res points
 * to the c of the next version (NULL for the last).  col_data has
 * num_planes planes of rsc_plane_bytes(w, h) bytes, col_mask one plane;
 * sel_data and sel_mask are both NULL (no selected image) or both set, of
 * the same sizes; w and h are those of the mono ICONBLK.  data_bytes is
 * the model's: the size col_data and sel_data were allocated with (planes
 * times the plane size), which the writer compares with num_planes.
 *
 * file_ptr holds the five pointer fields of the version header in the file
 * (col_data, col_mask, sel_data, sel_mask, next_res) as read, opaque; the
 * API sets them to 0.  PLACEHOLDER: the reader (#29 task 2) settles whether
 * they can be canonicalised (and this field dropped) or must be kept. */
struct rsc_cicon_version
{
	CICON		c;		/* must stay the first member */
	uint32_t	file_ptr[5];
	size_t		data_bytes;	/* the model's */
};

/* What a G_CICON object's ob_spec points to (ob_spec.ciconblk is &blk).
 * blk is the AES CICONBLK: blk.monoblk is the mono icon, whose ib_pdata and
 * ib_pmask are owned word arrays and whose ib_ptext always points to text
 * below; blk.mainlist is the chain of colour versions (struct
 * rsc_cicon_version), in file order.  text has the 12 text bytes of the
 * file plus a NUL the model keeps, so ib_ptext is always a C string.
 *
 * file_head holds the three pointer fields of the ICONBLK at the head of the
 * block in the file (ib_pmask, ib_pdata, ib_ptext) as read, opaque; the API
 * sets them to 0.  PLACEHOLDER as for rsc_cicon_version.file_ptr.
 *
 * last, nver and mono_bytes are the model's (read the first two through
 * rsc_cicon_nversions and rsc_cicon_version, never write them): the last
 * version of the chain, so that appending is O(1), the number of versions,
 * and the size the mono data and mask (and every colour mask) were
 * allocated with, which the writer compares with ib_wicon and ib_hicon.
 *
 * The struct points into itself (ib_ptext): it is never copied by value. */
struct rsc_cicon
{
	CICONBLK	blk;		/* must stay the first member */
	char		text[13];
	uint32_t	file_head[3];
	struct rsc_cicon_version *last;	/* the model's: NULL without versions */
	int		nver;		/* the model's */
	size_t		mono_bytes;	/* the model's */
};

/* ---- the kind of spec an object type has: THE ownership rule ------------- */

enum rsc_spec_kind
{
	RSC_SPEC_VALUE,		/* a plain 32-bit value (boxes, G_BOXCHAR, G_USERDEF, any other type) */
	RSC_SPEC_STRING,	/* ob_spec.free_string: G_BUTTON, G_STRING, G_TITLE */
	RSC_SPEC_TEDINFO,	/* ob_spec.tedinfo: G_TEXT, G_BOXTEXT, G_FTEXT, G_FBOXTEXT */
	RSC_SPEC_ICONBLK,	/* ob_spec.iconblk: G_ICON */
	RSC_SPEC_BITBLK,	/* ob_spec.bitblk: G_IMAGE */
	RSC_SPEC_CICON		/* ob_spec.ciconblk (a struct rsc_cicon): G_CICON */
};

/* The kind of spec of an object of type ob_type (only the low byte counts).
 * A pointer kind may be NULL: no record (written as 0xFFFFFFFF). */
enum rsc_spec_kind rsc_spec_kind(unsigned ob_type);
/* The kind of spec of object o: rsc_spec_kind(o->ob_type), but
 * RSC_SPEC_VALUE when o has OF_INDIRECT (as far as the model is concerned
 * that is just a flag: the spec is then an opaque value it never owns).
 * This, not rsc_spec_kind, says which ob_spec member is valid. */
enum rsc_spec_kind rsc_obj_spec_kind(const OBJECT *o);

/* Bytes of one icon plane of w x h pixels, by the AES rule
 * ((w + 15) / 16) * 2 * h; 0 for a negative size. */
size_t rsc_plane_bytes(int w, int h);
/* Bytes of the data of a BITBLK: bi_wb * bi_hl; 0 for a negative size. */
size_t rsc_bitblk_bytes(const BITBLK *bb);

/* ---- the resource ------------------------------------------------------- */

struct rsc;

/* An empty resource (no trees, cell size 0, vrsn 0), or NULL. */
struct rsc *rsc_new(void);
/* Frees r and everything it owns; NULL is allowed. */
void rsc_free(struct rsc *r);

/* rsh_vrsn without the format bits (RSC_VRSN_FORMAT_BITS are cleared on
 * set; the writer sets the ones its output needs). */
#define RSC_VRSN_FORMAT_BITS	0x0004	/* bit 2: the colour icon extension follows */
unsigned rsc_vrsn(const struct rsc *r);
void rsc_set_vrsn(struct rsc *r, unsigned vrsn);

/* The cell size the coordinates are in: 0, 0 for file form.  Setting it
 * converts nothing.  Returns -1 for a negative size or only one of the two 0. */
void rsc_cell(const struct rsc *r, int *cell_w, int *cell_h);
int rsc_set_cell(struct rsc *r, int cell_w, int cell_h);

/* ---- trees ------------------------------------------------------------ */

/* The largest number of objects in a tree (links are shorts). */
#define RSC_MAX_OBJECTS	32767

int rsc_ntrees(const struct rsc *r);

/* Appends a tree of nobj objects (1 <= nobj <= RSC_MAX_OBJECTS) and returns
 * its index, or -1.  Every object is a G_IBOX with spec 0, flags 0 (but
 * OF_LASTOB on the last), state 0, coordinates 0 and all links -1: the root
 * alone, the others unlinked.  An editor asks for 1 and adds objects with
 * rsc_obj_add; the reader asks for the count of the file and writes the
 * links itself. */
int rsc_tree_add(struct rsc *r, int nobj);

/* Deletes tree t and everything it owns; the later trees move down by one.
 * Returns 0 or -1. */
int rsc_tree_delete(struct rsc *r, int t);

/* The object array of tree t and its length (*nobj, may be NULL), or NULL
 * for a bad index.  r is const so that a writer holding a const model can
 * read; writing through the result is subject to "Direct access" above. */
OBJECT *rsc_tree_objects(const struct rsc *r, int t, int *nobj);

/* Errors of rsc_tree_preorder and rsc_tree_compact (all negative). */
enum rsc_walk_error
{
	RSC_WALK_EARG = -1,	/* objs NULL, n < 1 or n > RSC_MAX_OBJECTS, bad tree */
	RSC_WALK_ENOMEM = -2,	/* the scratch memory could not be allocated */
	RSC_WALK_ERANGE = -3,	/* a link of a reachable object is not -1 nor in 0..n-1 */
	RSC_WALK_ESHARED = -4,	/* an object is reached twice: a cycle, or two parents */
	RSC_WALK_ECHAIN = -5	/* a sibling chain is malformed: ob_head and ob_tail
				 * not both -1 or both set, the chain does not end
				 * back at the parent, or ends there at another
				 * object than the parent's ob_tail */
};

/* The objects reachable from the root (index 0) of the n objects at objs,
 * in preorder (an object, then its children in sibling order), following
 * ob_head, ob_next and ob_tail as the AES does: a child whose ob_next is
 * its parent is the last child, and must be the parent's ob_tail.  The
 * root's ob_next is ignored (the file may have anything there; the reader
 * sets it to -1).  Fills order[] (room for n ints; NULL to only check) and
 * returns the number of objects visited, or an enum rsc_walk_error.
 * Unreachable objects are not visited and their links not looked at.  It
 * is iterative, ends after at most 2n steps and reads only objs[0..n-1];
 * it allocates 2n ints of scratch. */
int rsc_tree_preorder(const OBJECT *objs, int n, int *order);

/* Removes the unreachable objects of tree t (left by rsc_obj_delete or by
 * the file), releasing their specs, and renumbers the others, which keep
 * their relative order in the array; the links are rewritten, the root's
 * ob_next becomes -1 and OF_LASTOB is on the new last object.  When map is
 * not NULL (room for the old object count) map[old] is the new index or
 * -1 for a dropped object.  Returns the new object count, or an enum
 * rsc_walk_error, and then the tree is unchanged. */
int rsc_tree_compact(struct rsc *r, int t, int *map);

/* ---- objects ------------------------------------------------------------ */

/* Appends a new object to the array of tree t and makes it the last child
 * of parent, the AES way: the former last child's ob_next (or the parent's
 * ob_head when it had none) points to it, its ob_next points back to the
 * parent, and the parent's ob_tail is it.  The object has type type with an
 * empty spec (0, or NULL for a pointer kind), flags, state and coordinates
 * 0, no children; OF_LASTOB moves to it.  Returns its index, or -1. */
int rsc_obj_add(struct rsc *r, int t, int parent, unsigned type);

/* Unlinks object i from its parent and releases the specs of i and all its
 * descendants.  Their slots stay in the array (indices of the other objects
 * do not change) as unreachable G_IBOX objects with spec 0 and all links
 * -1; they are not reused, and the writer drops them.  An editor that adds
 * and deletes repeatedly can so reach RSC_MAX_OBJECTS: rsc_tree_compact
 * gives the slots back.  The root cannot be deleted (delete the tree).
 * An object with ob_next -1 is unlinked: only its subtree is released.
 * Returns 0, or -1 for a bad index, broken links or a failed allocation
 * (it allocates 2n ints of scratch), and then nothing has changed. */
int rsc_obj_delete(struct rsc *r, int t, int i);

/* Sets ob_type.  When the kind of spec changes (rsc_obj_spec_kind before
 * and after differ) the old spec is released and the spec becomes empty;
 * otherwise it is kept (G_TEXT to G_FBOXTEXT keeps the TEDINFO, G_STRING to
 * G_BUTTON keeps the string, any change with OF_INDIRECT set keeps the
 * value).  Returns 0 or -1 (bad index). */
int rsc_obj_set_type(struct rsc *r, int t, int i, unsigned type);
/* Sets ob_flags, except OF_LASTOB, which the model keeps on the last object
 * of the array.  Setting or clearing OF_INDIRECT changes the kind of spec
 * of a type with a pointer kind: the old spec is then released and becomes
 * empty, as for a type change. */
int rsc_obj_set_flags(struct rsc *r, int t, int i, unsigned flags);
int rsc_obj_set_state(struct rsc *r, int t, int i, unsigned state);
int rsc_obj_set_geom(struct rsc *r, int t, int i, int x, int y, int w, int h);

/* RSC_SPEC_VALUE (rsc_obj_spec_kind): sets / reads the plain spec value.
 * rsc_obj_get_value returns 0, or -1 for a bad index or another kind;
 * rsc_obj_value is the lenient variant: 0 for those. */
int rsc_obj_set_value(struct rsc *r, int t, int i, uint32_t value);
int rsc_obj_get_value(const struct rsc *r, int t, int i, uint32_t *out);
uint32_t rsc_obj_value(const struct rsc *r, int t, int i);

/* The strings of an object, for rsc_obj_set_str. */
enum rsc_str_field
{
	RSC_STR_SPEC,		/* RSC_SPEC_STRING: ob_spec.free_string */
	RSC_STR_TE_TEXT,	/* RSC_SPEC_TEDINFO: te_ptext */
	RSC_STR_TE_TMPLT,	/* RSC_SPEC_TEDINFO: te_ptmplt */
	RSC_STR_TE_VALID,	/* RSC_SPEC_TEDINFO: te_pvalid */
	RSC_STR_IB_TEXT		/* RSC_SPEC_ICONBLK: ib_ptext; RSC_SPEC_CICON: text */
};

/* Replaces one string of an object with a copy of s (NULL: no string),
 * freeing the old one.  The record (TEDINFO, ICONBLK, colour icon) must
 * exist.  For a colour icon s is copied into its text and must have at most
 * 12 characters (NULL clears it).  RSC_STR_TE_TEXT and RSC_STR_TE_TMPLT also
 * set te_txtlen or te_tmplen to strlen(s) + 1, as the old editor did (0
 * for NULL), and fail for a string of 32767 characters or more (the length
 * is a short; the reader refuses such a file).  Returns 0 or -1. */
int rsc_obj_set_str(struct rsc *r, int t, int i, enum rsc_str_field f, const char *s);

/* Replace the record of an object with a copy of *src (NULL src: no
 * record), releasing the old one.  The object's type must have that kind of
 * spec.  The pointer fields of src are sources, copied too (each may be
 * NULL):
 *  - TEDINFO: te_ptext, te_ptmplt, te_pvalid, copied as strings; the other
 *    fields (te_font, te_just, te_color, te_thickness, ...) as they are,
 *    except that te_txtlen and te_tmplen become strlen + 1 of a non-NULL
 *    te_ptext and te_ptmplt (as the reader and rsc_obj_set_str make them;
 *    kept as given for a NULL string); a text or template of 32767
 *    characters or more fails.  The writer writes the stored values;
 *  - ICONBLK: ib_pmask and ib_pdata, rsc_plane_bytes(ib_wicon, ib_hicon)
 *    bytes each; ib_ptext as a string;
 *  - BITBLK: bi_pdata, rsc_bitblk_bytes(src) bytes.
 * Returns 0 or -1. */
int rsc_obj_set_tedinfo(struct rsc *r, int t, int i, const TEDINFO *src);
int rsc_obj_set_iconblk(struct rsc *r, int t, int i, const ICONBLK *src);
int rsc_obj_set_bitblk(struct rsc *r, int t, int i, const BITBLK *src);

/* Change the size of the image of an object (G_ICON: ib_wicon, ib_hicon;
 * G_IMAGE: bi_wb, bi_hl) or of free image k, reallocating its data: the
 * top left part of the old image is kept, the rest is zero; NULL data stays
 * NULL.  The record must exist; sizes 0..32767.  Returns 0 or -1. */
int rsc_obj_resize_icon(struct rsc *r, int t, int i, int w, int h);
int rsc_obj_resize_bitblk(struct rsc *r, int t, int i, int wb, int hl);
int rsc_frimg_resize(struct rsc *r, int k, int wb, int hl);

/* Replaces the colour icon of a G_CICON object with a new one without
 * colour versions: the fields of *mono are copied, its ib_pmask and
 * ib_pdata (rsc_plane_bytes(ib_wicon, ib_hicon) bytes each, may be NULL)
 * too; its ib_ptext is ignored: text (at most 12 characters, NULL for
 * none) becomes the icon text.  NULL mono: no colour icon.  Returns 0 or
 * -1. */
int rsc_obj_set_cicon(struct rsc *r, int t, int i, const ICONBLK *mono, const char *text);
/* The colour icon of object i, or NULL (not a G_CICON, or none). */
struct rsc_cicon *rsc_obj_cicon(const struct rsc *r, int t, int i);

/* The most planes a colour version may have.  The format and the AES allow
 * up to 32 (some tools write 24 and 32 plane versions); the old reader
 * accepted 1..16 only, and so does the model for now. */
#define RSC_MAX_PLANES	16

/* The most colour versions a colour icon may have.  The AES draws the one
 * whose plane count suits the screen, so a sensible icon has one version
 * per plane count, at most RSC_MAX_PLANES (16) of them; 32 leaves room.
 * The cap keeps the cost of a colour icon bounded: rsc_cicon_add_version
 * refuses a version beyond it, the reader refuses a block that has more
 * (RSC_ECICON, before decoding any version) and so does the writer. */
#define RSC_MAX_VERSIONS	32

/* Appends a colour version (planes 1..RSC_MAX_PLANES) to colour icon c, copying the
 * planes (sizes as for struct rsc_cicon_version; col_data and col_mask
 * required, sel_data and sel_mask both or neither).  Returns its index, or
 * -1 (and then c is unchanged), also when c has RSC_MAX_VERSIONS versions
 * already.  It takes constant time. */
int rsc_cicon_add_version(struct rsc_cicon *c, int planes,
			  const short *col_data, const short *col_mask,
			  const short *sel_data, const short *sel_mask);
/* Removes and frees version v of c.  Returns 0 or -1. */
int rsc_cicon_remove_version(struct rsc_cicon *c, int v);
/* The number of versions of c (constant time). */
int rsc_cicon_nversions(const struct rsc_cicon *c);
/* Version v of c, or NULL.  It walks the chain: at most RSC_MAX_VERSIONS
 * steps. */
struct rsc_cicon_version *rsc_cicon_version(const struct rsc_cicon *c, int v);

/* ---- free strings and free images ---------------------------------------- */

int rsc_nfrstr(const struct rsc *r);
/* Free string k, or NULL for a bad index. */
const char *rsc_frstr(const struct rsc *r, int k);
/* Appends a copy of s (not NULL) and returns its index, or -1. */
int rsc_frstr_add(struct rsc *r, const char *s);
/* Replaces free string k with a copy of s (not NULL), freeing the old one. */
int rsc_frstr_set(struct rsc *r, int k, const char *s);
/* Removes free string k; the later ones move down.  Returns 0 or -1. */
int rsc_frstr_delete(struct rsc *r, int k);

int rsc_nfrimg(const struct rsc *r);
/* Free image k, or NULL for a bad index. */
BITBLK *rsc_frimg(const struct rsc *r, int k);
/* Appends a copy of *bb, its data (rsc_bitblk_bytes bytes, may be NULL)
 * included, and returns its index, or -1. */
int rsc_frimg_add(struct rsc *r, const BITBLK *bb);
/* Removes free image k; the later ones move down.  Returns 0 or -1. */
int rsc_frimg_delete(struct rsc *r, int k);

/* ---- allocation, shared with the reader and the writer ------------------- */

/* malloc, realloc, free and a string copy (NULL for NULL) that the test
 * build can make fail and counts (rsc_test_*).  The library allocates and
 * frees only through these, and so must a reader or writer for the blocks
 * it gives the model or the caller (the writer's output buffer is freed by
 * the caller with rsc_mfree).  A size of 0 allocates 1 byte. */
void *rsc_malloc(size_t n);
void *rsc_realloc(void *p, size_t n);
void rsc_mfree(void *p);
char *rsc_strdup(const char *s);

#ifdef RSC_TEST
/* After n more successful allocations every allocation fails, until the
 * next call; n < 0 never fails (the default). */
void rsc_test_fail_alloc_after(long n);
/* Allocations tried (successful or not) since the last
 * rsc_test_fail_alloc_after. */
long rsc_test_alloc_count(void);
/* Blocks allocated through rsc_malloc/rsc_realloc and not yet freed with
 * rsc_mfree: the leak check of the tests (LeakSanitizer is not available
 * everywhere). */
long rsc_test_live_blocks(void);
#endif

#endif
