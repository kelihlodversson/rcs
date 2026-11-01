/* RSCATARI_WRITE.C	the writer of the classic Atari .RSC format: the model of
 *			rscmodel.h encoded into a file image
 *
 * The records and the header are those of rscatari_read.c (all numbers
 * big-endian, pointers file offsets, a NULL pointer 0xFFFFFFFF).  The
 * output is a pure function of the logical resource, laid out as the old
 * RCS writer did (src/rcswrite.c), so that a file RCS wrote comes out
 * byte for byte the same:
 *
 *   header	36 bytes; rsh_string is always 36
 *   strings	from offset 36: per tree in index order, per object in
 *		preorder, the spec string of G_BUTTON, G_STRING, G_TITLE, the
 *		ib_ptext of a G_ICON, or the te_ptext, te_ptmplt, te_pvalid of a
 *		TEDINFO; then the free strings; each with its NUL; one 0x00 pad
 *		when that ends at an odd offset
 *   images	in the same order: the data of a BITBLK, the mask then the data
 *		of an ICONBLK; then the data of the free images; no padding
 *   frstr	the free string table (a long per free string), if any
 *   BITBLKs	those of the trees (preorder), then those of the free images
 *   frimg	the free image table (offsets of the free BITBLKs), if any
 *   ICONBLKs	preorder
 *   TEDINFOs	preorder
 *   OBJECTs	tree after tree, each in preorder and renumbered (the links
 *		rewritten to the new indices; the root's ob_next -1); OF_LASTOB
 *		only on the last object of each tree; the high byte of ob_type
 *		cleared for G_ICON, G_IMAGE and G_CICON
 *   trindex	the offset of the root of each tree
 *
 * That is the classic part; its offsets are 16-bit words in the header, so
 * it may not exceed 65535 bytes.  When colour icons are written (G_CICON
 * objects with a struct rsc_cicon) bit 2 of rsh_vrsn is set and the
 * extension follows, with 32-bit offsets: one 0x00 pad to an even offset,
 * which is rsh_rssize; there 12 bytes (the total file length, the offset
 * of the table, 0); the table at rsh_rssize + 12 (a long per colour icon
 * block, then 0xFFFFFFFF); then the blocks, in the layout rscatari_read.c
 * documents: the 38-byte head (file_head, the eleven ICONBLK words of the
 * mono icon, the number of versions), the mono data and mask, the 12 text
 * bytes, then per version its 22-byte header and planes.  Without colour
 * icons rsh_rssize is the file length.
 *
 * The header words object, tedinfo, iconblk, bitblk and imdata are the
 * offset of their section even when it is empty; frstr and frimg are 0
 * when there are no free strings or images.  nbb counts the BITBLKs of the
 * trees and of the free images; nib does not count colour icons.
 *
 * It works in two passes over the same walk: the first only measures the
 * sections (no byte is written, the model is never modified), then the
 * offsets of the sections are laid out and the whole file is allocated at
 * once and filled in by the second.
 *
 * Decisions:
 *  - Only the objects a root reaches are written, in the preorder of
 *    rsc_tree_preorder; the specs of the others are not looked at.  A
 *    broken tree fails the write: RSC_ECYCLE for an object reached twice,
 *    RSC_ELINK for a link out of range or a malformed sibling chain.
 *  - Strings are never shared: a string used twice is written twice.  A
 *    NULL string is written as a NULL pointer.  A record pointer that is
 *    NULL (an object without TEDINFO, ICONBLK, BITBLK or colour icon) is a
 *    NULL spec.
 *  - Image data that is NULL is a blank image of its size: it is written as
 *    zero words with a real pointer, never as a NULL pointer.  An icon
 *    plane is rsc_plane_bytes(w, h) bytes (the AES rule, unlike the old
 *    writer for widths that are not a multiple of 16), a BITBLK
 *    bi_wb * bi_hl; an odd last byte is copied as it is, as the reader
 *    does.
 *  - A negative icon or image size is refused (RSC_EARG): the reader would
 *    refuse the file.
 *  - te_txtlen and te_tmplen are written as the model has them (the reader
 *    recomputes them from the strings).
 *  - Coordinates: with a cell size of 0, 0 they are written as they are
 *    (file form); otherwise converted as unfix_chpos does: the low byte is
 *    pix / cell (C division), the high byte the remainder.  The cell size
 *    must be the one the model is in (rsc_cell): another one cannot be
 *    meant, and is refused (RSC_EARG), as are a mix of 0 and non-0 and
 *    sizes above RSC_MAX_CELL (rscfile.h), as for the reader.  A pixel
 *    value the file form cannot hold (negative, or above 256 * cell - 1:
 *    255 cells plus a remainder below the cell) is refused with
 *    RSC_ETOOBIG at the offset of the object, not wrapped.  (The old
 *    unfix_chpos wrapped them; the goldens have none.  Small negative
 *    values above -cell would survive the old conversion, but no editor
 *    produces them and the strict rule is the simple one.)
 *  - Colour icons: a G_CICON object's spec is the index of its icon in
 *    the order written (trees in order, objects in preorder); one without
 *    a struct rsc_cicon gets a NULL spec.  The opaque file_head and
 *    file_ptr fields are written as the model has them, except two
 *    fields of a version header that the file format gives a meaning:
 *    sel_data must be non-zero exactly when selected planes follow (the
 *    reader decides by it) and next_res is made non-zero exactly when
 *    another version follows (pTOS's loader checks that): a stored value
 *    that already says so is kept, otherwise 1 or 0 is written.  The 12
 *    text bytes are written as stored.  A version with NULL planes writes
 *    zero words; selected planes are written when sel_data or sel_mask is
 *    set.  A version with a number of planes outside 1..RSC_MAX_PLANES, or
 *    a version chain that loops, is RSC_ECICON.  There is no limit on the
 *    number of colour icons but the 32-bit offsets.
 *  - Limits: the classic part may not exceed 65535 bytes (with colour icons
 *    that includes the pad before the extension): RSC_ETOOBIG at the
 *    offset of the first item (string, image, record, table entry or pad)
 *    that ends beyond it.  The whole file may not exceed 4 GB, and every
 *    size is computed with overflow checks (RSC_ETOOBIG).  The counts of
 *    the header cannot exceed 65535 when the classic part fits: every
 *    counted item takes at least one byte of it.
 *  - Errors that concern the model rather than a position in the output
 *    (broken trees, negative sizes, bad colour versions) have offset 0 and
 *    name the tree and object, or the free image, in the message.
 */
