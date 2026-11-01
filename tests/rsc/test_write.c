/* TEST_WRITE.C	host tests of the classic Atari writer (rscatari_write.c):
 *		make rsc-test
 *
 * The acceptance test: the files of tests/golden (the directory may be
 * given as the first argument) read and written again must equal the
 * matching .out.rsc byte for byte, with the cell size 8x16 and in file
 * form.  Then the stability of write and read, resources built through the
 * editing API (checked against hand-computed bytes and read back), the
 * errors and limits, failed allocations, colour icons and a deterministic
 * mini-fuzz of edits followed by a write and a read.  When the environment
 * variable RSC_TEST_EXTERNAL names a directory, every *.rsc in it is read,
 * written, read back and written again too (CI does not set it: real
 * files are not part of the tree). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "rscmodel.h"
#include "rscfile.h"

/* ---- a tiny assertion framework -------------------------------------------- */

static long checks, failures;

#define CHECK(c)							\
	do								\
	{								\
		checks++;						\
		if (!(c))						\
		{							\
			failures++;					\
			fprintf(stderr, "%s:%d: CHECK failed: %s\n",	\
				__FILE__, __LINE__, #c);		\
		}							\
	} while (0)

/* ---- files and bytes ---------------------------------------------------------- */

static const char	*golden_dir = "tests/golden";

struct file
{
	uint8_t	*b;
	size_t	len;
};

static struct file load_path(const char *path)
{
	struct file	f = { NULL, 0 };
	FILE	*fp;
	long	n;

	fp = fopen(path, "rb");
	if (!fp)
	{
		fprintf(stderr, "cannot open %s\n", path);
		exit(2);
	}
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	f.b = malloc((size_t)n);
	if (!f.b || fread(f.b, 1, (size_t)n, fp) != (size_t)n)
		exit(2);
	fclose(fp);
	f.len = (size_t)n;
	return f;
}

static struct file load(const char *name)
{
	char	path[512];

	snprintf(path, sizeof path, "%s/%s", golden_dir, name);
	return load_path(path);
}

static unsigned rd16(const uint8_t *b, size_t off)
{
	return (unsigned)b[off] << 8 | b[off + 1];
}

static uint32_t rd32(const uint8_t *b, size_t off)
{
	return (uint32_t)rd16(b, off) << 16 | rd16(b, off + 2);
}

#define FILE_NULL	0xffffffffUL

enum
{
	H_VRSN, H_OBJECT, H_TEDINFO, H_ICONBLK, H_BITBLK, H_FRSTR, H_STRING,
	H_IMDATA, H_FRIMG, H_TRINDEX, H_NOBS, H_NTREE, H_NTED, H_NIB, H_NBB,
	H_NSTRING, H_NIMAGES, H_RSSIZE
};

static unsigned hw(const uint8_t *b, int k)
{
	return rd16(b, 2 * (size_t)k);
}

/* The section of a file (by its header) that offset off is in. */
static const char *section(const uint8_t *b, size_t len, size_t off)
{
	static const struct
	{
		int	h;
		const char	*name;
	} s[] = {
		{ H_STRING, "strings" }, { H_IMDATA, "image data" }, { H_FRSTR, "free string table" },
		{ H_BITBLK, "BITBLKs" }, { H_FRIMG, "free image table" }, { H_ICONBLK, "ICONBLKs" },
		{ H_TEDINFO, "TEDINFOs" }, { H_OBJECT, "OBJECTs" }, { H_TRINDEX, "tree index" },
		{ H_RSSIZE, "colour icon extension" }
	};
	const char	*best = "header";
	unsigned	at = 0;
	size_t	k;

	if (len < 36)
		return "header";
	for (k = 0; k < sizeof s / sizeof s[0]; k++)
		if (hw(b, s[k].h) && hw(b, s[k].h) <= off && hw(b, s[k].h) >= at)
		{
			at = hw(b, s[k].h);
			best = s[k].name;
		}
	return off < 36 ? "header" : best;
}

/* Reports the first difference of a and b; 1 when they are equal. */
static int same_bytes(const char *what, const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
	size_t	k, n = alen < blen ? alen : blen;

	for (k = 0; k < n && a[k] == b[k]; k++)
		;
	if (k == n && alen == blen)
		return 1;
	fprintf(stderr, "%s: lengths %zu and %zu, first difference at 0x%zx (%s)\n",
		what, alen, blen, k, section(b, blen, k));
	return 0;
}

static struct rsc *rd(const uint8_t *b, size_t len, int cw, int ch, struct rsc_error *err);

static int wr(const struct rsc *r, int cw, int ch, uint8_t **out, size_t *len, struct rsc_error *e)
{
	return rsc_atari_write(r, cw, ch, out, len, e);
}

/* Reads from an exactly sized copy (the sanitizer sees reads past it). */
static struct rsc *rd(const uint8_t *b, size_t len, int cw, int ch, struct rsc_error *err)
{
	uint8_t	*copy = malloc(len ? len : 1);
	struct rsc	*r;

	memcpy(copy, b, len);
	r = rsc_atari_read(copy, len, cw, ch, err);
	free(copy);
	return r;
}

/* The bytes o of a written model read back and written again give the same
 * bytes (the model keeps te_txtlen and te_tmplen as the reader makes them,
 * so this holds for a model built through the API too). */
static int fixed_point(const char *what, const uint8_t *o, size_t len, int cw, int ch)
{
	struct rsc_error	e;
	struct rsc	*r = rd(o, len, cw, ch, &e);
	uint8_t	*o2 = NULL;
	size_t	len2;
	int	ok;

	if (!r)
	{
		fprintf(stderr, "%s: %s\n", what, e.msg);
		return 0;
	}
	ok = wr(r, cw, ch, &o2, &len2, &e) == 0 && same_bytes(what, o2, len2, o, len);
	rsc_mfree(o2);
	rsc_free(r);
	return ok;
}

/* The colour icon table of the written file o (when it has one) as the
 * AES and EmuTOS read it (pTOS's aes/rsload_legacy.c): as many blocks as
 * entries before the terminator, the first right after it, each next
 * right after the previous, each as long as its own head says.  The
 * writer's entries must be exactly those offsets (so that a loader that
 * takes them as offsets, like pTOS's portable one, reads the same), and
 * the last block must end the file, the length in the extension.  1 when
 * all that holds. */
static int sequential_table(const uint8_t *o, size_t len)
{
	size_t	ext = hw(o, H_RSSIZE), tab, n, k, pos, words;
	uint32_t	nv, v;

	if (!(hw(o, H_VRSN) & RSC_VRSN_FORMAT_BITS))
		return 1;
	if (ext + 12 > len || rd32(o, ext) != len)
		return 0;
	tab = rd32(o, ext + 4);
	for (n = 0; tab + 4 * n + 4 <= len && rd32(o, tab + 4 * n) != FILE_NULL; n++)
		;
	pos = tab + 4 * n + 4;
	for (k = 0; k < n; k++)
	{
		if (pos + 38 > len || rd32(o, tab + 4 * k) != pos)
			return 0;
		words = rsc_plane_bytes((short)rd16(o, pos + 22), (short)rd16(o, pos + 24)) / 2;
		nv = rd32(o, pos + 34);
		pos += 38 + 4 * words + 12;
		for (v = 0; v < nv && pos + 22 <= len; v++)
			pos += 22 + 2 * words * (rd16(o, pos) + 1) * (rd32(o, pos + 10) ? 2 : 1);
	}
	return pos == len;
}

/* ---- comparing models ------------------------------------------------------------ */

static int why(const char *what, int t, int k)
{
	fprintf(stderr, "models differ: %s (tree %d, object %d)\n", what, t, k);
	return 0;
}

static int str_eq(const char *a, const char *b)
{
	return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

/* n bytes of image data, NULL being zeros */
static int data_eq(const void *a, const void *b, size_t n)
{
	const unsigned char	*p = a, *q = b;
	size_t	k;

	for (k = 0; k < n; k++)
		if ((p ? p[k] : 0) != (q ? q[k] : 0))
			return 0;
	return 1;
}

static int ib_words_eq(const ICONBLK *a, const ICONBLK *b)
{
	return a->ib_char == b->ib_char && a->ib_xchar == b->ib_xchar && a->ib_ychar == b->ib_ychar
	       && a->ib_xicon == b->ib_xicon && a->ib_yicon == b->ib_yicon
	       && a->ib_wicon == b->ib_wicon && a->ib_hicon == b->ib_hicon
	       && a->ib_xtext == b->ib_xtext && a->ib_ytext == b->ib_ytext
	       && a->ib_wtext == b->ib_wtext && a->ib_htext == b->ib_htext;
}

static int bb_eq(const BITBLK *a, const BITBLK *b)
{
	return a->bi_wb == b->bi_wb && a->bi_hl == b->bi_hl && a->bi_x == b->bi_x
	       && a->bi_y == b->bi_y && a->bi_color == b->bi_color
	       && data_eq(a->bi_pdata, b->bi_pdata, rsc_bitblk_bytes(a));
}

static int cicon_eq(const struct rsc_cicon *a, const struct rsc_cicon *b)
{
	const ICONBLK	*ma = &a->blk.monoblk, *mb = &b->blk.monoblk;
	size_t	pb = rsc_plane_bytes(ma->ib_wicon, ma->ib_hicon);
	int	v, n = rsc_cicon_nversions(a);

	if (!ib_words_eq(ma, mb) || !data_eq(ma->ib_pdata, mb->ib_pdata, pb)
	    || !data_eq(ma->ib_pmask, mb->ib_pmask, pb) || memcmp(a->text, b->text, 12) != 0
	    || memcmp(a->file_head, b->file_head, sizeof a->file_head) != 0
	    || n != rsc_cicon_nversions(b) || strcmp(mb->ib_ptext, b->text) != 0)
		return 0;
	for (v = 0; v < n; v++)
	{
		const struct rsc_cicon_version	*x = rsc_cicon_version(a, v), *y = rsc_cicon_version(b, v);
		int	sel = x->c.sel_data || x->c.sel_mask;
		size_t	np = (size_t)x->c.num_planes;

		if (x->c.num_planes != y->c.num_planes || !data_eq(x->c.col_data, y->c.col_data, pb * np)
		    || !data_eq(x->c.col_mask, y->c.col_mask, pb) || sel != (y->c.sel_data != NULL)
		    || (sel && (!data_eq(x->c.sel_data, y->c.sel_data, pb * np)
				|| !data_eq(x->c.sel_mask, y->c.sel_mask, pb))))
			return 0;
		/* col_data, col_mask, sel_mask: opaque, as stored */
		if (y->file_ptr[0] != x->file_ptr[0] || y->file_ptr[1] != x->file_ptr[1]
		    || y->file_ptr[3] != x->file_ptr[3])
			return 0;
		/* sel_data: non-zero exactly when the selected planes exist, and
		 * then the stored value if there was one */
		if ((y->file_ptr[2] != 0) != sel || (sel && x->file_ptr[2] && y->file_ptr[2] != x->file_ptr[2]))
			return 0;
		/* next_res: 1 when another version follows, 0 on the last */
		if (y->file_ptr[4] != (rsc_cicon_version(a, v + 1) ? 1U : 0U))
			return 0;
	}
	return 1;
}

/* The spec of a (written) and b (read back) are the same. */
static int spec_eq(const OBJECT *a, const OBJECT *b)
{
	const TEDINFO	*ta, *tb;
	const ICONBLK	*ia, *ib;

	switch (rsc_obj_spec_kind(a))
	{
	case RSC_SPEC_VALUE:
		return (uint32_t)a->ob_spec.index == (uint32_t)b->ob_spec.index;
	case RSC_SPEC_STRING:
		return str_eq(a->ob_spec.free_string, b->ob_spec.free_string);
	case RSC_SPEC_TEDINFO:
		ta = a->ob_spec.tedinfo;
		tb = b->ob_spec.tedinfo;
		if (!ta || !tb)
			return !ta && !tb;
		return str_eq(ta->te_ptext, tb->te_ptext) && str_eq(ta->te_ptmplt, tb->te_ptmplt)
		       && str_eq(ta->te_pvalid, tb->te_pvalid) && ta->te_font == tb->te_font
		       && ta->te_fontid == tb->te_fontid && ta->te_just == tb->te_just
		       && ta->te_color == tb->te_color && ta->te_fontsize == tb->te_fontsize
		       && ta->te_thickness == tb->te_thickness
		       && tb->te_txtlen == (ta->te_ptext ? (short)(strlen(ta->te_ptext) + 1) : ta->te_txtlen)
		       && tb->te_tmplen == (ta->te_ptmplt ? (short)(strlen(ta->te_ptmplt) + 1) : ta->te_tmplen);
	case RSC_SPEC_ICONBLK:
		ia = a->ob_spec.iconblk;
		ib = b->ob_spec.iconblk;
		if (!ia || !ib)
			return !ia && !ib;
		return ib_words_eq(ia, ib) && str_eq(ia->ib_ptext, ib->ib_ptext)
		       && data_eq(ia->ib_pmask, ib->ib_pmask, rsc_plane_bytes(ia->ib_wicon, ia->ib_hicon))
		       && data_eq(ia->ib_pdata, ib->ib_pdata, rsc_plane_bytes(ia->ib_wicon, ia->ib_hicon));
	case RSC_SPEC_BITBLK:
		if (!a->ob_spec.bitblk || !b->ob_spec.bitblk)
			return !a->ob_spec.bitblk && !b->ob_spec.bitblk;
		return bb_eq(a->ob_spec.bitblk, b->ob_spec.bitblk);
	case RSC_SPEC_CICON:
		if (!a->ob_spec.ciconblk || !b->ob_spec.ciconblk)
			return !a->ob_spec.ciconblk && !b->ob_spec.ciconblk;
		return cicon_eq((const struct rsc_cicon *)(void *)a->ob_spec.ciconblk,
				(const struct rsc_cicon *)(void *)b->ob_spec.ciconblk);
	}
	return 0;
}

static int link_of(int l, const int *pos)
{
	return l == -1 ? -1 : pos[l];
}

/* b is a written and read back: the logical content is that of a (the
 * objects a root reaches, in preorder, renumbered). */
static int same_model(const struct rsc *a, const struct rsc *b)
{
	int	t, k, na, nb, ca, *oa, *pos, ok = 1, cwa, cha, cwb, chb;
	const OBJECT	*A, *B;

	rsc_cell(a, &cwa, &cha);
	rsc_cell(b, &cwb, &chb);
	if (rsc_vrsn(a) != rsc_vrsn(b) || cwa != cwb || cha != chb)
		return why("vrsn or cell", -1, -1);
	if (rsc_ntrees(a) != rsc_ntrees(b))
		return why("number of trees", -1, -1);
	for (t = 0; ok && t < rsc_ntrees(a); t++)
	{
		A = rsc_tree_objects(a, t, &na);
		B = rsc_tree_objects(b, t, &nb);
		oa = malloc((size_t)na * sizeof *oa);
		pos = malloc((size_t)na * sizeof *pos);
		ca = rsc_tree_preorder(A, na, oa);
		if (ca != nb || rsc_tree_preorder(B, nb, NULL) != nb)
			ok = why("object count", t, -1);
		for (k = 0; k < na; k++)
			pos[k] = -1;
		for (k = 0; ok && k < ca; k++)
			pos[oa[k]] = k;
		for (k = 0; ok && k < ca; k++)
		{
			const OBJECT	*x = &A[oa[k]], *y = &B[k];
			unsigned	type = x->ob_type;

			if ((type & 0xff) == G_ICON || (type & 0xff) == G_IMAGE || (type & 0xff) == G_CICON)
				type &= 0xff;
			if (y->ob_next != (k ? link_of(x->ob_next, pos) : -1)
			    || y->ob_head != link_of(x->ob_head, pos) || y->ob_tail != link_of(x->ob_tail, pos))
				ok = why("links", t, k);
			else if (y->ob_type != type || (y->ob_flags & ~OF_LASTOB) != (x->ob_flags & ~OF_LASTOB)
				 || y->ob_state != x->ob_state)
				ok = why("type, flags or state", t, k);
			else if (x->ob_x != y->ob_x || x->ob_y != y->ob_y || x->ob_width != y->ob_width
				 || x->ob_height != y->ob_height)
				ok = why("geometry", t, k);
			else if (!spec_eq(x, y))
				ok = why("spec", t, k);
		}
		free(oa);
		free(pos);
	}
	if (!ok)
		return 0;
	if (rsc_nfrstr(a) != rsc_nfrstr(b) || rsc_nfrimg(a) != rsc_nfrimg(b))
		return why("free list lengths", -1, -1);
	for (k = 0; k < rsc_nfrstr(a); k++)
		if (!str_eq(rsc_frstr(a, k), rsc_frstr(b, k)))
			return why("free string", -1, k);
	for (k = 0; k < rsc_nfrimg(a); k++)
		if (!bb_eq(rsc_frimg(a, k), rsc_frimg(b, k)))
			return why("free image", -1, k);
	return 1;
}

/* ---- a deep hash of a model (everything, in array order, pointers too) --- */

static uint64_t hv;

static void hb(const void *p, size_t n)
{
	const unsigned char	*b = p;

	while (n--)
		hv = (hv ^ *b++) * 1099511628211ULL;
}

static void hp(const void *p)
{
	hb(&p, sizeof p);
}

static void hs(const char *s)
{
	hp(s);
	if (s)
		hb(s, strlen(s) + 1);
}

static void hd(const void *p, size_t n)
{
	hp(p);
	if (p)
		hb(p, n);
}

static void h_ib(const ICONBLK *ib)
{
	size_t	n = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);

	hb(&ib->ib_char, sizeof(short) * 11);
	hd(ib->ib_pmask, n);
	hd(ib->ib_pdata, n);
	hs(ib->ib_ptext);
}

static void h_bb(const BITBLK *bb)
{
	hb(&bb->bi_wb, sizeof(short) * 5);
	hd(bb->bi_pdata, rsc_bitblk_bytes(bb));
}

static uint64_t model_hash(const struct rsc *r)
{
	int	t, i, n, cw, ch;
	unsigned	v = rsc_vrsn(r);

	hv = 14695981039346656037ULL;
	rsc_cell(r, &cw, &ch);
	hb(&v, sizeof v);
	hb(&cw, sizeof cw);
	hb(&ch, sizeof ch);
	for (t = 0; t < rsc_ntrees(r); t++)
	{
		const OBJECT	*o = rsc_tree_objects(r, t, &n);

		hp(o);
		for (i = 0; i < n; i++, o++)
		{
			hb(&o->ob_next, 6 * sizeof(short));
			hb(&o->ob_x, 4 * sizeof(short));
			switch (rsc_obj_spec_kind(o))
			{
			case RSC_SPEC_VALUE:
				hb(&o->ob_spec.index, sizeof o->ob_spec.index);
				break;
			case RSC_SPEC_STRING:
				hs(o->ob_spec.free_string);
				break;
			case RSC_SPEC_TEDINFO:
				hp(o->ob_spec.tedinfo);
				if (o->ob_spec.tedinfo)
				{
					hs(o->ob_spec.tedinfo->te_ptext);
					hs(o->ob_spec.tedinfo->te_ptmplt);
					hs(o->ob_spec.tedinfo->te_pvalid);
					hb(&o->ob_spec.tedinfo->te_font, 8 * sizeof(short));
				}
				break;
			case RSC_SPEC_ICONBLK:
				hp(o->ob_spec.iconblk);
				if (o->ob_spec.iconblk)
					h_ib(o->ob_spec.iconblk);
				break;
			case RSC_SPEC_BITBLK:
				hp(o->ob_spec.bitblk);
				if (o->ob_spec.bitblk)
					h_bb(o->ob_spec.bitblk);
				break;
			case RSC_SPEC_CICON:
				hp(o->ob_spec.ciconblk);
				if (o->ob_spec.ciconblk)
				{
					const struct rsc_cicon	*c = (const void *)o->ob_spec.ciconblk;
					const ICONBLK	*m = &c->blk.monoblk;
					size_t	pb = rsc_plane_bytes(m->ib_wicon, m->ib_hicon);
					const CICON	*p;

					h_ib(m);
					hb(c->text, sizeof c->text);
					hb(c->file_head, sizeof c->file_head);
					for (p = c->blk.mainlist; p; p = p->next_res)
					{
						const struct rsc_cicon_version	*x = (const void *)p;
						size_t	np = (size_t)p->num_planes;

						hb(&p->num_planes, sizeof p->num_planes);
						hd(p->col_data, pb * np);
						hd(p->col_mask, pb);
						hd(p->sel_data, pb * np);
						hd(p->sel_mask, pb);
						hb(x->file_ptr, sizeof x->file_ptr);
					}
				}
				break;
			}
		}
	}
	for (i = 0; i < rsc_nfrstr(r); i++)
		hs(rsc_frstr(r, i));
	for (i = 0; i < rsc_nfrimg(r); i++)
	{
		hp(rsc_frimg(r, i));
		h_bb(rsc_frimg(r, i));
	}
	return hv;
}

/* ---- building resources ------------------------------------------------------------ */

static short pat[8192];

static void init_pat(void)
{
	size_t	k;

	for (k = 0; k < sizeof pat / sizeof pat[0]; k++)
		pat[k] = (short)(k * 2654435761U >> 7);
}

static int set_tedinfo(struct rsc *r, int t, int i, const char *a, const char *b, const char *c)
{
	TEDINFO	te;

	memset(&te, 0, sizeof te);
	te.te_ptext = (char *)a;
	te.te_ptmplt = (char *)b;
	te.te_pvalid = (char *)c;
	te.te_font = 3;
	te.te_fontid = 1;
	te.te_just = 2;
	te.te_color = 0x1180;
	te.te_fontsize = 13;
	te.te_thickness = -1;
	te.te_txtlen = 77;
	te.te_tmplen = 88;
	return rsc_obj_set_tedinfo(r, t, i, &te);
}

static void set_icon(struct rsc *r, int t, int i, int w, int h, const short *m, const short *d,
		     const char *text)
{
	ICONBLK	ib;

	memset(&ib, 0, sizeof ib);
	ib.ib_pmask = (short *)m;
	ib.ib_pdata = (short *)d;
	ib.ib_ptext = (char *)text;
	ib.ib_char = 0x1041;
	ib.ib_xchar = 3;
	ib.ib_ychar = 4;
	ib.ib_xicon = 5;
	ib.ib_wicon = (short)w;
	ib.ib_hicon = (short)h;
	ib.ib_ytext = (short)h;
	ib.ib_wtext = 48;
	ib.ib_htext = 8;
	CHECK(rsc_obj_set_iconblk(r, t, i, &ib) == 0);
}

static void set_image(struct rsc *r, int t, int i, int wb, int hl, const short *d)
{
	BITBLK	bb = { (short *)d, (short)wb, (short)hl, 1, 2, 3 };

	CHECK(rsc_obj_set_bitblk(r, t, i, &bb) == 0);
}

static struct rsc_cicon *set_cicon(struct rsc *r, int t, int i, int w, int h, const short *m,
				   const short *d, const char *text)
{
	ICONBLK	ib;

	memset(&ib, 0, sizeof ib);
	ib.ib_pmask = (short *)m;
	ib.ib_pdata = (short *)d;
	ib.ib_wicon = (short)w;
	ib.ib_hicon = (short)h;
	ib.ib_xtext = -2;
	ib.ib_ytext = (short)h;
	ib.ib_wtext = 40;
	ib.ib_htext = 6;
	CHECK(rsc_obj_set_cicon(r, t, i, &ib, text) == 0);
	return rsc_obj_cicon(r, t, i);
}

/* Everything the model can hold: all object kinds, objects added so that
 * the index order is not the preorder, deleted (unreachable) objects, NULL
 * strings, records and data, free strings and images, colour icons. */
static struct rsc *build_sample(void)
{
	struct rsc	*r = rsc_new();
	struct rsc_cicon	*c;
	BITBLK	bb1 = { pat, 4, 3, 0, 0, 1 }, bb2 = { NULL, 4, 2, 7, 8, 9 };
	int	t, a, b, x, k;

	rsc_set_vrsn(r, 1);
	/* tree 0 */
	t = rsc_tree_add(r, 1);
	CHECK(rsc_obj_set_type(r, t, 0, G_BOX) == 0);
	CHECK(rsc_obj_set_value(r, t, 0, 0x00ff1100) == 0);
	CHECK(rsc_obj_set_geom(r, t, 0, 0, 0, 40, 20) == 0);
	a = rsc_obj_add(r, t, 0, G_IBOX);				/* 1 */
	b = rsc_obj_add(r, t, 0, G_STRING);				/* 2 */
	CHECK(rsc_obj_set_str(r, t, b, RSC_STR_SPEC, "Hello") == 0);
	CHECK(rsc_obj_set_flags(r, t, b, OF_SELECTABLE | OF_EXIT) == 0);
	k = rsc_obj_add(r, t, a, G_FTEXT);				/* 3 */
	CHECK(set_tedinfo(r, t, k, "@____", "Name: ____", "XXXX") == 0);
	CHECK(rsc_obj_set_state(r, t, k, OS_SELECTED | OS_SHADOWED) == 0);
	CHECK(rsc_obj_set_geom(r, t, k, 1, 2, 0x0310, -5) == 0);
	k = rsc_obj_add(r, t, a, G_ICON);				/* 4 */
	set_icon(r, t, k, 24, 3, pat, pat + 100, "ICON");
	CHECK(rsc_obj_set_type(r, t, k, G_ICON | 0x1200) == 0);	/* high byte dropped */
	x = rsc_obj_add(r, t, b, G_BUTTON);				/* 5, deleted below */
	CHECK(rsc_obj_set_str(r, t, x, RSC_STR_SPEC, "gone") == 0);
	k = rsc_obj_add(r, t, 0, G_IMAGE);				/* 6 */
	set_image(r, t, k, 2, 3, pat + 7);
	k = rsc_obj_add(r, t, a, G_CICON);				/* 7 */
	c = set_cicon(r, t, k, 16, 2, pat + 300, pat + 400, "CI");
	c->file_head[0] = 0x11111111;
	c->file_head[1] = 0x22222222;
	c->file_head[2] = 0x33333333;
	CHECK(rsc_cicon_add_version(c, 4, pat + 500, pat + 600, NULL, NULL) == 0);
	CHECK(rsc_cicon_add_version(c, 2, pat + 700, pat + 800, pat + 900, pat + 1000) == 1);
	rsc_cicon_version(c, 0)->file_ptr[0] = 0xabcdef01;
	rsc_cicon_version(c, 1)->file_ptr[3] = 0x12345678;
	k = rsc_obj_add(r, t, 0, G_TITLE);				/* 8 */
	CHECK(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "Title") == 0);
	k = rsc_obj_add(r, t, x, G_STRING);				/* 9, deleted with 5 */
	CHECK(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "child of gone") == 0);
	CHECK(rsc_obj_delete(r, t, x) == 0);
	k = rsc_obj_add(r, t, 0, G_BUTTON);				/* 10 */
	CHECK(rsc_obj_set_flags(r, t, k, OF_INDIRECT) == 0);
	CHECK(rsc_obj_set_value(r, t, k, 0x12345678) == 0);
	k = rsc_obj_add(r, t, 0, G_BOXCHAR);				/* 11 */
	CHECK(rsc_obj_set_value(r, t, k, 0x41001172) == 0);
	CHECK(rsc_obj_set_type(r, t, k, G_BOXCHAR | 0x5500) == 0);	/* high byte kept */
	/* tree 1: NULL strings, records and data */
	t = rsc_tree_add(r, 1);
	CHECK(rsc_obj_set_type(r, t, 0, G_FBOXTEXT) == 0);
	CHECK(set_tedinfo(r, t, 0, NULL, NULL, NULL) == 0);
	k = rsc_obj_add(r, t, 0, G_BUTTON);
	k = rsc_obj_add(r, t, 0, G_ICON);
	set_icon(r, t, k, 17, 2, NULL, NULL, NULL);
	k = rsc_obj_add(r, t, 0, G_IMAGE);
	set_image(r, t, k, 3, 3, NULL);
	k = rsc_obj_add(r, t, 0, G_IMAGE);
	k = rsc_obj_add(r, t, 0, G_TEXT);
	k = rsc_obj_add(r, t, 0, G_CICON);
	k = rsc_obj_add(r, t, 0, G_CICON);
	set_cicon(r, t, k, 32, 1, NULL, NULL, "twelve chars");
	/* tree 2: a root alone */
	t = rsc_tree_add(r, 1);
	CHECK(rsc_obj_set_geom(r, t, 0, -1, 0x7fff, 3, 4) == 0);
	CHECK(rsc_frstr_add(r, "free one") == 0);
	CHECK(rsc_frstr_add(r, "[1][An alert][OK]") == 1);
	CHECK(rsc_frimg_add(r, &bb1) == 0);
	CHECK(rsc_frimg_add(r, &bb2) == 1);
	return r;
}

