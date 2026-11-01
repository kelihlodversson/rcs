/* RSCATARI_READ.C	the reader of the classic Atari .RSC format: the file
 *			image decoded into the model of rscmodel.h
 *
 * The file (all numbers big-endian, all pointers file offsets, a NULL
 * pointer 0xFFFFFFFF):
 *
 *   header	36 bytes, 18 words: vrsn, object, tedinfo, iconblk, bitblk,
 *		frstr, string, imdata, frimg, trindex (offsets), nobs, ntree,
 *		nted, nib, nbb, nstring, nimages (counts), rssize
 *   OBJECT	24 bytes: next, head, tail, type, flags, state (words), spec
 *		(long), x, y, width, height (words)
 *   TEDINFO	28 bytes: ptext, ptmplt, pvalid (longs), font, fontid, just,
 *		color, fontsize, thickness, txtlen, tmplen (words)
 *   ICONBLK	34 bytes: pmask, pdata, ptext (longs), char, xchar, ychar,
 *		xicon, yicon, wicon, hicon, xtext, ytext, wtext, htext (words)
 *   BITBLK	14 bytes: pdata (long), wb, hl, x, y, color (words)
 *   tables	the tree index (ntree longs: the offsets of the roots), the
 *		free strings (nstring longs) and the free images (nimages
 *		longs: offsets of BITBLKs)
 *
 * With bit 2 of vrsn set, longs at rssize extend the file: the total
 * file length, the offset of the colour icon table, then more longs that
 * this library ignores (the writer writes a third long of 0; TeraDesk's
 * files have a non-zero third long, the offset of a colour palette after
 * the blocks, which is not kept).  The table is a list of longs ending
 * with 0xFFFFFFFF, one per colour icon block; their values do not matter
 * (the AES and EmuTOS fill them in with pointers when they load the file,
 * pTOS's aes/rsload_legacy.c; real files such as TeraDesk's have zeros,
 * this writer and pTOS's resource generator put the offsets of the
 * blocks).  The first block starts right after the terminator and each
 * next one right after the previous, as long as its own head says; a
 * G_CICON object's spec is the index of its block in that sequence.
 * Bytes after the last block are ignored.  A block:
 *
 *   ICONBLK	34 bytes, its three pointers meaningless (kept opaque in
 *		struct rsc_cicon.file_head)
 *   count	a long: the number of colour versions
 *   mono	data, then mask: ((w + 15) / 16) * h words each
 *   text	12 bytes
 *   versions	count times: a 22-byte header (planes word; col_data,
 *		col_mask, sel_data, sel_mask, next_res longs, kept opaque in
 *		struct rsc_cicon_version.file_ptr), colour data (planes
 *		planes) and mask (1 plane), and when sel_data is not 0 the
 *		selected data and mask likewise
 *
 * Every offset and count is checked here, once, before it is used: every
 * read of the buffer goes through a range check and every sum of offsets is
 * computed so that it cannot overflow.  Every loop is bounded by the
 * buffer length or by a fixed limit (RSC_MAX_VERSIONS versions of a colour
 * icon, checked from the block's count before any version is looked at),
 * and the work done per step of a loop over the file is either constant or
 * charged to the budget below.  A violation fails the whole read with a
 * code and the position in the file; what was built so far is freed.
 *
 * The budget.  A small file could still name one big record from many
 * objects, and every object gets its own copy.  So every allocation the
 * reader makes or causes (object arrays, records, strings, image data and
 * the temporary copies it decodes them into, colour icons and versions,
 * list entries, scratch of the tree walk) is charged, before it is made,
 * at its size plus BLOCK_COST bytes, and so is the work of checking the
 * colour icon blocks (their heads and version headers).  When the total
 * would exceed 4 * len + 65536 bytes the read fails with RSC_ETOOBIG at
 * the object being decoded.  So the time of a read is proportional to len
 * plus what it charges, which is at most the budget: linear in len (a
 * colour version is appended in constant time and checking one is
 * charged).  Since records are copied per object, a file in which k
 * objects share one big record or colour block is refused once k is above
 * about 2.  Legitimate files stay well inside: measured up to about 3x
 * len for tiny objects (15000 one-object trees: 3.04x) and about 2x for
 * typical files (rcs.rsc: 1.89x, 47 % of 4 * len).
 *
 * Decisions:
 *  - rsh_rssize is not trusted for a plain file: the buffer length decides
 *    and bytes after the data are ignored; rsh_string is not used.
 *  - A table with a count of 0 is not looked at (its offset may be stale).
 *  - A tree is the run of objects that starts at its root and ends at the
 *    first of: the first object at or after the root with OF_LASTOB (the
 *    root itself when it has the flag: a tree of one object), the object
 *    just before the next root (the smallest root offset of the tree index
 *    above this one), the last object of the table.  cicon.rsc has no
 *    OF_LASTOB at all.  So runs cannot overlap; a root listed twice in the
 *    tree index is an error (RSC_ELINK).  The links are relative to the
 *    root.  Objects of the run that the root does not reach are kept with
 *    their type, flags, links and coordinates, but their specs are not
 *    decoded (left empty: they are dropped on write anyway), so a bad spec
 *    there does not fail the read.  Objects in no run are ignored.
 *  - Records and strings shared by objects are copied per object (the
 *    setters copy).
 *  - te_txtlen and te_tmplen become strlen + 1 of a non-NULL te_ptext and
 *    te_ptmplt, as the old RCS reader did (so the editing capacity of a
 *    foreign file may shrink); above 32767 that is RSC_ETOOBIG.
 *  - Negative ICONBLK and BITBLK sizes are refused (RSC_EOFFSET at the
 *    field), as pTOS's loader does.
 *  - NULL entries of the free string and free image tables are dropped.
 *  - A colour icon table offset of 0 or 0xFFFFFFFF means no colour icons,
 *    as for the old reader and the AES; a G_CICON object then has no
 *    colour icon if its spec is 0xFFFFFFFF and is an error otherwise.
 *  - A cell size above RSC_MAX_CELL (127, rscfile.h) is refused
 *    (RSC_EARG), so that a converted coordinate (low byte * cell + high
 *    byte, at most 255 * 127 + 128 = 32513) always fits a short.
 *  - A colour icon block with more than RSC_MAX_VERSIONS (32) versions is
 *    refused (RSC_ECICON at the block), before any version is checked.
 *  - Colour icons, where pTOS's loader is stricter: the total length of
 *    the extension is accepted when it is at most len (pTOS: equal to
 *    len); a G_CICON index of 0xFFFFFFFF is accepted as no colour icon
 *    when there is a table (pTOS rejects it); next_res is kept opaque and
 *    not checked (pTOS wants 1, ..., 1, 0).
 *  - The colour icon table: its entries are counted up to the terminator
 *    (none before the end of the buffer is RSC_ECICON at the table) and
 *    their values ignored.  A table with more entries than the rest of
 *    the file could hold blocks (each takes at least CBLK_SIZE +
 *    CTEXT_SIZE bytes) is refused at once, at the first entry that cannot
 *    fit, before any block is looked at; otherwise the blocks are checked
 *    one after the other, and a file that ends before the block of an
 *    entry is RSC_ECICON at the entry ("lists more blocks than the file
 *    holds"), one that ends inside a block RSC_ECICON in the block.  So
 *    the work on the table is linear in len.  The blocks stay at even
 *    offsets: the table offset is even and every block length is even.
 *  - Errors that concern a pointer report the position of the field that
 *    holds it, except a string without its NUL, which reports the string.
 *
 * Differences from the old RCS reader (src/rcsread.c, src/rcscicon.c) and
 * pTOS's portable loader: both took the colour icon table entries as the
 * offsets of the blocks, which only holds for files written by pTOS's
 * generator, the old RCS writer and this writer, whose entries happen to
 * equal the sequential positions.  A real file with zero entries
 * (TeraDesk's cicons.rsc) lost its colour icons in the old reader (it
 * stopped at the first entry that is not a block).  Here the blocks are
 * found as the AES finds them.
 */