#include <string.h>
#include "rscfile.h"

#define FILE_NULL	0xffffffffUL

#define HDR_SIZE	36
#define OBJ_SIZE	24
#define TED_SIZE	28
#define IB_SIZE		34
#define BB_SIZE		14
#define CBLK_SIZE	38	/* the ICONBLK and the count of colour versions */
#define CTEXT_SIZE	12
#define CICON_SIZE	22	/* the header of a colour version */
#define EXT_SIZE	12	/* the extension at rsh_rssize */

/* The largest classic part: its offsets are 16-bit words. */
#define CLASSIC_MAX	65535UL

/* positions of the header words */
enum
{
	H_VRSN, H_OBJECT, H_TEDINFO, H_ICONBLK, H_BITBLK, H_FRSTR, H_STRING,
	H_IMDATA, H_FRIMG, H_TRINDEX, H_NOBS, H_NTREE, H_NTED, H_NIB, H_NBB,
	H_NSTRING, H_NIMAGES, H_RSSIZE, H_WORDS
};

/* The sections of the output in the order they are laid out: the classic
 * part, then the colour icon table and blocks. */
enum
{
	S_STR, S_IMG, S_FRSTR, S_BB, S_FRIMG, S_IB, S_TED, S_OBJ, S_TRI,
	S_CTAB, S_CBLK, S_N
};

/* What a walk does: measure the sections (cursors from 0), find the first
 * item beyond the classic part (cursors at the sections), or write. */
enum
{
	W_SIZE, W_LOCATE, W_EMIT
};

struct wr
{
	const struct rsc	*r;
	int	cell_w, cell_h;
	struct rsc_error	*err;
	int	mode;
	uint8_t	*out;		/* W_EMIT: the file, len bytes */
	size_t	len;
	size_t	cur[S_N];	/* where the next item of each section goes */
	size_t	base[S_N];	/* where each section starts (0 for W_SIZE) */
	size_t	size[S_N];	/* the size of each section (from W_SIZE) */
	size_t	strpad;		/* 1 when a pad byte follows the strings */
	size_t	classic;	/* the end of the tree index */
	size_t	rssize;		/* rsh_rssize: the extension, or the end */
	size_t	beyond;		/* W_LOCATE: first item ending beyond CLASSIC_MAX */
	size_t	ncicon;		/* colour icons so far: the index of the next */
	int	ntree;
	int	*order;		/* the preorder of every tree, one after the other */
	size_t	*first;		/* first[t]: where that of tree t starts in order */
	int	*nreach;	/* nreach[t]: the number of objects of it */
	int	*map;		/* W_EMIT: old index -> new index in the current tree */
};

/* ---- errors -------------------------------------------------------------- */