/* ---- (a) the goldens, (b) stability --------------------------------------------------- */

static void test_golden(const char *name, int cw, int ch)
{
	char	in[64], outn[64], what[96];
	struct file	f, g;
	struct rsc	*r, *r2;
	struct rsc_error	e;
	uint8_t	*o1 = NULL, *o2 = NULL, *o3 = NULL;
	size_t	l1, l2, l3;
	uint64_t	h;

	snprintf(in, sizeof in, "%s.rsc", name);
	snprintf(outn, sizeof outn, "%s.out.rsc", name);
	snprintf(what, sizeof what, "%s cell %dx%d", name, cw, ch);
	f = load(in);
	g = load(outn);
	r = rd(f.b, f.len, cw, ch, &e);
	CHECK(r != NULL);
	if (!r)
	{
		fprintf(stderr, "%s: %s\n", what, e.msg);
		return;
	}
	h = model_hash(r);
	/* no image block of an odd size, so the pads of odd blocks cannot
	 * have changed the output (the bytes below would show it anyway) */
	{
		OBJECT	*ob;
		int	t, k, n, odd = 0;

		for (t = 0; t < rsc_ntrees(r); t++)
			for (ob = rsc_tree_objects(r, t, &n), k = 0; k < n; k++)
				odd += rsc_obj_spec_kind(&ob[k]) == RSC_SPEC_BITBLK && ob[k].ob_spec.bitblk
				       && rsc_bitblk_bytes(ob[k].ob_spec.bitblk) % 2;
		for (k = 0; k < rsc_nfrimg(r); k++)
			odd += rsc_bitblk_bytes(rsc_frimg(r, k)) % 2;
		CHECK(odd == 0);
	}
	CHECK(wr(r, cw, ch, &o1, &l1, &e) == 0);
	CHECK(o1 && same_bytes(what, o1, l1, g.b, g.len));
	CHECK(o1 && sequential_table(o1, l1));
	/* the model is unchanged, and written twice gives the same bytes */
	CHECK(model_hash(r) == h);
	CHECK(wr(r, cw, ch, &o2, &l2, &e) == 0);
	CHECK(o2 && same_bytes("written twice", o2, l2, o1, l1));
	/* write(read(write(read(x)))) == write(read(x)), and the same content */
	r2 = rd(o1, l1, cw, ch, &e);
	CHECK(r2 && same_model(r, r2));
	CHECK(r2 && wr(r2, cw, ch, &o3, &l3, &e) == 0);
	CHECK(o3 && same_bytes("rewritten", o3, l3, o1, l1));
	rsc_mfree(o1);
	rsc_mfree(o2);
	rsc_mfree(o3);
	rsc_free(r);
	rsc_free(r2);
	free(f.b);
	free(g.b);
}