#include <string.h>
#include "rscatari.h"

/* What one allocation costs in the budget beyond its size.  The densest
 * file the writer can make (13099 empty free strings: 5 bytes of file and
 * 25 charged each) reads back with about 200 bytes of the budget to spare:
 * raising this or adding a charge per table entry or string can make the
 * reader refuse what the writer writes.  test_densest (tests/rsc/
 * test_write.c) checks it; change it consciously. */
#define BLOCK_COST	8

/* a table of the file: where, how many, how big */
struct table
{
	size_t	off;
	size_t	n;
	size_t	size;
};

struct rd
{
	const uint8_t	*buf;
	size_t	len;
	int	cell_w, cell_h;
	struct rsc	*r;
	struct rsc_error	*err;
	unsigned	h[H_WORDS];
	struct table	obj, ted, ib, bb;
	size_t	ctable;		/* offset of the colour icon table, 0 for none */
	size_t	ncicon;		/* the number of colour icon blocks */
	size_t	*cblk;		/* their offsets (rsc_malloc), NULL for none */
	size_t	budget;		/* bytes that may be charged */
	size_t	used;		/* bytes charged so far */
	size_t	at;		/* the object (or table entry) being decoded */
};

/* ---- errors -------------------------------------------------------------- */

/* Fills the error report: code, offset and "what at offset 0x...".
 * Returns -1 for the caller to pass on. */
static int fail(struct rd *d, int code, size_t off, const char *what)
{
	rsc_error_set(d->err, code, off, what);
	return -1;
}

static int nomem(struct rd *d)
{
	return fail(d, RSC_ENOMEM, 0, "out of memory");
}

/* Charges an allocation of n bytes (or that much work) to the budget. */
static int charge(struct rd *d, size_t n)
{
	if (n > d->budget - d->used || BLOCK_COST > d->budget - d->used - n)
		return fail(d, RSC_ETOOBIG, d->at, "decoded size exceeds the limit");
	d->used += n + BLOCK_COST;
	return 0;
}

/* n bytes of image data at off are decoded into a temporary block and
 * copied by the model: two blocks, nothing for FILE_NULL. */