static void app(char *m, size_t *n, const char *s)
{
	while (*s && *n < sizeof ((struct rsc_error *)0)->msg - 1)
		m[(*n)++] = *s++;
	m[*n] = '\0';
}

static void app_num(char *m, size_t *n, size_t v, int hex)
{
	static const char	dig[] = "0123456789abcdef";
	char	num[3 * sizeof(size_t) + 1];
	size_t	k = sizeof num - 1, b = hex ? 16 : 10;

	num[k] = '\0';
	do
	{
		num[--k] = dig[v % b];
		v /= b;
	} while (v && k > 0);
	app(m, n, num + k);
}

/* Fills the error report with code, offset and "what at offset 0x...".
 * Returns -1 for the caller to pass on. */
static int fail(struct wr *w, int code, size_t off, const char *what)
{
	struct rsc_error	*e = w->err;
	size_t	n = 0;

	if (!e)
		return -1;
	e->code = code;
	e->offset = off;
	app(e->msg, &n, what);
	app(e->msg, &n, " at offset 0x");
	app_num(e->msg, &n, off, 1);
	return -1;
}

/* An error of the model: offset 0, "what (tree t, object i)", or "what
 * (where k)" when i < 0. */
static int fail_in(struct wr *w, int code, const char *what, const char *where, int k, int i)
{
	struct rsc_error	*e = w->err;
	size_t	n = 0;

	if (!e)
		return -1;
	e->code = code;
	e->offset = 0;
	app(e->msg, &n, what);
	app(e->msg, &n, " (");
	app(e->msg, &n, where);
	app(e->msg, &n, " ");
	app_num(e->msg, &n, (size_t)k, 0);
	if (i >= 0)
	{
		app(e->msg, &n, ", object ");
		app_num(e->msg, &n, (size_t)i, 0);
	}
	app(e->msg, &n, ")");
	return -1;
}

static int nomem(struct wr *w)
{
	return fail(w, RSC_ENOMEM, 0, "out of memory");
}

/* ---- positions and bytes ---------------------------------------------------- */

/* *sum := a + b; RSC_ETOOBIG when that overflows. */
static int add(struct wr *w, size_t a, size_t b, size_t *sum)
{
	if (b > (size_t)-1 - a)
		return fail(w, RSC_ETOOBIG, a, "output too big");
	*sum = a + b;
	return 0;
}

/* v does not fit 32 bits (written so that no compiler sees a constant
 * comparison where size_t has 32 bits) */
static int over32(size_t v)
{
	return (v >> 16 >> 16) != 0;
}

/* Reserves n bytes in section s: *at := where they go. */
static int adv(struct wr *w, int s, size_t n, size_t *at)
{
	size_t	p = w->cur[s];

	if (add(w, p, n, &w->cur[s]))
		return -1;
	if (w->mode == W_LOCATE && s < S_CTAB && p + n > CLASSIC_MAX && p < w->beyond)
		w->beyond = p;
	*at = p;
	return 0;
}

/* The writes below do something only in the writing pass, and never
 * outside the buffer (a safety net: the passes agree on every position). */
static int can_put(const struct wr *w, size_t at, size_t n)
{
	return w->mode == W_EMIT && at <= w->len && n <= w->len - at;
}

static void put16(struct wr *w, size_t at, unsigned v)
{
	if (!can_put(w, at, 2))
		return;
	w->out[at] = (uint8_t)(v >> 8);
	w->out[at + 1] = (uint8_t)v;
}

static void put32(struct wr *w, size_t at, uint32_t v)
{
	put16(w, at, (unsigned)(v >> 16) & 0xffff);
	put16(w, at + 2, (unsigned)v & 0xffff);
}

static void put_bytes(struct wr *w, size_t at, const void *p, size_t n)
{
	if (can_put(w, at, n) && n)
		memcpy(w->out + at, p, n);
}

/* n bytes of words in the machine's order at p into section s, big-endian
 * (an odd last byte copied as it is); NULL p: zero words (the buffer starts
 * zeroed).  *at := where. */
static int put_words(struct wr *w, int s, const short *p, size_t n, size_t *at)
{
	const unsigned char	*b = (const unsigned char *)p;
	size_t	k;
	short	v;

	if (adv(w, s, n, at))
		return -1;
	if (!p || !can_put(w, *at, n))
		return 0;
	for (k = 0; k + 1 < n; k += 2)
	{
		memcpy(&v, b + k, 2);
		put16(w, *at + k, (unsigned short)v);
	}
	if (k < n)
		w->out[*at + k] = b[k];
	return 0;
}

/* The string s (NULL: none) into the string section; *off := its offset,
 * or FILE_NULL. */