/* ---- (c) construction -------------------------------------------------------------------- */

/* A resource small enough to compute by hand: a box with a string, and a
 * free string. */
static void test_hand(void)
{
	struct rsc	*r = rsc_new();
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	static const unsigned	want[18] = {
		0, 46, 46, 46, 46, 42, 36, 42, 0, 94, 2, 1, 0, 0, 0, 1, 0, 98
	};
	int	k, t, s;

	t = rsc_tree_add(r, 1);
	CHECK(rsc_obj_set_type(r, t, 0, G_BOX) == 0);
	CHECK(rsc_obj_set_value(r, t, 0, 0x00021100) == 0);
	CHECK(rsc_obj_set_geom(r, t, 0, 0, 0, 0x0220, 0x0105) == 0);
	s = rsc_obj_add(r, t, 0, G_STRING);
	CHECK(rsc_obj_set_str(r, t, s, RSC_STR_SPEC, "AB") == 0);
	CHECK(rsc_obj_set_geom(r, t, s, 1, 1, 2, 1) == 0);
	CHECK(rsc_frstr_add(r, "C") == 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	CHECK(len == 98);
	for (k = 0; k < 18; k++)
		CHECK(hw(o, k) == want[k]);
	CHECK(memcmp(o + 36, "AB\0C\0\0", 6) == 0);
	CHECK(rd32(o, 42) == 39);
	/* the box */
	CHECK(rd16(o, 46) == 0xffff && rd16(o, 48) == 1 && rd16(o, 50) == 1);
	CHECK(rd16(o, 52) == G_BOX && rd16(o, 54) == 0 && rd16(o, 56) == 0);
	CHECK(rd32(o, 58) == 0x00021100);
	CHECK(rd16(o, 62) == 0 && rd16(o, 64) == 0 && rd16(o, 66) == 0x0220 && rd16(o, 68) == 0x0105);
	/* the string */
	CHECK(rd16(o, 70) == 0 && rd16(o, 72) == 0xffff && rd16(o, 74) == 0xffff);
	CHECK(rd16(o, 76) == G_STRING && rd16(o, 78) == OF_LASTOB && rd32(o, 82) == 36);
	CHECK(rd32(o, 94) == 46);
	rsc_mfree(o);
	/* the same in pixels with cell 8x16: 0x0220 is 2 cells + 2 pixels */
	CHECK(rsc_set_cell(r, 8, 16) == 0);
	CHECK(rsc_obj_set_geom(r, t, 0, 0, 0, 18, 21) == 0);
	CHECK(rsc_obj_set_geom(r, t, s, 8, 16, 16, 16) == 0);
	CHECK(wr(r, 8, 16, &o, &len, &e) == 0);
	CHECK(len == 98 && rd16(o, 66) == 0x0202 && rd16(o, 68) == 0x0501);
	CHECK(rd16(o, 86) == 1 && rd16(o, 88) == 1 && rd16(o, 90) == 2 && rd16(o, 92) == 1);
	rsc_mfree(o);
	rsc_free(r);
}

static void test_sample(void)
{
	struct rsc	*r = build_sample(), *r2;
	struct rsc_error	e;
	uint8_t	*o = NULL, *o2 = NULL;
	size_t	len, len2, ob, cb;
	uint64_t	h = model_hash(r);
	int	k;

	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	if (!o)
	{
		fprintf(stderr, "sample: %s\n", e.msg);
		return;
	}
	CHECK(model_hash(r) == h);
	CHECK(hw(o, H_VRSN) == 5);
	CHECK(hw(o, H_NOBS) == 10 + 8 + 1);
	CHECK(hw(o, H_NTREE) == 3);
	CHECK(hw(o, H_NTED) == 2);
	CHECK(hw(o, H_NIB) == 2);
	CHECK(hw(o, H_NBB) == 4);
	CHECK(hw(o, H_NSTRING) == 2);
	CHECK(hw(o, H_NIMAGES) == 2);
	CHECK(hw(o, H_STRING) == 36);
	/* strings in preorder: the FTEXT (object 3) comes first */
	CHECK(memcmp(o + 36, "@____\0Name: ____\0XXXX\0ICON\0Hello\0Title\0free one", 48) == 0);
	ob = hw(o, H_OBJECT);
	CHECK(hw(o, H_TRINDEX) == ob + 19 * 24);
	CHECK(rd32(o, hw(o, H_TRINDEX)) == ob);
	CHECK(rd32(o, hw(o, H_TRINDEX) + 4) == ob + 10 * 24);
	CHECK(rd32(o, hw(o, H_TRINDEX) + 8) == ob + 18 * 24);
	/* preorder of tree 0: 0 1 3 4 7 2 6 8 10 11 */
	CHECK(rd16(o, ob) == 0xffff && rd16(o, ob + 2) == 1 && rd16(o, ob + 4) == 9);
	CHECK(rd16(o, ob + 24 + 2) == 2 && rd16(o, ob + 24 + 4) == 4 && rd16(o, ob + 24) == 5);
	CHECK(rd16(o, ob + 3 * 24 + 6) == G_ICON);
	CHECK(rd16(o, ob + 4 * 24 + 6) == G_CICON && rd32(o, ob + 4 * 24 + 12) == 0);
	CHECK(rd16(o, ob + 4 * 24) == 1);			/* last child of 1 */
	CHECK(rd16(o, ob + 8 * 24 + 8) == OF_INDIRECT && rd32(o, ob + 8 * 24 + 12) == 0x12345678);
	CHECK(rd16(o, ob + 9 * 24 + 6) == (G_BOXCHAR | 0x5500));
	CHECK(rd16(o, ob + 9 * 24 + 8) == OF_LASTOB && rd16(o, ob + 9 * 24) == 0);
	for (k = 0; k < 9; k++)
		CHECK(!(rd16(o, ob + (size_t)k * 24 + 8) & OF_LASTOB));
	CHECK(rd16(o, ob + 17 * 24 + 8) & OF_LASTOB);
	CHECK(rd16(o, ob + 18 * 24 + 8) == OF_LASTOB);
	/* tree 1: TEDINFO with NULL strings, a NULL string, a G_CICON without icon */
	CHECK(rd32(o, hw(o, H_TEDINFO) + 28) == FILE_NULL);
	CHECK(rd32(o, ob + 11 * 24 + 12) == FILE_NULL);
	CHECK(rd32(o, ob + 14 * 24 + 12) == FILE_NULL);	/* G_IMAGE without BITBLK */
	CHECK(rd32(o, ob + 15 * 24 + 12) == FILE_NULL);	/* G_TEXT without TEDINFO */
	CHECK(rd32(o, ob + 16 * 24 + 12) == FILE_NULL);	/* G_CICON without icon */
	CHECK(rd32(o, ob + 17 * 24 + 12) == 1);
	/* the root of tree 2 in file form */
	CHECK(rd16(o, ob + 18 * 24 + 16) == 0xffff && rd16(o, ob + 18 * 24 + 18) == 0x7fff);
	/* the colour icon extension (the 9-byte image of tree 1 is padded, so
	 * the tree index ends even and no pad precedes the extension) */
	CHECK(hw(o, H_TRINDEX) % 2 == 0 && hw(o, H_RSSIZE) == hw(o, H_TRINDEX) + 12);
	cb = hw(o, H_RSSIZE);
	CHECK(rd32(o, cb) == len && rd32(o, cb + 4) == cb + 12 && rd32(o, cb + 8) == 0);
	CHECK(rd32(o, cb + 12) == cb + 24 && rd32(o, cb + 20) == FILE_NULL);
	CHECK(rd32(o, cb + 24) == 0x11111111 && rd32(o, cb + 24 + 34) == 2);
	/* the first version: 16x2 planes of 4 bytes, mono data, mask, text */
	k = (int)(cb + 24 + 38 + 8 + 12);
	CHECK(memcmp(o + cb + 24 + 38 + 8, "CI\0\0\0\0\0\0\0\0\0\0", 12) == 0);
	CHECK(rd16(o, (size_t)k) == 4 && rd32(o, (size_t)k + 2) == 0xabcdef01);
	CHECK(rd32(o, (size_t)k + 10) == 0 && rd32(o, (size_t)k + 18) == 1);
	k += 22 + 4 * 5;
	CHECK(rd16(o, (size_t)k) == 2 && rd32(o, (size_t)k + 10) == 1 && rd32(o, (size_t)k + 14) == 0x12345678);
	CHECK(rd32(o, (size_t)k + 18) == 0);
	CHECK(rd32(o, cb + 16) == (uint32_t)k + 22 + 2 * 4 * 3);
	CHECK((size_t)rd32(o, cb + 16) + 38 + 2 * 4 + 12 == len);
	/* read back: the same content, and the same bytes again */
	/* te_txtlen and te_tmplen as the model has them: strlen + 1 of the
	 * strings, as given (77, 88) for NULL strings (tree 1) */
	CHECK(rd16(o, hw(o, H_TEDINFO) + 24) == 6 && rd16(o, hw(o, H_TEDINFO) + 26) == 11);
	CHECK(rd16(o, hw(o, H_TEDINFO) + 28 + 24) == 77 && rd16(o, hw(o, H_TEDINFO) + 28 + 26) == 88);
	r2 = rd(o, len, 0, 0, &e);
	CHECK(r2 && same_model(r, r2));
	CHECK(r2 && wr(r2, 0, 0, &o2, &len2, &e) == 0);
	CHECK(o2 && fixed_point("sample rewritten", o2, len2, 0, 0));
	rsc_mfree(o);
	rsc_mfree(o2);
	rsc_free(r);
	rsc_free(r2);
}

/* NULL image data pointers in a file (0xFFFFFFFF) read as NULL data, and
 * are written as zeros of the right size with a real pointer; they read
 * back as a zero image. */
static void test_null_data(void)
{
	struct rsc	*r = build_sample(), *r2, *r3;
	struct rsc_error	e;
	uint8_t	*o = NULL, *o2 = NULL, *z;
	size_t	len, len2, bb, ib, k;
	const OBJECT	*ob;
	uint32_t	p;

	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	rsc_free(r);
	if (!o)
		return;
	bb = hw(o, H_BITBLK);			/* the G_IMAGE of tree 0, 2 x 3 bytes */
	ib = hw(o, H_ICONBLK);			/* the G_ICON of tree 0, 24 x 3 */
	CHECK(rd16(o, bb + 4) == 2 && rd16(o, bb + 6) == 3 && rd16(o, ib + 22) == 24 && rd16(o, ib + 24) == 3);
	memset(o + bb, 0xff, 4);
	memset(o + ib, 0xff, 4);
	r2 = rd(o, len, 0, 0, &e);
	CHECK(r2 != NULL);
	if (!r2)
		return;
	ob = rsc_tree_objects(r2, 0, NULL);
	CHECK(ob[6].ob_spec.bitblk->bi_pdata == NULL);	/* preorder 0 1 3 4 7 2 6 */
	CHECK(ob[3].ob_spec.iconblk->ib_pmask == NULL && ob[3].ob_spec.iconblk->ib_pdata != NULL);
	CHECK(wr(r2, 0, 0, &o2, &len2, &e) == 0);
	if (o2)
	{
		p = rd32(o2, bb);
		CHECK(p != FILE_NULL && p + 6 <= len2);
		for (k = 0; k < 6 && p + 6 <= len2; k++)
			CHECK(o2[p + k] == 0);
		p = rd32(o2, ib);
		CHECK(p != FILE_NULL && p + 12 <= len2 && rd32(o2, ib + 4) == p + 12);
		for (k = 0; k < 12 && p + 12 <= len2; k++)
			CHECK(o2[p + k] == 0);
		r3 = rd(o2, len2, 0, 0, &e);
		CHECK(r3 != NULL);
		if (r3)
		{
			ob = rsc_tree_objects(r3, 0, NULL);
			z = calloc(1, 12);
			CHECK(ob[6].ob_spec.bitblk->bi_pdata && memcmp(ob[6].ob_spec.bitblk->bi_pdata, z, 6) == 0);
			CHECK(ob[3].ob_spec.iconblk->ib_pmask && memcmp(ob[3].ob_spec.iconblk->ib_pmask, z, 12) == 0);
			free(z);
			CHECK(same_model(r2, r3));
		}
		rsc_free(r3);
	}
	rsc_mfree(o);
	rsc_mfree(o2);
	rsc_free(r2);
}

/* Trees in several orders: the file follows the order of the model. */
static void test_tree_order(void)
{
	struct rsc	*r = rsc_new(), *r2;
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	char	s[8];
	int	t, k;

	for (t = 0; t < 4; t++)
	{
		CHECK(rsc_tree_add(r, 1) == t);
		for (k = 0; k <= t; k++)
		{
			int	i = rsc_obj_add(r, t, 0, G_STRING);

			snprintf(s, sizeof s, "%d.%d", t, k);
			CHECK(rsc_obj_set_str(r, t, i, RSC_STR_SPEC, s) == 0);
		}
	}
	CHECK(rsc_tree_delete(r, 1) == 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	CHECK(hw(o, H_NTREE) == 3 && hw(o, H_NOBS) == 2 + 4 + 5);
	CHECK(memcmp(o + 36, "0.0\0" "2.0\0" "2.1\0" "2.2\0" "3.0", 20) == 0);
	CHECK(rd32(o, hw(o, H_TRINDEX) + 4) == hw(o, H_OBJECT) + 2 * 24u);
	r2 = rd(o, len, 0, 0, &e);
	CHECK(r2 && same_model(r, r2));
	rsc_free(r2);
	rsc_mfree(o);
	rsc_free(r);
}

/* ---- (d) errors and limits -------------------------------------------------------------- */

static void expect_fail(struct rsc *r, int cw, int ch, int code, size_t off, int line)
{
	struct rsc_error	e;
	uint8_t	*o = (uint8_t *)&e;
	size_t	len = 99;
	long	live = rsc_test_live_blocks();
	int	rc = wr(r, cw, ch, &o, &len, &e);

	if (rc != -1 || e.code != code || (off != (size_t)-1 && e.offset != off))
		fprintf(stderr, "line %d: rc %d code %d offset 0x%zx: %s\n", line, rc, e.code,
			e.offset, e.msg);
	CHECK(rc == -1);
	CHECK(e.code == code);
	CHECK(off == (size_t)-1 || e.offset == off);
	CHECK(o == NULL && len == 0);
	CHECK(e.msg[0] != '\0');
	CHECK(rsc_test_live_blocks() == live);
	if (rc == 0)
		rsc_mfree(o);
}

#define EXPECT_FAIL(r, cw, ch, code, off)	expect_fail(r, cw, ch, code, off, __LINE__)

/* One tree (a root), one free string of n characters and, when odd, a
 * free image of one byte. */
static struct rsc *sized(size_t n, int odd)
{
	struct rsc	*r = rsc_new();
	char	*s = malloc(n + 1);
	BITBLK	bb = { pat, 1, 1, 0, 0, 1 };

	memset(s, 'x', n);
	s[n] = '\0';
	rsc_tree_add(r, 1);
	CHECK(rsc_frstr_add(r, s) == 0);
	if (odd)
		CHECK(rsc_frimg_add(r, &bb) == 0);
	free(s);
	return r;
}

/* TEDINFO strings: models built with rsc_obj_set_str are fixed points
 * (write, read, write gives the same bytes), te_txtlen and te_tmplen
 * following the strings; a text or template of 32767 characters (which
 * only a caller breaking the contract can store) is refused by the writer,
 * since the reader would refuse it. */
static void test_tedinfo_lengths(void)
{
	static const size_t	lens[] = { 0, 1, 2, 7, 100, 1000, 20000, 32766 };
	char	*s = malloc(32768), *t = malloc(32768);
	struct rsc_error	e;
	struct rsc	*r;
	TEDINFO	te, *tp;
	uint8_t	*o;
	size_t	len, k;
	int	i;

	memset(s, 'a', 32767);
	memset(t, '_', 32767);
	for (k = 0; k < sizeof lens / sizeof lens[0]; k++)
	{
		r = rsc_new();
		rsc_tree_add(r, 1);
		memset(&te, 0, sizeof te);
		te.te_txtlen = 5;		/* stale: replaced below */
		te.te_tmplen = 9;
		i = rsc_obj_add(r, 0, 0, G_FTEXT);
		CHECK(rsc_obj_set_tedinfo(r, 0, i, &te) == 0);
		s[lens[k]] = '\0';
		t[lens[(k + 3) % 8] / 2] = '\0';
		CHECK(rsc_obj_set_str(r, 0, i, RSC_STR_TE_TEXT, s) == 0);
		CHECK(rsc_obj_set_str(r, 0, i, RSC_STR_TE_TMPLT, t) == 0);
		CHECK(rsc_obj_set_str(r, 0, i, RSC_STR_TE_VALID, "X") == 0);
		i = rsc_obj_add(r, 0, 0, G_TEXT);
		CHECK(rsc_obj_set_tedinfo(r, 0, i, &te) == 0);
		CHECK(rsc_obj_set_str(r, 0, i, RSC_STR_TE_TEXT, "short") == 0);
		s[lens[k]] = 'a';
		t[lens[(k + 3) % 8] / 2] = '_';
		o = NULL;
		CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
		if (!o)
			fprintf(stderr, "tedinfo %zu: %s\n", lens[k], e.msg);
		CHECK(o && rd16(o, hw(o, H_TEDINFO) + 24) == lens[k] + 1);
		CHECK(o && rd16(o, hw(o, H_TEDINFO) + 26) == lens[(k + 3) % 8] / 2 + 1);
		CHECK(o && rd16(o, hw(o, H_TEDINFO) + 28 + 24) == 6 && rd16(o, hw(o, H_TEDINFO) + 28 + 26) == 9);
		CHECK(o && fixed_point("tedinfo lengths", o, len, 0, 0));
		rsc_mfree(o);
		rsc_free(r);
	}
	/* 32767 characters stored behind the model's back: refused at the
	 * TEDINFO (after 36 + 32768 bytes of string), naming the object */
	r = rsc_new();
	rsc_tree_add(r, 1);
	CHECK(rsc_obj_set_type(r, 0, 0, G_TEXT) == 0);
	memset(&te, 0, sizeof te);
	CHECK(rsc_obj_set_tedinfo(r, 0, 0, &te) == 0);
	tp = rsc_tree_objects(r, 0, NULL)[0].ob_spec.tedinfo;
	s[32767] = '\0';
	tp->te_ptext = rsc_strdup(s);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 36 + 32768);
	CHECK(wr(r, 0, 0, &o, &len, &e) == -1 && strncmp(e.msg, "TEDINFO text too long (tree 0, object 0)", 40) == 0);
	tp->te_ptmplt = tp->te_ptext;
	tp->te_ptext = NULL;
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 36 + 32768);
	CHECK(wr(r, 0, 0, &o, &len, &e) == -1 && strncmp(e.msg, "TEDINFO template too long", 25) == 0);
	tp->te_ptmplt[32766] = '\0';		/* 32766: written, and read back */
	tp->te_tmplen = 32767;
	o = NULL;
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	CHECK(o && fixed_point("tedinfo 32766", o, len, 0, 0));
	rsc_mfree(o);
	rsc_free(r);
	free(s);
	free(t);
	CHECK(rsc_test_live_blocks() == 0);
}