static int charge_data(struct rd *d, uint32_t off, size_t n)
{
	return off == FILE_NULL ? 0 : charge(d, n) || charge(d, n) ? -1 : 0;
}

static int charge_str(struct rd *d, const char *s)
{
	return s ? charge(d, strlen(s) + 1) : 0;
}

/* ---- reading the buffer --------------------------------------------------- */

/* n bytes at off are inside the buffer (no overflow: off is compared first) */
static int in_buf(const struct rd *d, size_t off, size_t n)
{
	return off <= d->len && n <= d->len - off;
}

/* A big-endian word or long at off; 0 outside the buffer (a safety net:
 * every caller has checked the range before). */
static unsigned be16(const struct rd *d, size_t off)
{
	if (!in_buf(d, off, 2))
		return 0;
	return (unsigned)d->buf[off] << 8 | d->buf[off + 1];
}

static short sbe16(const struct rd *d, size_t off)
{
	unsigned	v = be16(d, off);

	return (short)(v >= 0x8000 ? (int)v - 0x10000 : (int)v);
}

static uint32_t be32(const struct rd *d, size_t off)
{
	if (!in_buf(d, off, 4))
		return 0;
	return (uint32_t)be16(d, off) << 16 | be16(d, off + 2);
}

/* A block of nbytes of big-endian words at off (the range has been
 * checked) converted to the machine's order (an odd last byte is copied as
 * it is), allocated with rsc_malloc, in *out.  Returns 0 or -1. */
static int words_at(struct rd *d, size_t off, size_t nbytes, short **out)
{
	unsigned char	*p;
	size_t	k;

	*out = NULL;
	if (!in_buf(d, off, nbytes))
		return fail(d, RSC_ETRUNC, off, "data past the end");
	p = rsc_malloc(nbytes);
	if (!p)
		return nomem(d);
	for (k = 0; k + 1 < nbytes; k += 2)
	{
		short	w = sbe16(d, off + k);

		memcpy(p + k, &w, 2);
	}
	if (k < nbytes)
		p[k] = d->buf[off + k];
	*out = (short *)(void *)p;
	return 0;
}

/* words_at for an image data pointer of a record: FILE_NULL gives NULL. */
static int data_at(struct rd *d, uint32_t off, size_t nbytes, short **out)
{
	*out = NULL;
	return off == FILE_NULL ? 0 : words_at(d, off, nbytes, out);
}

/* The string at off (FILE_NULL: NULL in *s): it must start in the buffer and end
 * with a NUL before its end.  field is the position of the pointer. */
static int string_at(struct rd *d, uint32_t off, size_t field, const char **s)
{
	*s = NULL;
	if (off == FILE_NULL)
		return 0;
	if (off >= d->len)
		return fail(d, RSC_ESTRING, field, "string pointer past the end");
	if (!memchr(d->buf + off, 0, d->len - off))
		return fail(d, RSC_ESTRING, off, "string without NUL");
	*s = (const char *)(d->buf + off);
	return 0;
}

/* Image data of nbytes at off (FILE_NULL allowed) lies inside the buffer. */
static int data_ok(struct rd *d, uint32_t off, size_t nbytes, size_t field)
{
	if (off == FILE_NULL || in_buf(d, off, nbytes))
		return 0;
	return fail(d, off >= d->len ? RSC_EOFFSET : RSC_ETRUNC, field, "image data past the end");
}

/* off is the start of a record of table t (field holds it). */
static int record_in(struct rd *d, const struct table *t, uint32_t off, size_t field,
		     const char *what)
{
	if (off < t->off || off - t->off >= t->n * t->size || (off - t->off) % t->size)
		return fail(d, RSC_EOFFSET, field, what);
	return 0;
}

/* The two size words at off and off + 2 of a record are not negative. */
static int sizes_ok(struct rd *d, size_t off, const char *what)
{
	if (sbe16(d, off) < 0)
		return fail(d, RSC_EOFFSET, off, what);
	if (sbe16(d, off + 2) < 0)
		return fail(d, RSC_EOFFSET, off + 2, what);
	return 0;
}

/* ---- the header and the tables --------------------------------------------- */

/* Table at the offset in header word wo, count in header word wn. */
static int table(struct rd *d, struct table *t, int wo, int wn, size_t size)
{
	t->off = d->h[wo];
	t->n = d->h[wn];
	t->size = size;
	if (t->n == 0)
		return 0;
	if (t->off > d->len)
		return fail(d, RSC_EOFFSET, 2 * (size_t)wo, "table past the end");
	if (t->n > (d->len - t->off) / size)
		return fail(d, RSC_ETRUNC, 2 * (size_t)wo, "table truncated");
	return 0;
}

static int header(struct rd *d)
{
	struct table	t;
	int	k;

	if (d->len < HDR_SIZE)
		return fail(d, RSC_ETRUNC, d->len, "header truncated");
	for (k = 0; k < H_WORDS; k++)
		d->h[k] = be16(d, 2 * (size_t)k);
	rsc_set_vrsn(d->r, d->h[H_VRSN]);
	return table(d, &d->obj, H_OBJECT, H_NOBS, OBJ_SIZE)
	       || table(d, &d->ted, H_TEDINFO, H_NTED, TED_SIZE)
	       || table(d, &d->ib, H_ICONBLK, H_NIB, IB_SIZE)
	       || table(d, &d->bb, H_BITBLK, H_NBB, BB_SIZE)
	       || table(d, &t, H_TRINDEX, H_NTREE, 4)
	       || table(d, &t, H_FRSTR, H_NSTRING, 4)
	       || table(d, &t, H_FRIMG, H_NIMAGES, 4) ? -1 : 0;
}