static int put_str(struct wr *w, const char *s, uint32_t *off)
{
	size_t	at, n;

	*off = FILE_NULL;
	if (!s)
		return 0;
	n = strlen(s) + 1;
	if (adv(w, S_STR, n, &at))
		return -1;
	put_bytes(w, at, s, n);
	*off = (uint32_t)at;
	return 0;
}

/* ---- records ---------------------------------------------------------------- */

static int put_tedinfo(struct wr *w, const TEDINFO *te, uint32_t *spec)
{
	uint32_t	p[3];
	size_t	at;

	if (put_str(w, te->te_ptext, &p[0]) || put_str(w, te->te_ptmplt, &p[1])
	    || put_str(w, te->te_pvalid, &p[2]) || adv(w, S_TED, TED_SIZE, &at))
		return -1;
	put32(w, at, p[0]);
	put32(w, at + 4, p[1]);
	put32(w, at + 8, p[2]);
	put16(w, at + 12, (unsigned short)te->te_font);
	put16(w, at + 14, (unsigned short)te->te_fontid);
	put16(w, at + 16, (unsigned short)te->te_just);
	put16(w, at + 18, (unsigned short)te->te_color);
	put16(w, at + 20, (unsigned short)te->te_fontsize);
	put16(w, at + 22, (unsigned short)te->te_thickness);
	put16(w, at + 24, (unsigned short)te->te_txtlen);
	put16(w, at + 26, (unsigned short)te->te_tmplen);
	*spec = (uint32_t)at;
	return 0;
}

/* The eleven words of an ICONBLK (ib_char .. ib_htext) at at. */
static void put_ibwords(struct wr *w, size_t at, const ICONBLK *ib)
{
	put16(w, at, (unsigned short)ib->ib_char);
	put16(w, at + 2, (unsigned short)ib->ib_xchar);
	put16(w, at + 4, (unsigned short)ib->ib_ychar);
	put16(w, at + 6, (unsigned short)ib->ib_xicon);
	put16(w, at + 8, (unsigned short)ib->ib_yicon);
	put16(w, at + 10, (unsigned short)ib->ib_wicon);
	put16(w, at + 12, (unsigned short)ib->ib_hicon);
	put16(w, at + 14, (unsigned short)ib->ib_xtext);
	put16(w, at + 16, (unsigned short)ib->ib_ytext);
	put16(w, at + 18, (unsigned short)ib->ib_wtext);
	put16(w, at + 20, (unsigned short)ib->ib_htext);
}

static int put_iconblk(struct wr *w, int t, int i, const ICONBLK *ib, uint32_t *spec)
{
	size_t	n, pm, pd, at;
	uint32_t	txt;

	if (ib->ib_wicon < 0 || ib->ib_hicon < 0)
		return fail_in(w, RSC_EARG, "negative icon size", "tree", t, i);
	n = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);
	if (put_words(w, S_IMG, ib->ib_pmask, n, &pm) || put_words(w, S_IMG, ib->ib_pdata, n, &pd)
	    || put_str(w, ib->ib_ptext, &txt) || adv(w, S_IB, IB_SIZE, &at))
		return -1;
	put32(w, at, (uint32_t)pm);
	put32(w, at + 4, (uint32_t)pd);
	put32(w, at + 8, txt);
	put_ibwords(w, at + 12, ib);
	*spec = (uint32_t)at;
	return 0;
}

/* A BITBLK (sizes checked) and its data; *spec := the record's offset. */
static int put_bitblk(struct wr *w, const BITBLK *bb, uint32_t *spec)
{
	size_t	pd, at;

	if (put_words(w, S_IMG, bb->bi_pdata, rsc_bitblk_bytes(bb), &pd)
	    || adv(w, S_BB, BB_SIZE, &at))
		return -1;
	put32(w, at, (uint32_t)pd);
	put16(w, at + 4, (unsigned short)bb->bi_wb);
	put16(w, at + 6, (unsigned short)bb->bi_hl);
	put16(w, at + 8, (unsigned short)bb->bi_x);
	put16(w, at + 10, (unsigned short)bb->bi_y);
	put16(w, at + 12, (unsigned short)bb->bi_color);
	*spec = (uint32_t)at;
	return 0;
}

/* The number of colour versions of c in *n; -1 when the chain loops (the
 * walk of the slow and the fast pointer). */
static int nversions(const struct rsc_cicon *c, size_t *n)
{
	const CICON	*slow = c->blk.mainlist, *fast = slow;
	size_t	k = 0;

	while (fast)
	{
		fast = fast->next_res;
		k++;
		if (!fast)
			break;
		fast = fast->next_res;
		k++;
		slow = slow->next_res;
		if (fast == slow)
			return -1;
	}
	*n = k;
	return 0;
}