static uint8_t *round_trip(struct rsc *r, size_t *len);

/* Image data of an odd size is followed by a zero pad, so every block and
 * record after it, and every header offset, stays even (a 68000 cannot
 * read a record at an odd address); the content reads back the same.  An
 * icon plane is always even (rsc_plane_bytes), so only BITBLKs can be
 * odd. */
static void test_odd_images(void)
{
	static const int	sizes[][2] = { { 3, 3 }, { 1, 1 }, { 5, 7 }, { 2, 3 }, { 0, 9 } };
	BITBLK	bb = { NULL, 3, 3, 1, 2, 3 };
	struct rsc_error	e;
	struct rsc	*r, *r2;
	uint8_t	*o;
	size_t	len, img, k;
	int	i, h, w;

	for (w = 1; w <= 33; w++)
		for (h = 0; h <= 5; h++)
			CHECK(rsc_plane_bytes(w, h) % 2 == 0);
	for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++)
	{
		r = rsc_new();
		rsc_tree_add(r, 1);
		i = rsc_obj_add(r, 0, 0, G_IMAGE);
		set_image(r, 0, i, sizes[k][0], sizes[k][1], pat + 3);
		i = rsc_obj_add(r, 0, 0, G_ICON);
		set_icon(r, 0, i, 16, 3, pat, pat + 10, "odd");
		i = rsc_obj_add(r, 0, 0, G_TEXT);
		CHECK(set_tedinfo(r, 0, i, "ab", NULL, "c") == 0);	/* strings end odd */
		bb.bi_wb = (short)sizes[k][1];
		bb.bi_hl = (short)sizes[k][0];
		bb.bi_pdata = pat + 20;
		CHECK(rsc_frimg_add(r, &bb) == 0);
		bb.bi_pdata = NULL;
		CHECK(rsc_frimg_add(r, &bb) == 1);
		CHECK(rsc_frstr_add(r, "x") == 0);
		o = round_trip(r, &len);
		CHECK(o != NULL);
		if (o)
		{
			size_t	n = (size_t)(sizes[k][0] * sizes[k][1]), pad = n & 1;

			for (h = H_OBJECT; h <= H_TRINDEX; h++)
				CHECK(hw(o, h) % 2 == 0);
			CHECK(len % 2 == 0 && hw(o, H_RSSIZE) == len);
			/* the image section: the BITBLK data, its pad, the icon's
			 * mask and data, then the two free images */
			img = hw(o, H_IMDATA);
			CHECK(rd32(o, hw(o, H_BITBLK)) == img);
			CHECK(!pad || o[img + n] == 0);
			CHECK(rd32(o, hw(o, H_ICONBLK)) == img + n + pad);
			CHECK(rd32(o, hw(o, H_ICONBLK) + 4) == img + n + pad + 6);
			CHECK(rd32(o, hw(o, H_BITBLK) + 14) == img + n + pad + 12);
			CHECK(rd32(o, hw(o, H_BITBLK) + 28) == img + 2 * (n + pad) + 12);
			CHECK(hw(o, H_FRSTR) == img + 3 * (n + pad) + 12);
			/* and reads back as written */
			r2 = rd(o, len, 0, 0, &e);
			CHECK(r2 && fixed_point("odd images", o, len, 0, 0));
			CHECK(r2 && rsc_bitblk_bytes(rsc_frimg(r2, 0)) == n
			      && data_eq(rsc_frimg(r2, 0)->bi_pdata, pat + 20, n));
			rsc_free(r2);
		}
		rsc_mfree(o);
		rsc_free(r);
	}
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_limits(void)
{
	struct rsc	*r;
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	int	t, k;

	/* 36 + 65446 + 1 + 1 (pad) + 4 + 14 + 4 + 24 + 4 = 65534: the largest
	 * file (every section ends even, so 65535 bytes cannot be reached) */
	r = sized(65445, 1);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && len == 65534 && hw(o, H_RSSIZE) == 65534);
	if (o)
	{
		struct rsc	*r2 = rd(o, len, 0, 0, &e);

		CHECK(r2 && same_model(r, r2));
		rsc_free(r2);
	}
	rsc_mfree(o);
	rsc_free(r);
	/* two bytes more: the tree index ends at 65536; it starts at 65532 */
	r = sized(65447, 1);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65532);
	rsc_free(r);
	/* 65536 bytes (36 + 65468 + 4 + 24 + 4): the tree index at 65532 */
	r = sized(65467, 0);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65532);
	rsc_free(r);
	r = sized(65465, 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && len == 65534);
	rsc_mfree(o);
	rsc_free(r);
	/* a string that alone passes the limit: it is the item */
	r = sized(70000, 0);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 36);
	rsc_free(r);
	/* with a colour icon: a classic part of 65534 bytes, then the
	 * extension at 65534 without a pad; two bytes more do not fit */
	r = sized(65445 - 24, 1);
	k = rsc_obj_add(r, 0, 0, G_CICON);
	set_cicon(r, 0, k, 16, 1, NULL, NULL, "x");
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && o && hw(o, H_RSSIZE) == 65534 && rd32(o, 65534) == len);
	rsc_mfree(o);
	rsc_free(r);
	r = sized(65447 - 24, 1);
	k = rsc_obj_add(r, 0, 0, G_CICON);
	set_cicon(r, 0, k, 16, 1, NULL, NULL, "x");
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65532);
	CHECK(rsc_frstr_set(r, 0, "short") == 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0);
	rsc_mfree(o);
	rsc_free(r);
	r = sized(65440, 0);
	k = rsc_obj_add(r, 0, 0, G_CICON);
	set_cicon(r, 0, k, 16, 1, NULL, NULL, "x");
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && o && hw(o, H_RSSIZE) == 65534);
	rsc_mfree(o);
	rsc_free(r);
	/* strings alone (no trees): strings ending at 65535 need the pad there */
	r = sized(65498, 0);
	CHECK(rsc_tree_delete(r, 0) == 0);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65535);
	rsc_free(r);
	r = sized(65494, 0);			/* strings end at 65531: pad, table at 65532 */
	CHECK(rsc_tree_delete(r, 0) == 0);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65532);
	rsc_free(r);
	r = sized(65495, 0);			/* strings end at 65532: 65536 bytes */
	CHECK(rsc_tree_delete(r, 0) == 0);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, 65532);
	rsc_free(r);
	r = sized(65493, 0);			/* strings end at 65530: 65534 bytes */
	CHECK(rsc_tree_delete(r, 0) == 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && len == 65534 && o && o[65528] == 'x' && o[65529] == 0
	      && rd32(o, 65530) == 36);
	rsc_mfree(o);
	rsc_free(r);
	/* counts above 65535: free strings, objects */
	r = rsc_new();
	rsc_tree_add(r, 1);
	for (k = 0; k < 65536; k++)
		rsc_frstr_add(r, "");
	CHECK(rsc_nfrstr(r) == 65536);
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, (size_t)-1);
	rsc_free(r);
	r = rsc_new();
	for (t = 0; t < 3; t++)
	{
		OBJECT	*ob;
		int	n = RSC_MAX_OBJECTS;

		CHECK(rsc_tree_add(r, n) == t);
		ob = rsc_tree_objects(r, t, NULL);
		ob[0].ob_head = 1;
		ob[0].ob_tail = (short)(n - 1);
		for (k = 1; k < n; k++)
			ob[k].ob_next = (short)(k + 1 < n ? k + 1 : 0);
	}
	EXPECT_FAIL(r, 0, 0, RSC_ETOOBIG, (size_t)-1);
	rsc_free(r);
}