/* ---- the colour icon extension ---------------------------------------------- */

/* Words of one plane of the colour icon block at off. */
static size_t cblk_words(const struct rd *d, size_t off)
{
	int	w = sbe16(d, off + 22), h = sbe16(d, off + 24);

	return rsc_plane_bytes(w, h) / 2;
}

/* Checks the version header at pos of a block with planes of words words
 * and moves *pos past the version. */
static int check_version(struct rd *d, size_t *pos, size_t words)
{
	size_t	hdr = *pos, payload;
	unsigned	planes;

	if (charge(d, CICON_SIZE))
		return -1;
	if (!in_buf(d, hdr, CICON_SIZE))
		return fail(d, RSC_ECICON, hdr, "colour version truncated");
	planes = be16(d, hdr);
	if (planes < 1 || planes > RSC_MAX_PLANES)
		return fail(d, RSC_ECICON, hdr, "bad number of colour planes");
	if (words > (d->len - hdr) / (2 * ((size_t)planes + 1)))
		return fail(d, RSC_ECICON, hdr, "colour version truncated");
	payload = words * 2 * (planes + 1);
	*pos = hdr + CICON_SIZE;
	if (!in_buf(d, *pos, payload))
		return fail(d, RSC_ECICON, hdr, "colour version truncated");
	*pos += payload;
	if (be32(d, hdr + 10) != 0)
	{
		if (!in_buf(d, *pos, payload))
			return fail(d, RSC_ECICON, hdr, "selected colour version truncated");
		*pos += payload;
	}
	return 0;
}

/* Checks the colour icon block at off (the table entry at field) and sets
 * *end to the first byte after it. */
static int check_cblk(struct rd *d, size_t off, size_t field, size_t *end)
{
	size_t	words, pos;
	uint32_t	count, i;

	if (charge(d, CBLK_SIZE))
		return -1;
	if ((off & 1) || !in_buf(d, off, CBLK_SIZE))
		return fail(d, RSC_ECICON, field, "bad colour icon block offset");
	if (sbe16(d, off + 22) < 0 || sbe16(d, off + 24) < 0)
		return fail(d, RSC_ECICON, off, "negative colour icon size");
	words = cblk_words(d, off);
	pos = off + CBLK_SIZE;
	if (words > (d->len - pos) / 4 || !in_buf(d, pos + words * 4, CTEXT_SIZE))
		return fail(d, RSC_ECICON, off, "colour icon block truncated");
	pos += words * 4 + CTEXT_SIZE;
	count = be32(d, off + 34);
	if (count > RSC_MAX_VERSIONS)
		return fail(d, RSC_ECICON, off, "too many colour versions");
	if (count > (d->len - pos) / CICON_SIZE)
		return fail(d, RSC_ECICON, off + 34, "bad number of colour versions");
	for (i = 0; i < count; i++)
		if (check_version(d, &pos, words))
			return -1;
	*end = pos;
	return 0;
}

static int too_few_blocks(struct rd *d, size_t entry)
{
	return fail(d, RSC_ECICON, entry, "colour icon table lists more blocks than the file holds");
}

/* The blocks of the table at tab: one per entry before the terminator,
 * the first right after it, each next right after the previous; the
 * entries' values are not looked at.  Their offsets go to d->cblk. */
static int scan_table(struct rd *d, size_t tab)
{
	size_t	pos, n, k, room;

	for (pos = tab; be32(d, pos) != FILE_NULL; pos += 4)
		if (!in_buf(d, pos + 4, 4))
			return fail(d, RSC_ECICON, tab, "colour icon table without end");
	n = (pos - tab) / 4;
	pos += 4;
	d->ctable = tab;
	/* every block takes at least CBLK_SIZE + CTEXT_SIZE bytes */
	room = (d->len - pos) / (CBLK_SIZE + CTEXT_SIZE);
	if (n > room)
		return too_few_blocks(d, tab + 4 * room);
	if (n == 0)
		return 0;
	if (charge(d, n * sizeof *d->cblk))
		return -1;
	d->cblk = rsc_malloc(n * sizeof *d->cblk);
	if (!d->cblk)
		return nomem(d);
	for (k = 0; k < n; k++)
	{
		d->at = tab + 4 * k;
		if (!in_buf(d, pos, CBLK_SIZE))
			return too_few_blocks(d, d->at);
		d->cblk[k] = pos;
		if (check_cblk(d, pos, d->at, &pos))
			return -1;
		d->ncicon++;
	}
	return 0;
}

/* The extension and the colour icon table, when bit 2 of vrsn says so;
 * every block is checked here.  The longs after the table offset are
 * ignored. */