/* A pointer field of a version header that says yes or no: the stored
 * value when it says what is true, else 1 or 0. */
static uint32_t flag_field(uint32_t stored, int yes)
{
	return yes ? (stored ? stored : 1) : 0;
}

/* One colour version of a block whose planes have pb bytes. */
static int put_version(struct wr *w, int t, int i, const CICON *v, size_t pb)
{
	const struct rsc_cicon_version	*cv = (const struct rsc_cicon_version *)v;
	int	planes = v->num_planes, sel = v->sel_data || v->sel_mask, k;
	size_t	at, d;
	uint32_t	f;

	if (planes < 1 || planes > RSC_MAX_PLANES)
		return fail_in(w, RSC_ECICON, "bad number of colour planes", "tree", t, i);
	if (pb > (size_t)-1 / (size_t)planes)
		return fail(w, RSC_ETOOBIG, w->cur[S_CBLK], "output too big");
	if (adv(w, S_CBLK, CICON_SIZE, &at))
		return -1;
	put16(w, at, (unsigned)planes);
	for (k = 0; k < 5; k++)
	{
		f = cv->file_ptr[k];
		if (k == 2)
			f = flag_field(f, sel);
		else if (k == 4)
			f = flag_field(f, v->next_res != NULL);
		put32(w, at + 2 + 4 * (size_t)k, f);
	}
	if (put_words(w, S_CBLK, v->col_data, pb * (size_t)planes, &d)
	    || put_words(w, S_CBLK, v->col_mask, pb, &d))
		return -1;
	if (sel && (put_words(w, S_CBLK, v->sel_data, pb * (size_t)planes, &d)
		    || put_words(w, S_CBLK, v->sel_mask, pb, &d)))
		return -1;
	return 0;
}

/* The colour icon c of object i: its table entry and block; *spec := its
 * index. */
static int put_cicon(struct wr *w, int t, int i, const struct rsc_cicon *c, uint32_t *spec)
{
	const ICONBLK	*ib = &c->blk.monoblk;
	const CICON	*v;
	size_t	pb, nv, tab, at, d;
	int	k;

	if (ib->ib_wicon < 0 || ib->ib_hicon < 0)
		return fail_in(w, RSC_EARG, "negative colour icon size", "tree", t, i);
	if (nversions(c, &nv))
		return fail_in(w, RSC_ECICON, "colour version chain loops", "tree", t, i);
	pb = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);
	if (adv(w, S_CTAB, 4, &tab) || adv(w, S_CBLK, CBLK_SIZE, &at))
		return -1;
	put32(w, tab, (uint32_t)at);
	for (k = 0; k < 3; k++)
		put32(w, at + 4 * (size_t)k, c->file_head[k]);
	put_ibwords(w, at + 12, ib);
	put32(w, at + 34, (uint32_t)nv);
	if (put_words(w, S_CBLK, ib->ib_pdata, pb, &d) || put_words(w, S_CBLK, ib->ib_pmask, pb, &d)
	    || adv(w, S_CBLK, CTEXT_SIZE, &d))
		return -1;
	put_bytes(w, d, c->text, CTEXT_SIZE);
	for (v = c->blk.mainlist; v; v = v->next_res)
		if (put_version(w, t, i, v, pb))
			return -1;
	*spec = (uint32_t)w->ncicon++;
	return 0;
}

/* ---- objects and trees -------------------------------------------------------- */

/* A coordinate converted back to file form with cell size cell
 * (unfix_chpos): the low byte pix / cell, the high byte the remainder,
 * in two's complement as the old code computed it. */
static unsigned unfix_coord(int pix, int cell)
{
	int	coff = pix / cell, cbits = pix - coff * cell;

	return ((unsigned)coff | (unsigned)cbits << 8) & 0xffff;
}

/* Coordinate v in file form into *out; -1 when the file form cannot hold
 * it (only with a cell size). */
static int coord(int v, int cell, unsigned *out)
{
	if (!cell)
	{
		*out = (unsigned short)v;
		return 0;
	}
	if (v < 0 || v > 256 * cell - 1)
		return -1;
	*out = unfix_coord(v, cell);
	return 0;
}

/* A link of the current tree (n objects) renumbered; -1 when it points to
 * no written object (cannot happen after rsc_tree_preorder). */
static int new_link(const struct wr *w, int l, int n, unsigned *out)
{
	if (l == -1)
	{
		*out = 0xffff;
		return 0;
	}
	if (l < 0 || l >= n || w->map[l] < 0)
		return -1;
	*out = (unsigned)w->map[l];
	return 0;
}