/* The densest file the writer can make, the largest number of empty free
 * strings (5 bytes of file each: the NUL and the table entry), reads back
 * within the reader's budget (25 bytes charged each on a 64-bit host,
 * against 4 * len + 65536: about 200 bytes to spare).  A new per-entry
 * charge in the reader can break this write-then-read; see BLOCK_COST in
 * rscatari_read.c. */
static void test_densest(void)
{
	struct rsc_error	e;
	struct rsc	*r = rsc_new(), *r2;
	uint8_t	*o = NULL;
	size_t	len;
	int	n;

	for (n = 0; n < 13200; n++)
		rsc_frstr_add(r, "");
	while (wr(r, 0, 0, &o, &len, &e) != 0)
	{
		CHECK(e.code == RSC_ETOOBIG);
		rsc_frstr_delete(r, rsc_nfrstr(r) - 1);
	}
	n = rsc_nfrstr(r);
	CHECK(n == 13099 && len == 65532);	/* 36 + 13099 + 1 (pad) + 4 * 13099 */
	r2 = rd(o, len, 0, 0, &e);
	CHECK(r2 != NULL);
	if (!r2)
		fprintf(stderr, "densest file (%d free strings): %s\n", n, e.msg);
	CHECK(r2 && same_model(r, r2));
	rsc_free(r2);
	rsc_mfree(o);
	rsc_free(r);
}

static void test_errors(void)
{
	struct rsc	*r = rsc_new();
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	OBJECT	*ob;
	int	t;

	/* arguments */
	CHECK(rsc_atari_write(NULL, 0, 0, &o, &len, &e) == -1 && e.code == RSC_EARG && !o && !len);
	CHECK(rsc_atari_write(r, 0, 0, NULL, &len, &e) == -1 && e.code == RSC_EARG);
	CHECK(rsc_atari_write(r, 0, 0, &o, NULL, &e) == -1 && e.code == RSC_EARG);
	CHECK(rsc_atari_write(r, 0, 0, NULL, NULL, NULL) == -1);
	EXPECT_FAIL(r, 8, 0, RSC_EARG, 0);
	EXPECT_FAIL(r, 0, 16, RSC_EARG, 0);
	EXPECT_FAIL(r, -8, -16, RSC_EARG, 0);
	EXPECT_FAIL(r, 8, 16, RSC_EARG, 0);		/* the model is in file form */
	CHECK(rsc_set_cell(r, RSC_MAX_CELL + 1, 16) == 0);
	EXPECT_FAIL(r, RSC_MAX_CELL + 1, 16, RSC_EARG, 0);
	CHECK(rsc_set_cell(r, 0, 0) == 0);
	/* an empty resource: the header alone */
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && len == 36 && hw(o, H_RSSIZE) == 36
	      && hw(o, H_OBJECT) == 36 && hw(o, H_TRINDEX) == 36 && hw(o, H_NTREE) == 0);
	rsc_mfree(o);
	/* a cycle written into the links */
	t = rsc_tree_add(r, 3);
	ob = rsc_tree_objects(r, t, NULL);
	ob[0].ob_head = 1;
	ob[0].ob_tail = 2;
	ob[1].ob_next = 2;
	ob[2].ob_next = 0;
	ob[2].ob_head = ob[2].ob_tail = 1;	/* 1 under two parents, and its own ancestor */
	EXPECT_FAIL(r, 0, 0, RSC_ECYCLE, 0);
	ob[2].ob_head = ob[2].ob_tail = -1;
	ob[1].ob_head = ob[1].ob_tail = 0;	/* the root as a child of 1 */
	EXPECT_FAIL(r, 0, 0, RSC_ECYCLE, 0);
	ob[1].ob_head = ob[1].ob_tail = -1;
	/* a link out of range */
	ob[2].ob_next = 99;
	EXPECT_FAIL(r, 0, 0, RSC_ELINK, 0);
	/* a malformed chain: the last child is not the parent's ob_tail */
	ob[2].ob_next = 0;
	ob[0].ob_tail = 1;
	EXPECT_FAIL(r, 0, 0, RSC_ELINK, 0);
	ob[0].ob_tail = 2;
	/* the root's ob_next is not taken from the model */
	ob[0].ob_next = 5;
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && o && rd16(o, hw(o, H_OBJECT)) == 0xffff);
	rsc_mfree(o);
	rsc_free(r);
}