static int cicon_ext(struct rd *d)
{
	size_t	ext = d->h[H_RSSIZE];
	uint32_t	total, tab;

	if (!(d->h[H_VRSN] & RSC_VRSN_FORMAT_BITS))
		return 0;
	d->at = 2 * H_RSSIZE;
	if (!in_buf(d, ext, 12))
		return fail(d, RSC_ECICON, 2 * H_RSSIZE, "colour icon extension past the end");
	total = be32(d, ext);
	tab = be32(d, ext + 4);
	if (total < ext + 12 || total > d->len)
		return fail(d, RSC_ECICON, ext, "bad file length in the colour icon extension");
	if (tab == 0 || tab == FILE_NULL)
		return 0;
	if ((tab & 1) || !in_buf(d, tab, 4))
		return fail(d, RSC_ECICON, ext + 4, "bad colour icon table offset");
	return scan_table(d, tab);
}

/* ---- specs -------------------------------------------------------------------- */

static int set_string(struct rd *d, int t, int i, uint32_t off, size_t field)
{
	const char	*s;

	if (string_at(d, off, field, &s) || charge_str(d, s))
		return -1;
	if (s && rsc_obj_set_str(d->r, t, i, RSC_STR_SPEC, s))
		return nomem(d);
	return 0;
}

static int set_tedinfo(struct rd *d, int t, int i, uint32_t off, size_t field)
{
	TEDINFO	te;
	const char	*s[3];
	int	k;

	if (off == FILE_NULL)
		return 0;
	if (record_in(d, &d->ted, off, field, "TEDINFO pointer outside the TEDINFO table"))
		return -1;
	for (k = 0; k < 3; k++)
		if (string_at(d, be32(d, off + 4 * (size_t)k), off + 4 * (size_t)k, &s[k]))
			return -1;
	if (charge(d, sizeof te) || charge_str(d, s[0]) || charge_str(d, s[1]) || charge_str(d, s[2]))
		return -1;
	if (s[0] && strlen(s[0]) >= 32767)
		return fail(d, RSC_ETOOBIG, off + 24, "TEDINFO text too long");
	if (s[1] && strlen(s[1]) >= 32767)
		return fail(d, RSC_ETOOBIG, off + 26, "TEDINFO template too long");
	te.te_ptext = (char *)s[0];
	te.te_ptmplt = (char *)s[1];
	te.te_pvalid = (char *)s[2];
	te.te_font = sbe16(d, off + 12);
	te.te_fontid = sbe16(d, off + 14);
	te.te_just = sbe16(d, off + 16);
	te.te_color = sbe16(d, off + 18);
	te.te_fontsize = sbe16(d, off + 20);
	te.te_thickness = sbe16(d, off + 22);
	te.te_txtlen = s[0] ? (short)(strlen(s[0]) + 1) : sbe16(d, off + 24);
	te.te_tmplen = s[1] ? (short)(strlen(s[1]) + 1) : sbe16(d, off + 26);
	return rsc_obj_set_tedinfo(d->r, t, i, &te) ? nomem(d) : 0;
}

/* The words fields of an ICONBLK at off (char .. htext) into *ib. */
static void iconblk_words(const struct rd *d, size_t off, ICONBLK *ib)
{
	ib->ib_char = sbe16(d, off + 12);
	ib->ib_xchar = sbe16(d, off + 14);
	ib->ib_ychar = sbe16(d, off + 16);
	ib->ib_xicon = sbe16(d, off + 18);
	ib->ib_yicon = sbe16(d, off + 20);
	ib->ib_wicon = sbe16(d, off + 22);
	ib->ib_hicon = sbe16(d, off + 24);
	ib->ib_xtext = sbe16(d, off + 26);
	ib->ib_ytext = sbe16(d, off + 28);
	ib->ib_wtext = sbe16(d, off + 30);
	ib->ib_htext = sbe16(d, off + 32);
}

static int set_iconblk(struct rd *d, int t, int i, uint32_t off, size_t field)
{
	ICONBLK	ib;
	const char	*s;
	uint32_t	pm, pd;
	size_t	n;
	int	rc = -1;

	if (off == FILE_NULL)
		return 0;
	if (record_in(d, &d->ib, off, field, "ICONBLK pointer outside the ICONBLK table")
	    || sizes_ok(d, off + 22, "negative icon size"))
		return -1;
	iconblk_words(d, off, &ib);
	n = rsc_plane_bytes(ib.ib_wicon, ib.ib_hicon);
	pm = be32(d, off);
	pd = be32(d, off + 4);
	if (data_ok(d, pm, n, off) || data_ok(d, pd, n, off + 4)
	    || string_at(d, be32(d, off + 8), off + 8, &s)
	    || charge(d, sizeof ib) || charge_data(d, pm, n) || charge_data(d, pd, n)
	    || charge_str(d, s))
		return -1;
	ib.ib_ptext = (char *)s;
	ib.ib_pdata = NULL;
	if (data_at(d, pm, n, &ib.ib_pmask) || data_at(d, pd, n, &ib.ib_pdata))
		goto out;
	if (rsc_obj_set_iconblk(d->r, t, i, &ib))
		nomem(d);
	else
		rc = 0;
out:
	rsc_mfree(ib.ib_pmask);
	rsc_mfree(ib.ib_pdata);
	return rc;
}