/* Object i of tree t (n objects), the k-th of cnt written. */
static int put_object(struct wr *w, int t, const OBJECT *objs, int n, int i, int k, int cnt)
{
	const OBJECT	*o = objs + i;
	uint32_t	spec = FILE_NULL;
	unsigned	type = o->ob_type, flags, next = 0xffff, head, tail, xywh[4];
	size_t	at;
	int	rc = 0, j;

	switch (rsc_obj_spec_kind(o))
	{
	case RSC_SPEC_VALUE:
		spec = (uint32_t)o->ob_spec.index;
		break;
	case RSC_SPEC_STRING:
		rc = put_str(w, o->ob_spec.free_string, &spec);
		break;
	case RSC_SPEC_TEDINFO:
		if (o->ob_spec.tedinfo)
			rc = put_tedinfo(w, o->ob_spec.tedinfo, &spec);
		break;
	case RSC_SPEC_ICONBLK:
		if (o->ob_spec.iconblk)
			rc = put_iconblk(w, t, i, o->ob_spec.iconblk, &spec);
		break;
	case RSC_SPEC_BITBLK:
		if (!o->ob_spec.bitblk)
			break;
		if (o->ob_spec.bitblk->bi_wb < 0 || o->ob_spec.bitblk->bi_hl < 0)
			return fail_in(w, RSC_EARG, "negative image size", "tree", t, i);
		rc = put_bitblk(w, o->ob_spec.bitblk, &spec);
		break;
	case RSC_SPEC_CICON:
		if (o->ob_spec.ciconblk)
			rc = put_cicon(w, t, i, (const struct rsc_cicon *)(const void *)o->ob_spec.ciconblk,
				       &spec);
		break;
	}
	if (rc || adv(w, S_OBJ, OBJ_SIZE, &at))
		return -1;
	if (w->mode != W_EMIT)
		return 0;
	if ((k && new_link(w, o->ob_next, n, &next)) || new_link(w, o->ob_head, n, &head)
	    || new_link(w, o->ob_tail, n, &tail))
		return fail_in(w, RSC_ELINK, "tree link out of range", "tree", t, i);
	if (coord(o->ob_x, w->cell_w, &xywh[0]) || coord(o->ob_y, w->cell_h, &xywh[1])
	    || coord(o->ob_width, w->cell_w, &xywh[2]) || coord(o->ob_height, w->cell_h, &xywh[3]))
		return fail(w, RSC_ETOOBIG, at, "coordinate does not fit the file form");
	switch (type & 0xff)
	{
	case G_ICON:
	case G_IMAGE:
	case G_CICON:
		type &= 0xff;
		break;
	}
	flags = (o->ob_flags & ~(unsigned)OF_LASTOB) | (k == cnt - 1 ? OF_LASTOB : 0);
	put16(w, at, next);
	put16(w, at + 2, head);
	put16(w, at + 4, tail);
	put16(w, at + 6, type);
	put16(w, at + 8, flags);
	put16(w, at + 10, o->ob_state);
	put32(w, at + 12, spec);
	for (j = 0; j < 4; j++)
		put16(w, at + 16 + 2 * (size_t)j, xywh[j]);
	return 0;
}

static int walk_trees(struct wr *w)
{
	const OBJECT	*objs;
	const int	*ord;
	int	t, n, k, cnt;
	size_t	root, at;

	for (t = 0; t < w->ntree; t++)
	{
		objs = rsc_tree_objects(w->r, t, &n);
		ord = w->order + w->first[t];
		cnt = w->nreach[t];
		if (w->mode == W_EMIT)
		{
			for (k = 0; k < n; k++)
				w->map[k] = -1;
			for (k = 0; k < cnt; k++)
				w->map[ord[k]] = k;
		}
		root = w->cur[S_OBJ];
		for (k = 0; k < cnt; k++)
			if (put_object(w, t, objs, n, ord[k], k, cnt))
				return -1;
		if (adv(w, S_TRI, 4, &at))
			return -1;
		put32(w, at, (uint32_t)root);
	}
	return 0;
}

static int walk_free(struct wr *w)
{
	const BITBLK	*bb;
	uint32_t	off;
	size_t	at;
	int	k;

	for (k = 0; k < rsc_nfrstr(w->r); k++)
		if (put_str(w, rsc_frstr(w->r, k), &off) || adv(w, S_FRSTR, 4, &at))
			return -1;
		else
			put32(w, at, off);
	for (k = 0; k < rsc_nfrimg(w->r); k++)
	{
		bb = rsc_frimg(w->r, k);
		off = FILE_NULL;
		if (bb && (bb->bi_wb < 0 || bb->bi_hl < 0))
			return fail_in(w, RSC_EARG, "negative image size", "free image", k, -1);
		if ((bb && put_bitblk(w, bb, &off)) || adv(w, S_FRIMG, 4, &at))
			return -1;
		put32(w, at, off);
	}
	return 0;
}