/* Negative icon and image sizes are refused at each of the four places,
 * and not looked at in an object no root reaches. */
static void test_negative(void)
{
	struct rsc	*r;
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	ICONBLK	ib;
	BITBLK	bb = { NULL, -2, 3, 0, 0, 1 };
	OBJECT	*ob;
	int	site, k;

	memset(&ib, 0, sizeof ib);
	for (site = 0; site < 4; site++)
	{
		r = rsc_new();
		rsc_tree_add(r, 1);
		k = rsc_obj_add(r, 0, 0, site == 0 ? G_ICON : site == 3 ? G_CICON : G_IMAGE);
		ib.ib_wicon = 16;
		ib.ib_hicon = -1;
		switch (site)
		{
		case 0:
			CHECK(rsc_obj_set_iconblk(r, 0, k, &ib) == 0);
			break;
		case 1:
			CHECK(rsc_obj_set_bitblk(r, 0, k, &bb) == 0);
			break;
		case 2:
			CHECK(rsc_frimg_add(r, &bb) == 0);
			break;
		case 3:
			CHECK(rsc_obj_set_cicon(r, 0, k, &ib, "neg") == 0);
			break;
		}
		EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
		CHECK(wr(r, 0, 0, &o, &len, &e) == -1 && strstr(e.msg, "negative")
		      && strstr(e.msg, site == 2 ? "free image 0" : "object 1"));
		if (site != 2)
		{
			/* unreachable: the root no longer has the child */
			ob = rsc_tree_objects(r, 0, NULL);
			ob[0].ob_head = ob[0].ob_tail = -1;
			CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && o && hw(o, H_NOBS) == 1);
			rsc_mfree(o);
		}
		rsc_free(r);
	}
}

/* A colour icon whose mono size is changed behind the model's back: the
 * writer must refuse it (RSC_EARG) rather than read past the mono data. */
static void test_cicon_mono_size(void)
{
	struct rsc	*r;
	struct rsc_error	e;
	struct rsc_cicon	*c;
	uint8_t	*o;
	size_t	len;
	int	k, withv;

	for (withv = 0; withv < 2; withv++)
	{
		r = rsc_new();
		rsc_tree_add(r, 1);
		k = rsc_obj_add(r, 0, 0, G_CICON);
		c = set_cicon(r, 0, k, 16, 2, pat + 300, pat + 400, "CI");
		if (withv)
		{
			CHECK(rsc_cicon_add_version(c, 4, pat + 500, pat + 600, NULL, NULL) == 0);
			c->blk.monoblk.ib_wicon = 32;	/* only the mono size */
		}
		else
			c->blk.monoblk.ib_hicon = 40;
		CHECK(wr(r, 0, 0, &o, &len, &e) == -1 && e.code == RSC_EARG
		      && strstr(e.msg, "does not match") && strstr(e.msg, "tree 0")
		      && strstr(e.msg, "object 1"));
		rsc_free(r);
	}
}

/* Pixel coordinates the file form cannot hold. */
static void test_coords(void)
{
	struct rsc	*r = rsc_new();
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	int	k;

	rsc_tree_add(r, 1);
	k = rsc_obj_add(r, 0, 0, G_BOX);
	CHECK(rsc_set_cell(r, 8, 16) == 0);
	/* canonical values: the old unfix_chpos form */
	CHECK(rsc_obj_set_geom(r, 0, 0, 0, 0, 255 * 8 + 7, 255 * 16 + 15) == 0);
	CHECK(wr(r, 8, 16, &o, &len, &e) == 0 && o && rd16(o, 36 + 20) == 0x07ff && rd16(o, 36 + 22) == 0x0fff);
	rsc_mfree(o);
	/* the extremes the reader produces: -127 and 255 cells + 128 */
	CHECK(rsc_obj_set_geom(r, 0, 0, -127, -1, 255 * 8 + 128, 255 * 16 + 128) == 0);
	CHECK(wr(r, 8, 16, &o, &len, &e) == 0 && o);
	if (o)
		CHECK(rd16(o, 36 + 16) == 0x8100 && rd16(o, 36 + 18) == 0xff00
		      && rd16(o, 36 + 20) == 0x80ff && rd16(o, 36 + 22) == 0x80ff);
	rsc_mfree(o);
	/* one past them: no file form */
	CHECK(rsc_obj_set_geom(r, 0, 0, 0, 0, 255 * 8 + 129, 1) == 0);
	EXPECT_FAIL(r, 8, 16, RSC_ETOOBIG, 36);
	CHECK(wr(r, 8, 16, &o, &len, &e) == -1 && strstr(e.msg, "ob_width") && strstr(e.msg, "object 0"));
	CHECK(rsc_obj_set_geom(r, 0, 0, 0, 0, 8, 255 * 16 + 129) == 0);
	EXPECT_FAIL(r, 8, 16, RSC_ETOOBIG, 36);
	CHECK(rsc_obj_set_geom(r, 0, 0, 0, 0, 8, 16) == 0);
	CHECK(rsc_obj_set_geom(r, 0, k, -128, 0, 8, 16) == 0);
	EXPECT_FAIL(r, 8, 16, RSC_ETOOBIG, 36 + 24);
	CHECK(wr(r, 8, 16, &o, &len, &e) == -1 && strstr(e.msg, "ob_x") && strstr(e.msg, "object 1"));
	CHECK(rsc_obj_set_geom(r, 0, k, 0, -128, 8, 16) == 0);
	EXPECT_FAIL(r, 8, 16, RSC_ETOOBIG, 36 + 24);
	/* in file form every value is written as it is */
	CHECK(rsc_set_cell(r, 0, 0) == 0);
	CHECK(rsc_obj_set_geom(r, 0, k, -1, -32768, 32767, 0x1234) == 0);
	CHECK(wr(r, 0, 0, &o, &len, &e) == 0 && o && rd16(o, 36 + 24 + 16) == 0xffff
	      && rd16(o, 36 + 24 + 18) == 0x8000 && rd16(o, 36 + 24 + 20) == 0x7fff);
	rsc_mfree(o);
	/* the largest cell */
	CHECK(rsc_set_cell(r, RSC_MAX_CELL, RSC_MAX_CELL) == 0);
	CHECK(rsc_obj_set_geom(r, 0, k, 255 * RSC_MAX_CELL + 128, 0, 0, 0) == 0);
	CHECK(wr(r, RSC_MAX_CELL, RSC_MAX_CELL, &o, &len, &e) == 0 && o && rd16(o, 36 + 24 + 16) == 0x80ff);
	rsc_mfree(o);
	rsc_free(r);
}

/* fix_chpos, the reader's conversion, written again */
static int fix(unsigned v, int cell)
{
	int	hi = (int)(v >> 8) & 0xff, c = (int)(v & 0xff) * cell;

	return hi > 128 ? c - (256 - hi) : c + hi;
}

/* Every word of every cell: what the reader makes of it the writer turns
 * into a word the reader makes the same of; the values in between too; the
 * canonical ones as the old unfix_chpos; outside the range nothing. */
static void test_coord_brute(void)
{
	long	bad = 0, n = 0;
	int	cell, v, coff;
	unsigned	word, w2;

	for (cell = 1; cell <= RSC_MAX_CELL; cell++)
	{
		for (word = 0; word < 0x10000; word++)
		{
			v = fix(word, cell);
			n++;
			if (rsc_test_coord_to_file(v, cell, &w2) || fix(w2, cell) != v)
				bad++;
		}
		for (v = -127; v <= 255 * cell + 128; v++)
			if (rsc_test_coord_to_file(v, cell, &w2) || w2 > 0xffff || fix(w2, cell) != v)
				bad++;
		for (v = 0; v < 256 * cell; v++)
		{
			coff = v / cell;
			if (rsc_test_coord_to_file(v, cell, &w2)
			    || w2 != ((unsigned)coff | (unsigned)(v - coff * cell) << 8))
				bad++;
		}
		if (!rsc_test_coord_to_file(-128, cell, &w2) || !rsc_test_coord_to_file(255 * cell + 129, cell, &w2))
			bad++;
	}
	CHECK(bad == 0);
	printf("  coordinates: %ld (cell, word) pairs, %ld wrong\n", n, bad);
}

static uint64_t mrng = 0x2545f4914f6cdd1dULL;

static unsigned mrnd(unsigned n)
{
	mrng = mrng * 6364136223846793005ULL + 1442695040888963407ULL;
	return (unsigned)(mrng >> 33) % n;
}

/* Real files: coordinates the old writer wrapped, and every mutation of a
 * golden the reader accepts at 8x16 is written and reads back the same. */
static void test_coord_files(void)
{
	static const char	*names[] = { "rcs.rsc", "cicon.rsc" };
	static const unsigned	words[] = { 0xff00, 0x08ff, 0x80ff, 0x8101 };
	struct rsc_error	e;
	struct rsc	*r, *r2;
	struct file	f;
	uint8_t	*b, *o;
	size_t	len, at;
	int	k, m, j, nread = 0, nfail = 0;

	f = load("rcs.rsc");
	b = malloc(f.len);
	at = hw(f.b, H_OBJECT) + 24 + 16;		/* ob_x of object 1 */
	for (k = 0; k < 4; k++)
	{
		memcpy(b, f.b, f.len);
		b[at] = (uint8_t)(words[k] >> 8);
		b[at + 1] = (uint8_t)words[k];
		r = rd(b, f.len, 8, 16, &e);
		CHECK(r && rsc_tree_objects(r, 0, NULL)[1].ob_x == fix(words[k], 8));
		o = NULL;
		CHECK(r && wr(r, 8, 16, &o, &len, &e) == 0);
		r2 = o ? rd(o, len, 8, 16, &e) : NULL;
		CHECK(r2 && same_model(r, r2) && rsc_tree_objects(r2, 0, NULL)[1].ob_x == fix(words[k], 8));
		rsc_free(r);
		rsc_free(r2);
		rsc_mfree(o);
	}
	free(b);
	free(f.b);
	for (j = 0; j < 2; j++)
	{
		f = load(names[j]);
		b = malloc(f.len);
		for (k = 0; k < 2000; k++)
		{
			memcpy(b, f.b, f.len);
			for (m = 1 + (int)mrnd(3); m > 0; m--)
				b[mrnd((unsigned)f.len)] ^= (uint8_t)(1 + mrnd(255));
			r = rd(b, f.len, 8, 16, &e);
			if (!r)
				continue;
			nread++;
			o = NULL;
			r2 = NULL;
			if (wr(r, 8, 16, &o, &len, &e) || !(r2 = rd(o, len, 8, 16, &e)) || !same_model(r, r2))
			{
				fprintf(stderr, "%s mutation %d: %s\n", names[j], k, e.msg);
				nfail++;
			}
			rsc_free(r);
			rsc_free(r2);
			rsc_mfree(o);
		}
		free(b);
		free(f.b);
	}
	CHECK(nfail == 0 && nread > 1000);
	printf("  mutated goldens: %d read at 8x16, %d failed to write back\n", nread, nfail);
}

/* ---- (e) allocation failures ---------------------------------------------------------------- */

static void fail_loop(const char *what, const struct rsc *r, int cw, int ch)
{
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	long	n, live = rsc_test_live_blocks();
	int	rc = -1;

	for (n = 0; n < 1000; n++)
	{
		rsc_test_fail_alloc_after(n);
		rc = wr(r, cw, ch, &o, &len, &e);
		rsc_test_fail_alloc_after(-1);
		if (rc == 0)
			break;
		CHECK(e.code == RSC_ENOMEM && o == NULL && len == 0);
		CHECK(rsc_test_live_blocks() == live);
	}
	CHECK(rc == 0 && n > 2);
	if (rc != 0)
		fprintf(stderr, "%s: no success after %ld allocations\n", what, n);
	rsc_mfree(o);
	CHECK(rsc_test_live_blocks() == live);
}