/* The BITBLK at off (inside the BITBLK table, checked) into *bb, its data
 * decoded into a block the caller frees. */
static int read_bitblk(struct rd *d, uint32_t off, BITBLK *bb)
{
	uint32_t	pd = be32(d, off);

	bb->bi_pdata = NULL;
	if (sizes_ok(d, off + 4, "negative image size"))
		return -1;
	bb->bi_wb = sbe16(d, off + 4);
	bb->bi_hl = sbe16(d, off + 6);
	bb->bi_x = sbe16(d, off + 8);
	bb->bi_y = sbe16(d, off + 10);
	bb->bi_color = sbe16(d, off + 12);
	if (data_ok(d, pd, rsc_bitblk_bytes(bb), off)
	    || charge(d, sizeof *bb) || charge_data(d, pd, rsc_bitblk_bytes(bb)))
		return -1;
	return data_at(d, pd, rsc_bitblk_bytes(bb), &bb->bi_pdata);
}

static int set_bitblk(struct rd *d, int t, int i, uint32_t off, size_t field)
{
	BITBLK	bb;
	int	rc = 0;

	if (off == FILE_NULL)
		return 0;
	if (record_in(d, &d->bb, off, field, "BITBLK pointer outside the BITBLK table")
	    || read_bitblk(d, off, &bb))
		return -1;
	if (rsc_obj_set_bitblk(d->r, t, i, &bb))
		rc = nomem(d);
	rsc_mfree(bb.bi_pdata);
	return rc;
}

/* Adds the colour version at *pos (checked by check_version) to c. */
static int add_version(struct rd *d, struct rsc_cicon *c, size_t *pos, size_t words)
{
	size_t	hdr = *pos, pb = words * 2;
	unsigned	planes = be16(d, hdr);
	int	sel = be32(d, hdr + 10) != 0, k, rc = -1;
	short	*p[4] = { NULL, NULL, NULL, NULL };
	struct rsc_cicon_version	*ver;

	*pos = hdr + CICON_SIZE;
	if (charge(d, sizeof *ver) || charge_data(d, 0, pb * planes) || charge_data(d, 0, pb)
	    || (sel && (charge_data(d, 0, pb * planes) || charge_data(d, 0, pb))))
		return -1;
	if (words_at(d, *pos, pb * planes, &p[0])
	    || words_at(d, *pos + pb * planes, pb, &p[1]))
		goto out;
	*pos += pb * (planes + 1);
	if (sel)
	{
		if (words_at(d, *pos, pb * planes, &p[2])
		    || words_at(d, *pos + pb * planes, pb, &p[3]))
			goto out;
		*pos += pb * (planes + 1);
	}
	if (rsc_cicon_add_version(c, (int)planes, p[0], p[1], p[2], p[3]) < 0)
	{
		nomem(d);
		goto out;
	}
	ver = c->last;		/* the version just added */
	for (k = 0; k < 5; k++)
		ver->file_ptr[k] = be32(d, hdr + 2 + 4 * (size_t)k);
	rc = 0;
out:
	for (k = 0; k < 4; k++)
		rsc_mfree(p[k]);
	return rc;
}

/* The colour icon of block off (checked by check_cblk) for object i. */
static int build_cicon(struct rd *d, int t, int i, size_t off)
{
	ICONBLK	mono;
	char	text[CTEXT_SIZE + 1];
	size_t	words = cblk_words(d, off), pos = off + CBLK_SIZE;
	uint32_t	count = be32(d, off + 34), v;
	struct rsc_cicon	*c;
	int	k, rc = -1;

	if (count > RSC_MAX_VERSIONS)	/* check_cblk refused it: a safety net */
		return fail(d, RSC_ECICON, off, "too many colour versions");
	iconblk_words(d, off, &mono);
	mono.ib_ptext = NULL;
	mono.ib_pmask = NULL;
	mono.ib_pdata = NULL;
	if (charge(d, sizeof *c) || charge_data(d, 0, words * 2) || charge_data(d, 0, words * 2))
		return -1;
	if (words_at(d, pos, words * 2, &mono.ib_pdata)
	    || words_at(d, pos + words * 2, words * 2, &mono.ib_pmask))
		goto out;
	pos += words * 4;
	memcpy(text, d->buf + pos, CTEXT_SIZE);
	text[CTEXT_SIZE] = '\0';
	pos += CTEXT_SIZE;
	if (rsc_obj_set_cicon(d->r, t, i, &mono, text))
	{
		nomem(d);
		goto out;
	}
	c = rsc_obj_cicon(d->r, t, i);
	for (k = 0; k < 3; k++)
		c->file_head[k] = be32(d, off + 4 * (size_t)k);
	for (v = 0; v < count; v++)
		if (add_version(d, c, &pos, words))
			goto out;
	rc = 0;
out:
	rsc_mfree(mono.ib_pdata);
	rsc_mfree(mono.ib_pmask);
	return rc;
}