/* One pass over the whole resource in w->mode, the cursors starting at the
 * sections. */
static int walk(struct wr *w)
{
	memcpy(w->cur, w->base, sizeof w->cur);
	w->ncicon = 0;
	return walk_trees(w) || walk_free(w) ? -1 : 0;
}

/* ---- the passes ------------------------------------------------------------------ */

/* The preorder of every tree (and the scratch the writing pass needs). */
static int prepare(struct wr *w)
{
	size_t	total = 0;
	int	t, n, maxn = 1, cnt;
	const OBJECT	*objs;

	w->ntree = rsc_ntrees(w->r);
	for (t = 0; t < w->ntree; t++)
	{
		if (!rsc_tree_objects(w->r, t, &n) || n < 1)
			return fail_in(w, RSC_ELINK, "empty tree", "tree", t, -1);
		if ((size_t)n > (size_t)-1 / sizeof(int) - total)
			return fail(w, RSC_ETOOBIG, 0, "too many objects");
		total += (size_t)n;
		if (n > maxn)
			maxn = n;
	}
	w->order = rsc_malloc(total * sizeof *w->order);
	w->first = rsc_malloc((size_t)w->ntree * sizeof *w->first);
	w->nreach = rsc_malloc((size_t)w->ntree * sizeof *w->nreach);
	w->map = rsc_malloc((size_t)maxn * sizeof *w->map);
	if (!w->order || !w->first || !w->nreach || !w->map)
		return nomem(w);
	total = 0;
	for (t = 0; t < w->ntree; t++)
	{
		objs = rsc_tree_objects(w->r, t, &n);
		w->first[t] = total;
		cnt = rsc_tree_preorder(objs, n, w->order + total);
		switch (cnt)
		{
		case RSC_WALK_ENOMEM:
			return nomem(w);
		case RSC_WALK_ESHARED:
			return fail_in(w, RSC_ECYCLE, "tree links form a cycle or share an object",
				       "tree", t, -1);
		case RSC_WALK_EARG:
		case RSC_WALK_ERANGE:
			return fail_in(w, RSC_ELINK, "tree link out of range", "tree", t, -1);
		case RSC_WALK_ECHAIN:
			return fail_in(w, RSC_ELINK, "malformed sibling chain", "tree", t, -1);
		default:
			if (cnt < 1)
				return fail_in(w, RSC_ELINK, "tree link out of range", "tree", t, -1);
			break;
		}
		w->nreach[t] = cnt;
		total += (size_t)n;
	}
	return 0;
}

/* The classic part is too big: the offset of the first item that ends
 * beyond it, found by a walk with the real positions. */
static int too_big(struct wr *w)
{
	size_t	strend = HDR_SIZE + w->size[S_STR];

	w->beyond = (size_t)-1;
	w->mode = W_LOCATE;
	if (walk(w))
		return -1;
	if (w->strpad && strend >= CLASSIC_MAX && strend < w->beyond)
		w->beyond = strend;
	if (w->ncicon && (w->classic & 1) && w->classic >= CLASSIC_MAX && w->classic < w->beyond)
		w->beyond = w->classic;
	if (w->beyond == (size_t)-1)
		w->beyond = CLASSIC_MAX + 1;
	return fail(w, RSC_ETOOBIG, w->beyond, "classic part exceeds 65535 bytes");
}

/* The offsets of the sections from their sizes (measured by the first
 * pass), and the length of the file. */
static int layout(struct wr *w)
{
	size_t	p, end;
	int	s;

	memcpy(w->size, w->cur, sizeof w->size);
	memset(w->base, 0, sizeof w->base);
	w->base[S_STR] = HDR_SIZE;
	if (add(w, HDR_SIZE, w->size[S_STR], &p))
		return -1;
	w->strpad = p & 1;
	if (add(w, p, w->strpad, &p))
		return -1;
	for (s = S_IMG; s <= S_TRI; s++)
	{
		w->base[s] = p;
		if (add(w, p, w->size[s], &p))
			return -1;
	}
	w->classic = p;
	end = p;
	w->rssize = p;
	if (w->ncicon)
	{
		if (add(w, p, p & 1, &w->rssize) || add(w, w->rssize, EXT_SIZE, &w->base[S_CTAB])
		    || add(w, w->base[S_CTAB], w->size[S_CTAB], &p) || add(w, p, 4, &w->base[S_CBLK])
		    || add(w, w->base[S_CBLK], w->size[S_CBLK], &end))
			return -1;
	}
	if (w->rssize > CLASSIC_MAX)
		return too_big(w);
	if (over32(end))
		return fail(w, RSC_ETOOBIG, 0, "output exceeds 4 GB");
	w->len = end;
	return 0;
}