static void test_alloc(void)
{
	struct rsc	*r = build_sample();
	struct file	f;
	struct rsc_error	e;

	fail_loop("sample", r, 0, 0);
	rsc_free(r);
	f = load("rcs.rsc");
	r = rd(f.b, f.len, 8, 16, &e);
	CHECK(r != NULL);
	if (r)
		fail_loop("rcs", r, 8, 16);
	rsc_free(r);
	free(f.b);
	f = load("cicon.rsc");
	r = rd(f.b, f.len, 0, 0, &e);
	CHECK(r != NULL);
	if (r)
		fail_loop("cicon", r, 0, 0);
	rsc_free(r);
	free(f.b);
}

/* ---- (f) colour icons -------------------------------------------------------------------------- */

/* Writes r, reads it back and checks the content; returns the bytes. */
static uint8_t *round_trip(struct rsc *r, size_t *len)
{
	struct rsc_error	e;
	struct rsc	*r2;
	uint8_t	*o = NULL;

	CHECK(wr(r, 0, 0, &o, len, &e) == 0);
	if (!o)
	{
		fprintf(stderr, "round trip: %s\n", e.msg);
		return NULL;
	}
	r2 = rd(o, *len, 0, 0, &e);
	CHECK(r2 != NULL);
	if (!r2)
		fprintf(stderr, "round trip read: %s\n", e.msg);
	CHECK(r2 && same_model(r, r2));
	rsc_free(r2);
	return o;
}

static void test_cicons(void)
{
	struct rsc_error	e2;
	struct rsc	*r = rsc_new();
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	uint8_t	*o;
	size_t	len, cb, blk;
	char	text[16];
	int	k, i;

	/* an icon without versions: a block with a count of 0 */
	rsc_tree_add(r, 1);
	i = rsc_obj_add(r, 0, 0, G_CICON);
	c = set_cicon(r, 0, i, 16, 2, pat, pat + 2, "none");
	o = round_trip(r, &len);
	CHECK(o != NULL);
	if (o)
	{
		cb = hw(o, H_RSSIZE);
		blk = rd32(o, cb + 12);
		CHECK(rd32(o, cb + 16) == FILE_NULL && rd32(o, blk + 34) == 0);
		CHECK(blk + 38 + 8 + 12 == len);
	}
	rsc_mfree(o);
	/* versions with NULL planes, and NULL selected mask: zeros */
	CHECK(rsc_cicon_add_version(c, 2, pat, pat, pat, pat) == 0);
	v = rsc_cicon_version(c, 0);
	rsc_mfree(v->c.col_data);
	v->c.col_data = NULL;
	rsc_mfree(v->c.sel_mask);
	v->c.sel_mask = NULL;
	CHECK(rsc_cicon_add_version(c, 1, pat, pat, NULL, NULL) == 1);
	v = rsc_cicon_version(c, 1);
	rsc_mfree(v->c.col_mask);
	v->c.col_mask = NULL;
	o = round_trip(r, &len);
	if (o)
	{
		blk = rd32(o, hw(o, H_RSSIZE) + 12) + 38 + 8 + 12;
		CHECK(rd16(o, blk) == 2 && rd32(o, blk + 10) == 1 && rd32(o, blk + 18) == 1);
		for (k = 0; k < 2 * 4; k++)
			CHECK(o[blk + 22 + (size_t)k] == 0);
		CHECK(o[blk + 22 + 2 * 4 * 2 + 4] == 0 && o[blk + 22 + 2 * 4 * 2 + 4 + 2 * 4] == 0);
	}
	rsc_mfree(o);
	/* the opaque fields round trip; a stored flag that contradicts the
	 * planes is corrected */
	c->file_head[0] = 0xdeadbeef;
	c->file_head[2] = 0xffffffff;
	v = rsc_cicon_version(c, 0);
	v->file_ptr[0] = 0x01020304;
	v->file_ptr[1] = 0xffffffff;
	v->file_ptr[2] = 0x55555555;
	v->file_ptr[3] = 7;
	v->file_ptr[4] = 0x99;		/* another version follows: written 1 */
	v = rsc_cicon_version(c, 1);
	v->file_ptr[2] = 0x1234;	/* no selected planes: written 0 */
	v->file_ptr[4] = 0x5678;	/* the last: written 0 */
	o = round_trip(r, &len);
	if (o)
	{
		blk = rd32(o, hw(o, H_RSSIZE) + 12);
		CHECK(rd32(o, blk) == 0xdeadbeef && rd32(o, blk + 8) == 0xffffffff);
		blk += 38 + 8 + 12;
		CHECK(rd32(o, blk + 2) == 0x01020304 && rd32(o, blk + 6) == 0xffffffff);
		CHECK(rd32(o, blk + 10) == 0x55555555 && rd32(o, blk + 14) == 7 && rd32(o, blk + 18) == 1);
		blk += 22 + 2 * 4 * 3;
		CHECK(rd32(o, blk + 10) == 0 && rd32(o, blk + 18) == 0);
	}
	rsc_mfree(o);
	/* an icon whose mono data is NULL, and one of width 17 (two words a row) */
	i = rsc_obj_add(r, 0, 0, G_CICON);
	set_cicon(r, 0, i, 17, 3, NULL, NULL, NULL);
	i = rsc_obj_add(r, 0, 0, G_CICON);
	c = set_cicon(r, 0, i, 17, 3, pat, NULL, "w17");
	CHECK(rsc_cicon_add_version(c, 3, pat + 33, pat + 44, NULL, NULL) == 0);
	o = round_trip(r, &len);
	if (o)
		CHECK(rd32(o, hw(o, H_RSSIZE)) == len && hw(o, H_VRSN) == 4);
	rsc_mfree(o);
	/* bad versions: planes out of range, a chain that loops */
	v = rsc_cicon_version(c, 0);
	v->c.num_planes = 0;
	EXPECT_FAIL(r, 0, 0, RSC_ECICON, 0);
	v->c.num_planes = RSC_MAX_PLANES + 1;
	EXPECT_FAIL(r, 0, 0, RSC_ECICON, 0);
	CHECK(rsc_cicon_add_version(c, RSC_MAX_PLANES + 1, pat, pat, NULL, NULL) == -1);
	v->c.num_planes = 3;
	v->c.next_res = &v->c;
	EXPECT_FAIL(r, 0, 0, RSC_ECICON, 0);
	v->c.next_res = NULL;
	/* num_planes or the icon size written directly (against the contract):
	 * refused, not read past the blocks (AddressSanitizer would see it) */
	v->c.num_planes = 4;
	EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
	CHECK(wr(r, 0, 0, &o, &len, &e2) == -1 && strstr(e2.msg, "num_planes") != NULL
	      && strstr(e2.msg, "(tree 0, object 3)") != NULL);
	v->c.num_planes = RSC_MAX_PLANES;
	EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
	v->c.num_planes = 2;
	EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
	v->c.num_planes = 3;
	c->blk.monoblk.ib_wicon = 33;
	EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
	c->blk.monoblk.ib_wicon = 17;
	c->blk.monoblk.ib_hicon = 4;
	EXPECT_FAIL(r, 0, 0, RSC_EARG, 0);
	c->blk.monoblk.ib_hicon = 3;
	c->blk.monoblk.ib_wicon = 32;		/* the same plane size: accepted */
	o = round_trip(r, &len);
	rsc_mfree(o);
	c->blk.monoblk.ib_wicon = 17;
	/* exactly RSC_MAX_PLANES planes, with selected planes */
	CHECK(rsc_cicon_add_version(c, RSC_MAX_PLANES, pat, pat + 1, pat + 2, pat + 3) == 1);
	o = round_trip(r, &len);
	if (o)
	{
		blk = rd32(o, hw(o, H_RSSIZE) + 12 + 2 * 4);		/* the third icon */
		blk += 38 + 2 * 12 + 12 + 22 + 12 * 4;			/* its second version */
		CHECK(rd16(o, blk) == RSC_MAX_PLANES && rd32(o, blk + 10) == 1);
		CHECK(blk + 22 + 2 * 12 * (RSC_MAX_PLANES + 1) == len);
	}
	rsc_mfree(o);
	rsc_free(r);
	/* RSC_MAX_VERSIONS versions round trip; one more (linked behind the
	 * model's back) is refused, as the reader would refuse it */
	r = rsc_new();
	rsc_tree_add(r, 1);
	i = rsc_obj_add(r, 0, 0, G_CICON);
	c = set_cicon(r, 0, i, 16, 1, pat, pat + 1, "cap");
	for (k = 0; k < RSC_MAX_VERSIONS; k++)
		CHECK(rsc_cicon_add_version(c, 1 + k % RSC_MAX_PLANES, pat + k, pat, k % 2 ? pat : NULL,
					    k % 2 ? pat : NULL) == k);
	CHECK(rsc_cicon_add_version(c, 1, pat, pat, NULL, NULL) == -1);
	o = round_trip(r, &len);
	CHECK(o && rd32(o, rd32(o, hw(o, H_RSSIZE) + 12) + 34) == RSC_MAX_VERSIONS);
	rsc_mfree(o);
	{
		struct rsc_cicon_version	extra;

		memset(&extra, 0, sizeof extra);
		extra.c.num_planes = 1;
		c->last->c.next_res = &extra.c;
		EXPECT_FAIL(r, 0, 0, RSC_ECICON, 0);
		c->last->c.next_res = NULL;
	}
	rsc_free(r);
	/* more than 254 colour icons (the old limit) */
	r = rsc_new();
	rsc_tree_add(r, 1);
	for (k = 0; k < 300; k++)
	{
		i = rsc_obj_add(r, 0, 0, G_CICON);
		snprintf(text, sizeof text, "i%d", k);
		c = set_cicon(r, 0, i, 16, 1, pat + k, pat + k + 1, text);
		if (k % 3 == 0)
			rsc_cicon_add_version(c, 1 + k % 4, pat + k, pat, k % 2 ? pat : NULL, k % 2 ? pat : NULL);
	}
	o = round_trip(r, &len);
	if (o)
	{
		cb = hw(o, H_RSSIZE);
		CHECK(rd32(o, cb + 12 + 4 * 300) == FILE_NULL);
		CHECK(sequential_table(o, len));
		CHECK(rd32(o, hw(o, H_OBJECT) + 300 * 24 + 12) == 299);
	}
	rsc_mfree(o);
	rsc_free(r);
}

/* ---- (g) a mini-fuzz of edits ------------------------------------------------------------------- */

static uint64_t rng = 0x9e3779b97f4a7c15ULL;

static unsigned rnd(unsigned n)
{
	rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
	return n ? (unsigned)(rng >> 33) % n : 0;
}

static const unsigned	types[] = {
	G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE, G_USERDEF, G_IBOX, G_BUTTON, G_BOXCHAR, G_STRING,
	G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE, G_CICON, 0x1234, G_ICON | 0x4100, G_STRING | 0x2200
};

static const char *rstr(char *buf)
{
	int	n = (int)rnd(20), k;

	if (rnd(6) == 0)
		return NULL;
	for (k = 0; k < n; k++)
		buf[k] = (char)('a' + rnd(26));
	buf[n] = '\0';
	return buf;
}

static const short *rdata(void)
{
	return rnd(4) == 0 ? NULL : pat + rnd(1000);
}

/* the object an edit gave a coordinate without file form, or -1 */
static int geom_t = -1, geom_i = -1;