static int set_cicon(struct rd *d, int t, int i, uint32_t index, size_t field)
{
	if (index == FILE_NULL)
		return 0;
	if (index >= d->ncicon)
		return fail(d, RSC_ECICON, field, "colour icon index out of range");
	return build_cicon(d, t, i, d->cblk[index]);
}

/* The spec of object i of tree t, whose file record is at rec. */
static int set_spec(struct rd *d, int t, int i, size_t rec)
{
	OBJECT	*o = rsc_tree_objects(d->r, t, NULL) + i;
	uint32_t	spec = be32(d, rec + 12);
	size_t	field = rec + 12;

	switch (rsc_obj_spec_kind(o))
	{
	case RSC_SPEC_STRING:
		return set_string(d, t, i, spec, field);
	case RSC_SPEC_TEDINFO:
		return set_tedinfo(d, t, i, spec, field);
	case RSC_SPEC_ICONBLK:
		return set_iconblk(d, t, i, spec, field);
	case RSC_SPEC_BITBLK:
		return set_bitblk(d, t, i, spec, field);
	case RSC_SPEC_CICON:
		return set_cicon(d, t, i, spec, field);
	case RSC_SPEC_VALUE:
		break;
	}
	if (rsc_obj_set_value(d->r, t, i, spec))
		return fail(d, RSC_EARG, rec, "object spec not set");
	return 0;
}

/* ---- trees ---------------------------------------------------------------------- */

/* A coordinate in file form converted with cell size cell (fix_chpos):
 * the low byte in cells plus the high byte in pixels, signed above 128. */
static short fix_coord(unsigned v, int cell)
{
	int	hi = (int)(v >> 8 & 0xff), px = (int)(v & 0xff) * cell;

	return (short)(hi > 128 ? px - (256 - hi) : px + hi);
}

static short coord(const struct rd *d, size_t off, int cell)
{
	return cell ? fix_coord(be16(d, off), cell) : sbe16(d, off);
}

/* The fields of object i of tree t but its spec from the record at rec. */
static void set_fields(struct rd *d, int t, int i, size_t rec)
{
	OBJECT	*o;

	/* these cannot fail: i is an object of the tree just created */
	rsc_obj_set_type(d->r, t, i, be16(d, rec + 6));
	rsc_obj_set_flags(d->r, t, i, be16(d, rec + 8));
	o = rsc_tree_objects(d->r, t, NULL) + i;
	o->ob_next = i ? sbe16(d, rec) : -1;
	o->ob_head = sbe16(d, rec + 2);
	o->ob_tail = sbe16(d, rec + 4);
	o->ob_state = (unsigned short)be16(d, rec + 10);
	o->ob_x = coord(d, rec + 16, d->cell_w);
	o->ob_y = coord(d, rec + 18, d->cell_h);
	o->ob_width = coord(d, rec + 20, d->cell_w);
	o->ob_height = coord(d, rec + 22, d->cell_h);
}

/* The walk of tree t (whose root is at root in the file) into order[]:
 * the links form an AES tree.  Returns the number of objects reached. */
static int walk_links(struct rd *d, int t, size_t root, int *order)
{
	int	n, cnt;
	OBJECT	*o = rsc_tree_objects(d->r, t, &n);

	if (charge(d, 2 * (size_t)n * sizeof(int)))
		return -1;
	cnt = rsc_tree_preorder(o, n, order);
	switch (cnt)
	{
	case RSC_WALK_ENOMEM:
		return nomem(d);
	case RSC_WALK_ESHARED:
		return fail(d, RSC_ECYCLE, root, "tree links form a cycle or share an object");
	case RSC_WALK_EARG:
	case RSC_WALK_ERANGE:
		return fail(d, RSC_ELINK, root, "tree link out of range");
	case RSC_WALK_ECHAIN:
		return fail(d, RSC_ELINK, root, "malformed sibling chain in tree");
	default:
		return cnt;
	}
}

/* The index in the object table of the root of tree k (checked). */
static int root_index(struct rd *d, size_t k, size_t *s)
{
	size_t	field = d->h[H_TRINDEX] + 4 * k;
	uint32_t	off = be32(d, field);

	if (record_in(d, &d->obj, off, field, "tree root outside the object table"))
		return -1;
	*s = (off - d->obj.off) / OBJ_SIZE;
	return 0;
}

/* Marks the roots of all trees in isroot[] (one byte per object of the
 * table); a root listed twice is an error. */
static int mark_roots(struct rd *d, unsigned char *isroot)
{
	size_t	k, s;

	for (k = 0; k < d->h[H_NTREE]; k++)
	{
		if (root_index(d, k, &s))
			return -1;
		if (isroot[s])
			return fail(d, RSC_ELINK, d->h[H_TRINDEX] + 4 * k, "trees overlap");
		isroot[s] = 1;
	}
	return 0;
}

/* The decoded tree of n objects starting with the record at root, whose
 * tree index entry is at field. */