/* The header and the extension, after the writing pass. */
static int finish(struct wr *w)
{
	unsigned	h[H_WORDS];
	int	s;

	for (s = 0; s < S_N; s++)
		if (w->cur[s] != w->base[s] + w->size[s])
			return fail(w, RSC_EARG, w->cur[s], "the passes disagree");
	h[H_VRSN] = (rsc_vrsn(w->r) & 0xffff & ~(unsigned)RSC_VRSN_FORMAT_BITS)
		    | (w->ncicon ? RSC_VRSN_FORMAT_BITS : 0);
	h[H_OBJECT] = (unsigned)w->base[S_OBJ];
	h[H_TEDINFO] = (unsigned)w->base[S_TED];
	h[H_ICONBLK] = (unsigned)w->base[S_IB];
	h[H_BITBLK] = (unsigned)w->base[S_BB];
	h[H_FRSTR] = w->size[S_FRSTR] ? (unsigned)w->base[S_FRSTR] : 0;
	h[H_STRING] = HDR_SIZE;
	h[H_IMDATA] = (unsigned)w->base[S_IMG];
	h[H_FRIMG] = w->size[S_FRIMG] ? (unsigned)w->base[S_FRIMG] : 0;
	h[H_TRINDEX] = (unsigned)w->base[S_TRI];
	h[H_NOBS] = (unsigned)(w->size[S_OBJ] / OBJ_SIZE);
	h[H_NTREE] = (unsigned)(w->size[S_TRI] / 4);
	h[H_NTED] = (unsigned)(w->size[S_TED] / TED_SIZE);
	h[H_NIB] = (unsigned)(w->size[S_IB] / IB_SIZE);
	h[H_NBB] = (unsigned)(w->size[S_BB] / BB_SIZE);
	h[H_NSTRING] = (unsigned)(w->size[S_FRSTR] / 4);
	h[H_NIMAGES] = (unsigned)(w->size[S_FRIMG] / 4);
	h[H_RSSIZE] = (unsigned)w->rssize;
	for (s = 0; s < H_WORDS; s++)
		put16(w, 2 * (size_t)s, h[s]);
	if (w->ncicon)
	{
		put32(w, w->rssize, (uint32_t)w->len);
		put32(w, w->rssize + 4, (uint32_t)w->base[S_CTAB]);
		put32(w, w->rssize + 8, 0);
		put32(w, w->base[S_CTAB] + w->size[S_CTAB], FILE_NULL);
	}
	return 0;
}

int rsc_atari_write(const struct rsc *r, int cell_w, int cell_h,
		    uint8_t **out, size_t *len, struct rsc_error *err)
{
	struct wr	w;
	int	mw, mh, rc = -1;

	memset(&w, 0, sizeof w);
	w.err = err;
	if (err)
	{
		err->code = RSC_OK;
		err->offset = 0;
		err->msg[0] = '\0';
	}
	if (out)
		*out = NULL;
	if (len)
		*len = 0;
	if (!r || !out || !len)
		return fail(&w, RSC_EARG, 0, "bad argument");
	if (cell_w < 0 || cell_h < 0 || cell_w > RSC_MAX_CELL || cell_h > RSC_MAX_CELL
	    || (cell_w == 0) != (cell_h == 0))
		return fail(&w, RSC_EARG, 0, "bad cell size");
	rsc_cell(r, &mw, &mh);
	if (mw != cell_w || mh != cell_h)
		return fail(&w, RSC_EARG, 0, "cell size differs from the model's");
	w.r = r;
	w.cell_w = cell_w;
	w.cell_h = cell_h;
	w.mode = W_SIZE;
	if (prepare(&w) || walk(&w) || layout(&w))
		goto out;
	w.out = rsc_malloc(w.len);
	if (!w.out)
	{
		nomem(&w);
		goto out;
	}
	memset(w.out, 0, w.len);
	w.mode = W_EMIT;
	if (walk(&w) || finish(&w))
		goto out;
	*out = w.out;
	*len = w.len;
	w.out = NULL;
	rc = 0;
out:
	rsc_mfree(w.out);
	rsc_mfree(w.order);
	rsc_mfree(w.first);
	rsc_mfree(w.nreach);
	rsc_mfree(w.map);
	return rc;
}