static void edit(struct rsc *r)
{
	char	b1[32], b2[32], b3[32];
	int	nt = rsc_ntrees(r), t = (int)rnd((unsigned)nt), n = 0, i, cw, ch;
	struct rsc_cicon	*c;

	rsc_cell(r, &cw, &ch);
	if (nt)
		rsc_tree_objects(r, t, &n);
	i = (int)rnd((unsigned)n);
	switch (rnd(20))
	{
	case 0:
	case 1:
	case 2:
		if (nt && n < 60)
			rsc_obj_add(r, t, i, types[rnd(sizeof types / sizeof types[0])]);
		break;
	case 3:
		if (nt)
			rsc_obj_delete(r, t, i);
		break;
	case 4:
		if (nt)
			rsc_obj_set_type(r, t, i, types[rnd(sizeof types / sizeof types[0])]);
		break;
	case 5:
		if (nt)
			rsc_obj_set_flags(r, t, i, rnd(0x10000) & (rnd(3) ? ~(unsigned)OF_INDIRECT : 0xffff));
		break;
	case 6:
		if (nt)
			rsc_obj_set_state(r, t, i, rnd(0x10000));
		break;
	case 7:
		if (nt && !cw)
			rsc_obj_set_geom(r, t, i, (short)rnd(0x10000), (short)rnd(0x10000),
					 (short)rnd(0x10000), (short)rnd(0x10000));
		else if (nt)
		{
			/* at 8x16 the reader's range is -127 .. 2168 and 4208;
			 * now and then one past it */
			int	bad = rnd(40) == 0;

			if (rsc_obj_set_geom(r, t, i, (int)rnd(2296) - 127, (int)rnd(4336) - 127,
					     bad ? 2169 : (int)rnd(2296) - 127, (int)rnd(4336) - 127) == 0 && bad)
			{
				geom_t = t;
				geom_i = i;
			}
		}
		break;
	case 8:
		if (nt)
			rsc_obj_set_str(r, t, i, (enum rsc_str_field)rnd(5), rstr(b1));
		break;
	case 9:
		if (nt)
		{
			if (rnd(5) == 0)
				rsc_obj_set_tedinfo(r, t, i, NULL);
			else
				set_tedinfo(r, t, i, rstr(b1), rstr(b2), rstr(b3));
		}
		break;
	case 10:
		if (nt)
		{
			ICONBLK	ib;

			memset(&ib, 0, sizeof ib);
			ib.ib_wicon = (short)rnd(40);
			ib.ib_hicon = (short)rnd(10);
			ib.ib_pmask = (short *)rdata();
			ib.ib_pdata = (short *)rdata();
			ib.ib_ptext = (char *)rstr(b1);
			ib.ib_char = (short)rnd(0x10000);
			rsc_obj_set_iconblk(r, t, i, rnd(5) ? &ib : NULL);
		}
		break;
	case 11:
		if (nt)
		{
			BITBLK	bb = { (short *)rdata(), (short)rnd(9), (short)rnd(9), 1, 2, (short)rnd(16) };

			rsc_obj_set_bitblk(r, t, i, rnd(5) ? &bb : NULL);
		}
		break;
	case 12:
		if (nt)
		{
			ICONBLK	ib;

			memset(&ib, 0, sizeof ib);
			ib.ib_wicon = (short)rnd(40);
			ib.ib_hicon = (short)rnd(10);
			ib.ib_pmask = (short *)rdata();
			ib.ib_pdata = (short *)rdata();
			rsc_obj_set_cicon(r, t, i, rnd(5) ? &ib : NULL, rnd(2) ? "cicon" : NULL);
		}
		break;
	case 13:
		if (nt && (c = rsc_obj_cicon(r, t, i)) != NULL)
		{
			if (rnd(3))
			{
				int	v = rsc_cicon_add_version(c, 1 + (int)rnd(RSC_MAX_PLANES), pat + rnd(100),
								  pat + rnd(100), rnd(2) ? pat : NULL, NULL);

				if (v < 0)
					rsc_cicon_add_version(c, 1 + (int)rnd(8), pat, pat, pat, pat);
				c->file_head[rnd(3)] = (uint32_t)rnd(0x10000) * 0x10001U;
			}
			else
				rsc_cicon_remove_version(c, (int)rnd(3));
		}
		break;
	case 14:
		if (rnd(3) || rsc_nfrstr(r) == 0)
			rsc_frstr_add(r, rstr(b1) ? b1 : "");
		else if (rnd(2))
			rsc_frstr_delete(r, (int)rnd((unsigned)rsc_nfrstr(r)));
		else
			rsc_frstr_set(r, (int)rnd((unsigned)rsc_nfrstr(r)), "set");
		break;
	case 15:
		if (rnd(3) || rsc_nfrimg(r) == 0)
		{
			BITBLK	bb = { (short *)rdata(), (short)rnd(7), (short)rnd(7), 0, 0, 1 };

			rsc_frimg_add(r, &bb);
		}
		else if (rnd(2))
			rsc_frimg_delete(r, (int)rnd((unsigned)rsc_nfrimg(r)));
		else
			rsc_frimg_resize(r, (int)rnd((unsigned)rsc_nfrimg(r)), (int)rnd(7), (int)rnd(7));
		break;
	case 16:
		if (nt < 6 && rnd(2))
			rsc_tree_add(r, 1);
		else if (nt > 1)
			rsc_tree_delete(r, t);
		break;
	case 17:
		if (nt)
			rsc_obj_set_value(r, t, i, (uint32_t)rnd(0x10000) << 16 | rnd(0x10000));
		break;
	case 18:
		if (nt && rnd(2))
			rsc_obj_resize_icon(r, t, i, (int)rnd(50), (int)rnd(12));
		else if (nt)
			rsc_obj_resize_bitblk(r, t, i, (int)rnd(9), (int)rnd(9));
		break;
	case 19:
		if (nt && rnd(2))
			rsc_tree_compact(r, t, NULL);
		else
			rsc_set_vrsn(r, rnd(0x10000));
		break;
	}
}

/* 5000 edits, alternately of a model in file form and of one at 8x16,
 * each followed by a write, a read and a comparison. */
static void test_fuzz(void)
{
	struct rsc	*m[2], *r, *r2;
	struct rsc_error	e;
	uint8_t	*o;
	size_t	len;
	long	live;
	int	k, bad = 0, ncicon = 0, refused = 0, cw;
	unsigned	maxobj = 0;
	size_t	bytes = 0;

	m[0] = build_sample();
	m[1] = build_sample();
	CHECK(rsc_set_cell(m[1], 8, 16) == 0);
	CHECK(rsc_obj_set_geom(m[1], 2, 0, -1, 0, 8, 16) == 0);	/* 0x7fff has no file form */
	for (k = 0; k < 5000 && bad < 5; k++)
	{
		r = m[k & 1];
		cw = k & 1 ? 8 : 0;
		geom_t = geom_i = -1;
		edit(r);
		live = rsc_test_live_blocks();
		if (wr(r, cw, cw * 2, &o, &len, &e))
		{
			CHECK(e.code != RSC_OK && o == NULL && rsc_test_live_blocks() == live);
			if (geom_t >= 0 && e.code == RSC_ETOOBIG && e.offset >= 36 && strstr(e.msg, "ob_width"))
			{
				/* the coordinate just set is refused: put it back */
				refused++;
				CHECK(rsc_obj_set_geom(r, geom_t, geom_i, 0, 0, 8, 16) == 0);
				continue;
			}
			/* otherwise edits through the API cannot make an unwritable resource */
			fprintf(stderr, "fuzz %d: write failed: %s\n", k, e.msg);
			bad++;
			continue;
		}
		if (geom_t >= 0)
		{
			/* the object was unreachable: its coordinates are not written */
			int	n;

			CHECK(rsc_tree_preorder(rsc_tree_objects(r, geom_t, &n), n, NULL) >= 0);
			CHECK(rsc_obj_set_geom(r, geom_t, geom_i, 0, 0, 8, 16) == 0);
		}
		bytes += len;
		ncicon += (hw(o, H_VRSN) & RSC_VRSN_FORMAT_BITS) != 0;
		if (hw(o, H_NOBS) > maxobj)
			maxobj = hw(o, H_NOBS);
		r2 = rd(o, len, cw, cw * 2, &e);
		if (!r2 || !same_model(r, r2))
		{
			fprintf(stderr, "fuzz %d: %s\n", k, r2 ? "content differs" : e.msg);
			bad++;
		}
		else if (!fixed_point("fuzz rewrite", o, len, cw, cw * 2))
			bad++;		/* write(read(write(m))) == write(m) for any edited m */
		else if (!sequential_table(o, len))
		{
			fprintf(stderr, "fuzz %d: the colour icon table is not sequential\n", k);
			bad++;
		}
		rsc_free(r2);
		rsc_mfree(o);
	}
	CHECK(bad == 0 && k == 5000 && refused > 0);
	printf("  fuzz: %d edits (half at 8x16) written and read back, %zu bytes, up to %u objects, "
	       "%d with colour icons, %d coordinates refused\n", k, bytes, maxobj, ncicon, refused);
	rsc_free(m[0]);
	rsc_free(m[1]);
}

/* ---- (h) real files, when RSC_TEST_EXTERNAL names a directory ---------------------- */

/* What r holds, on one line. */
static void summary(const struct rsc *r, char *buf, size_t size)
{
	struct rsc_cicon	*c;
	int	t, k, v, n, nobj = 0, nci = 0, vmin = 0, vmax = 0;
	unsigned	planes = 0;
	size_t	at;

	for (t = 0; t < rsc_ntrees(r); t++)
		for (rsc_tree_objects(r, t, &n), k = 0; k < n; k++)
		{
			nobj++;
			c = rsc_obj_cicon(r, t, k);
			if (!c)
				continue;
			v = rsc_cicon_nversions(c);
			vmin = nci == 0 || v < vmin ? v : vmin;
			vmax = v > vmax ? v : vmax;
			nci++;
			while (v-- > 0)
				planes |= 1u << (rsc_cicon_version(c, v)->c.num_planes & 31);
		}
	at = (size_t)snprintf(buf, size, "%d trees, %d objects, %d free strings, %d free images, "
			      "%d colour icons", rsc_ntrees(r), nobj, rsc_nfrstr(r), rsc_nfrimg(r), nci);
	if (nci && at < size)
	{
		at += (size_t)snprintf(buf + at, size - at, " (%d..%d versions, planes", vmin, vmax);
		for (k = 0; k < 32 && at < size; k++)
			if (planes & 1u << k)
				at += (size_t)snprintf(buf + at, size - at, " %d", k);
		if (at < size)
			snprintf(buf + at, size - at, ")");
	}
}

/* The file at path read with the cell size cw x ch, written, read back
 * (the same content) and written again (the same bytes). */
static void external_file(const char *path, int cw, int ch)
{
	struct file	f = load_path(path);
	struct rsc	*r, *r2 = NULL;
	struct rsc_error	e;
	uint8_t	*o1 = NULL, *o2 = NULL;
	size_t	l1 = 0, l2 = 0;
	char	sum[256];
	const char	*name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	int	same = 0, fixed = 0;

	r = rd(f.b, f.len, cw, ch, &e);
	if (!r)
	{
		printf("  external %s at %dx%d: refused: %s\n", name, cw, ch, e.msg);
		CHECK(r != NULL);
		free(f.b);
		return;
	}
	summary(r, sum, sizeof sum);
	if (wr(r, cw, ch, &o1, &l1, &e) == 0)
	{
		r2 = rd(o1, l1, cw, ch, &e);
		same = r2 && same_model(r, r2);
		fixed = r2 && wr(r2, cw, ch, &o2, &l2, &e) == 0 && same_bytes(path, o2, l2, o1, l1);
	}
	else
		printf("  external %s at %dx%d: write failed: %s\n", name, cw, ch, e.msg);
	printf("  external %s at %dx%d: %s; %zu bytes in, %zu out; same content %s, fixed point %s\n",
	       name, cw, ch, sum, f.len, l1, same ? "yes" : "no", fixed ? "yes" : "no");
	CHECK(same && fixed);
	rsc_mfree(o1);
	rsc_mfree(o2);
	rsc_free(r);
	rsc_free(r2);
	free(f.b);
}

/* Every *.rsc in the directory RSC_TEST_EXTERNAL names (not its
 * subdirectories), at 8x16 and in file form; nothing when it is not set.
 * Real files are not part of the tree: this is for checking them by
 * hand. */
static void test_external(void)
{
	const char	*dir = getenv("RSC_TEST_EXTERNAL");
	DIR	*dp;
	struct dirent	*de;
	char	path[1024];
	size_t	n;

	if (!dir || !*dir)
		return;
	dp = opendir(dir);
	CHECK(dp != NULL);
	if (!dp)
		return;
	while ((de = readdir(dp)) != NULL)
	{
		n = strlen(de->d_name);
		if (n < 5 || strcmp(de->d_name + n - 4, ".rsc") != 0)
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		external_file(path, 8, 16);
		external_file(path, 0, 0);
	}
	closedir(dp);
}

int main(int argc, char **argv)
{
	static const char	*goldens[] = { "rcs", "rcslow", "cicon" };
	long	live0;
	int	k;

	if (argc > 1)
		golden_dir = argv[1];
	init_pat();
	live0 = rsc_test_live_blocks();
	for (k = 0; k < 3; k++)
	{
		test_golden(goldens[k], 8, 16);
		test_golden(goldens[k], 0, 0);
	}
	test_hand();
	test_sample();
	test_tree_order();
	test_null_data();
	test_tedinfo_lengths();
	test_odd_images();
	test_densest();
	test_limits();
	test_errors();
	test_negative();
	test_cicon_mono_size();
	test_coords();
	test_coord_brute();
	test_coord_files();
	test_alloc();
	test_cicons();
	test_fuzz();
	test_external();
	CHECK(rsc_test_live_blocks() == live0);
	printf("test_write: %ld checks, %ld failures\n", checks, failures);
	return failures ? 1 : 0;
}