static int build_tree(struct rd *d, size_t root, size_t n, size_t field)
{
	int	t, i, cnt, *order, rc = -1;

	d->at = field;
	if (n > RSC_MAX_OBJECTS)
		return fail(d, RSC_ETOOBIG, field, "tree with too many objects");
	if (charge(d, n * sizeof(OBJECT) + 2 * sizeof(void *)) || charge(d, n * sizeof(int)))
		return -1;
	t = rsc_tree_add(d->r, (int)n);
	if (t < 0)
		return nomem(d);
	for (i = 0; (size_t)i < n; i++)
		set_fields(d, t, i, root + (size_t)i * OBJ_SIZE);
	order = rsc_malloc(n * sizeof *order);
	if (!order)
		return nomem(d);
	cnt = walk_links(d, t, root, order);
	if (cnt < 0)
		goto out;
	/* only the reachable objects get their specs */
	for (i = 0; i < cnt; i++)
	{
		d->at = root + (size_t)order[i] * OBJ_SIZE;
		if (set_spec(d, t, order[i], d->at))
			goto out;
	}
	rc = 0;
out:
	rsc_mfree(order);
	return rc;
}

/* Tree k of the tree index: its run ends at OF_LASTOB, before the next
 * root, or at the end of the table. */
static int read_tree(struct rd *d, size_t k, const unsigned char *isroot)
{
	size_t	s, e;

	if (root_index(d, k, &s))
		return -1;
	for (e = s; e + 1 < d->obj.n && !isroot[e + 1]; e++)
		if (be16(d, d->obj.off + e * OBJ_SIZE + 8) & OF_LASTOB)
			break;
	return build_tree(d, d->obj.off + s * OBJ_SIZE, e - s + 1, d->h[H_TRINDEX] + 4 * k);
}

/* All trees of the tree index. */
static int read_trees(struct rd *d)
{
	unsigned char	*isroot;
	size_t	k;
	int	rc = 0;

	if (d->h[H_NTREE] == 0)
		return 0;
	d->at = 2 * H_TRINDEX;
	if (charge(d, d->obj.n))
		return -1;
	isroot = rsc_malloc(d->obj.n);
	if (!isroot)
		return nomem(d);
	memset(isroot, 0, d->obj.n);
	rc = mark_roots(d, isroot);
	for (k = 0; !rc && k < d->h[H_NTREE]; k++)
		rc = read_tree(d, k, isroot);
	rsc_mfree(isroot);
	return rc;
}

/* ---- free strings and free images ---------------------------------------------- */

static int read_frstr(struct rd *d)
{
	size_t	k, field;
	const char	*s;

	for (k = 0; k < d->h[H_NSTRING]; k++)
	{
		field = d->h[H_FRSTR] + 4 * k;
		d->at = field;
		if (string_at(d, be32(d, field), field, &s)
		    || (s && (charge_str(d, s) || charge(d, sizeof(char *)))))
			return -1;
		if (s && rsc_frstr_add(d->r, s) < 0)
			return nomem(d);
	}
	return 0;
}

static int read_frimg(struct rd *d)
{
	size_t	k, field;
	uint32_t	off;
	BITBLK	bb;
	int	rc;

	for (k = 0; k < d->h[H_NIMAGES]; k++)
	{
		field = d->h[H_FRIMG] + 4 * k;
		d->at = field;
		off = be32(d, field);
		if (off == FILE_NULL)
			continue;
		if (record_in(d, &d->bb, off, field, "free image outside the BITBLK table")
		    || charge(d, sizeof(BITBLK *)) || read_bitblk(d, off, &bb))
			return -1;
		rc = rsc_frimg_add(d->r, &bb) < 0 ? nomem(d) : 0;
		rsc_mfree(bb.bi_pdata);
		if (rc)
			return -1;
	}
	return 0;
}

/* ---- the whole file -------------------------------------------------------------- */

static int read_all(struct rd *d)
{
	return header(d) || cicon_ext(d) || read_trees(d) || read_frstr(d) || read_frimg(d)
	       ? -1 : 0;
}

struct rsc *rsc_atari_read(const uint8_t *buf, size_t len, int cell_w, int cell_h,
			   struct rsc_error *err)
{
	struct rd	d;

	memset(&d, 0, sizeof d);
	d.buf = buf;
	d.len = len;
	d.cell_w = cell_w;
	d.cell_h = cell_h;
	d.err = err;
	d.budget = len > ((size_t)-1 - 65536) / 4 ? (size_t)-1 : 4 * len + 65536;
	if (err)
	{
		err->code = RSC_OK;
		err->offset = 0;
		err->msg[0] = '\0';
	}
	if (!buf || cell_w < 0 || cell_h < 0 || cell_w > RSC_MAX_CELL || cell_h > RSC_MAX_CELL
	    || (cell_w == 0) != (cell_h == 0))
	{
		fail(&d, RSC_EARG, 0, "bad argument");
		return NULL;
	}
	d.r = rsc_new();
	if (!d.r)
	{
		nomem(&d);
		return NULL;
	}
	if (rsc_set_cell(d.r, cell_w, cell_h) || read_all(&d))
	{
		if (err && err->code == RSC_OK)
			fail(&d, RSC_EARG, 0, "bad argument");
		rsc_mfree(d.cblk);
		rsc_free(d.r);
		return NULL;
	}
	rsc_mfree(d.cblk);
	return d.r;
}
