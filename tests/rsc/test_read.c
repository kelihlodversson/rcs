/* TEST_READ.C	host tests of the classic Atari reader (rscatari_read.c):
 *		make rsc-test
 *
 * Reads the files of tests/golden (the directory may be given as the
 * first argument) and files built here, checks what comes out against the
 * bytes of the file (decoded again here, independently), and runs the
 * error paths: truncations, mutations, malformed files and allocation
 * failures.  The expected numbers were taken from the files with xxd and
 * a Python parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

#define CHECK_STR(a, b)	CHECK((a) && (b) && strcmp((a), (b)) == 0)

/* ---- files ------------------------------------------------------------------- */

static const char	*golden_dir = "tests/golden";

struct file
{
	uint8_t	*b;
	size_t	len;
};

static struct file load(const char *name)
{
	struct file	f = { NULL, 0 };
	char	path[512];
	FILE	*fp;
	long	n;

	snprintf(path, sizeof path, "%s/%s", golden_dir, name);
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

static unsigned rd16(const uint8_t *b, size_t off)
{
	return (unsigned)b[off] << 8 | b[off + 1];
}

static short srd16(const uint8_t *b, size_t off)
{
	return (short)rd16(b, off);
}

static uint32_t rd32(const uint8_t *b, size_t off)
{
	return (uint32_t)rd16(b, off) << 16 | rd16(b, off + 2);
}

/* fix_chpos, written again */
static int fix(unsigned v, int cell)
{
	int	hi = (int)(v >> 8) & 0xff;
	int	c = (int)(v & 0xff) * cell;

	return hi > 128 ? c - (256 - hi) : c + hi;
}

/* the checksum the Python side computed: s = s * 31 + word, per word */
static uint32_t cksum(const short *w, size_t nwords)
{
	uint32_t	s = 0;
	size_t	k;

	for (k = 0; k < nwords; k++)
		s = s * 31 + (unsigned short)w[k];
	return s;
}

/* Reads len bytes of b from an exactly sized copy (so that the sanitizer
 * sees any read past the end). */
static struct rsc *read_copy(const uint8_t *b, size_t len, int cw, int ch, struct rsc_error *err)
{
	uint8_t	*copy = malloc(len ? len : 1);
	struct rsc	*r;

	memcpy(copy, b, len);
	r = rsc_atari_read(copy, len, cw, ch, err);
	free(copy);
	return r;
}

/* ---- comparing a model with its file ------------------------------------------ */

#define FILE_NULL	0xffffffffUL

/* The objects of tree t are those of the file run at root (fields, links,
 * geometry converted with cw/ch, presence of the spec). */
static void compare_tree(struct rsc *r, int t, const struct file *f, size_t root, int cw, int ch)
{
	int	n, i, ok = 1;
	OBJECT	*o = rsc_tree_objects(r, t, &n);

	CHECK(o && rsc_tree_preorder(o, n, NULL) == n);
	for (i = 0; o && i < n; i++)
	{
		size_t	p = root + (size_t)i * 24;
		uint32_t	spec = rd32(f->b, p + 12);
		enum rsc_spec_kind	k = rsc_obj_spec_kind(&o[i]);

		ok &= o[i].ob_next == (i ? srd16(f->b, p) : -1);
		ok &= o[i].ob_head == srd16(f->b, p + 2) && o[i].ob_tail == srd16(f->b, p + 4);
		ok &= o[i].ob_type == rd16(f->b, p + 6);
		ok &= (o[i].ob_flags & ~OF_LASTOB) == (rd16(f->b, p + 8) & ~OF_LASTOB);
		ok &= !(o[i].ob_flags & OF_LASTOB) == (i != n - 1);
		ok &= o[i].ob_state == rd16(f->b, p + 10);
		if (cw)
			ok &= o[i].ob_x == fix(rd16(f->b, p + 16), cw) && o[i].ob_y == fix(rd16(f->b, p + 18), ch)
			      && o[i].ob_width == fix(rd16(f->b, p + 20), cw)
			      && o[i].ob_height == fix(rd16(f->b, p + 22), ch);
		else
			ok &= o[i].ob_x == srd16(f->b, p + 16) && o[i].ob_y == srd16(f->b, p + 18)
			      && o[i].ob_width == srd16(f->b, p + 20) && o[i].ob_height == srd16(f->b, p + 22);
		if (k == RSC_SPEC_VALUE)
			ok &= (uint32_t)o[i].ob_spec.index == spec;
		else if (k == RSC_SPEC_STRING && spec != FILE_NULL)
			ok &= o[i].ob_spec.free_string && strcmp(o[i].ob_spec.free_string,
								(const char *)f->b + spec) == 0;
		else
			ok &= (o[i].ob_spec.free_string == NULL) == (spec == FILE_NULL);
	}
	CHECK(ok);
}

/* Counts of the records the trees of r have. */
static void count_specs(struct rsc *r, int *nted, int *nib, int *nbb, int *nstr)
{
	int	t, i, n;
	OBJECT	*o;

	*nted = *nib = *nbb = *nstr = 0;
	for (t = 0; t < rsc_ntrees(r); t++)
	{
		o = rsc_tree_objects(r, t, &n);
		for (i = 0; i < n; i++)
		{
			switch (rsc_obj_spec_kind(&o[i]))
			{
			case RSC_SPEC_TEDINFO:
				*nted += o[i].ob_spec.tedinfo != NULL;
				break;
			case RSC_SPEC_ICONBLK:
				*nib += o[i].ob_spec.iconblk != NULL;
				break;
			case RSC_SPEC_BITBLK:
				*nbb += o[i].ob_spec.bitblk != NULL;
				break;
			case RSC_SPEC_STRING:
				*nstr += o[i].ob_spec.free_string != NULL;
				break;
			default:
				break;
			}
		}
	}
}

/* Every tree of r against the file. */
static void compare_all(struct rsc *r, const struct file *f, int cw, int ch)
{
	int	t;
	unsigned	trindex = rd16(f->b, 18);

	CHECK(rsc_ntrees(r) == (int)rd16(f->b, 22));
	for (t = 0; t < rsc_ntrees(r); t++)
		compare_tree(r, t, f, rd32(f->b, trindex + 4 * (size_t)t), cw, ch);
}

/* ---- the goldens ---------------------------------------------------------------- */

static const int	rcs_runs[36] =
{
	58, 13, 7, 15, 12, 7, 17, 5, 17, 14, 20, 10, 6, 1, 1, 24, 9, 6,
	23, 6, 12, 9, 18, 23, 23, 3, 26, 10, 13, 15, 9, 17, 6, 4, 2, 36
};

static void test_rcs(const char *name, int low)
{
	struct file	f = load(name);
	struct rsc_error	err;
	struct rsc	*r;
	int	cell, t, n, nted, nib, nbb, nstr;
	OBJECT	*o;
	BITBLK	*bb;
	TEDINFO	*te;
	ICONBLK	*ib;

	for (cell = 0; cell < 2; cell++)
	{
		int	cw = cell ? 8 : 0, ch = cell ? 16 : 0;

		r = rsc_atari_read(f.b, f.len, cw, ch, &err);
		CHECK(r != NULL && err.code == RSC_OK);
		if (!r)
		{
			fprintf(stderr, "%s: %s\n", name, err.msg);
			continue;
		}
		CHECK(rsc_vrsn(r) == (low ? 0u : 1u));
		rsc_cell(r, &t, &n);
		CHECK(t == cw && n == ch);
		CHECK(rsc_ntrees(r) == 36);
		for (t = 0; t < 36; t++)
		{
			rsc_tree_objects(r, t, &n);
			CHECK(n == rcs_runs[t]);
		}
		compare_all(r, &f, cw, ch);
		count_specs(r, &nted, &nib, &nbb, &nstr);
		CHECK(nted == 26 && nib == 9 && nbb == 35 && nstr == 290);
		CHECK(rsc_nfrstr(r) == 39 && rsc_nfrimg(r) == 2);
		CHECK_STR(rsc_frstr(r, 0), "UNTITLED.RSC ");
		CHECK_STR(rsc_frstr(r, 38), "[3][There is not enough memory|available to use the Icon|Editor.][  OK  ]");
		bb = rsc_frimg(r, 0);
		CHECK(bb->bi_wb == 6 && bb->bi_hl == (low ? 24 : 44) && bb->bi_color == 1);
		CHECK(cksum(bb->bi_pdata, (size_t)(bb->bi_wb * bb->bi_hl / 2)) == (low ? 0xdfa30120u : 0x2316fc1eu));
		bb = rsc_frimg(r, 1);
		CHECK(cksum(bb->bi_pdata, (size_t)(bb->bi_wb * bb->bi_hl / 2)) == (low ? 0x4c576427u : 0xfc7720f7u));
		/* tree 0: the menu */
		o = rsc_tree_objects(r, 0, &n);
		CHECK(o[0].ob_type == G_IBOX && o[0].ob_next == -1);
		CHECK(cell ? o[0].ob_width == 720 && o[0].ob_height == 400
			   : o[0].ob_width == 90 && o[0].ob_height == 25);
		CHECK(cell ? o[2].ob_x == 16 && o[2].ob_height == 19
			   : o[2].ob_x == 2 && o[2].ob_height == 769);
		CHECK(o[1].ob_type == G_BOX && o[1].ob_spec.index == 0x1100);
		CHECK(o[3].ob_type == G_TITLE);
		CHECK_STR(o[3].ob_spec.free_string, " Desk ");
		/* a TEDINFO and an ICONBLK */
		o = rsc_tree_objects(r, 4, &n);
		te = o[3].ob_spec.tedinfo;
		CHECK(te && te->te_font == 3 && te->te_color == 4480 && te->te_thickness == -1);
		CHECK(te && te->te_txtlen == 7 && te->te_tmplen == 12);
		CHECK(te && te->te_ptext && te->te_ptmplt && te->te_pvalid);
		if (te)
		{
			CHECK_STR(te->te_ptext, "______");
			CHECK_STR(te->te_ptmplt, "EDIT:______");
			CHECK_STR(te->te_pvalid, "XXXXXX");
		}
		o = rsc_tree_objects(r, 2, &n);
		ib = o[1].ob_spec.iconblk;
		CHECK(ib && ib->ib_char == 4096 && ib->ib_wicon == 48 && ib->ib_hicon == 24);
		CHECK(ib && ib->ib_xtext == (low ? 0 : 24) && ib->ib_ytext == 24);
		CHECK(ib && ib->ib_wtext == (low ? 48 : 0) && ib->ib_htext == 8);
		CHECK(ib && ib->ib_ptext && ib->ib_ptext[0] == '\0');
		CHECK(ib && cksum(ib->ib_pmask, 72) == 0x8110ab80u && cksum(ib->ib_pdata, 72) == 0x32370b45u);
		rsc_free(r);
		CHECK(rsc_test_live_blocks() == 0);
	}
	free(f.b);
}

static void test_cicon(void)
{
	struct file	f = load("cicon.rsc");
	struct rsc_error	err;
	struct rsc	*r;
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	OBJECT	*o;
	int	cell, n, k, ok;

	for (cell = 0; cell < 2; cell++)
	{
		r = rsc_atari_read(f.b, f.len, cell ? 8 : 0, cell ? 16 : 0, &err);
		CHECK(r != NULL);
		if (!r)
		{
			fprintf(stderr, "cicon.rsc: %s\n", err.msg);
			continue;
		}
		CHECK(rsc_vrsn(r) == 0 && rsc_ntrees(r) == 1);
		compare_all(r, &f, cell ? 8 : 0, cell ? 16 : 0);
		o = rsc_tree_objects(r, 0, &n);
		CHECK(n == 2 && o[1].ob_type == G_CICON);
		CHECK(cell ? o[1].ob_x == 16 && o[1].ob_y == 16 && o[1].ob_width == 32 && o[1].ob_height == 64
			   : o[1].ob_x == 2 && o[1].ob_y == 1 && o[1].ob_width == 4 && o[1].ob_height == 4);
		c = rsc_obj_cicon(r, 0, 1);
		CHECK(c != NULL);
		if (!c)
			continue;
		CHECK(c->blk.monoblk.ib_wicon == 32 && c->blk.monoblk.ib_hicon == 32);
		CHECK(c->blk.monoblk.ib_ytext == 32 && c->blk.monoblk.ib_wtext == 32 && c->blk.monoblk.ib_htext == 8);
		CHECK_STR(c->blk.monoblk.ib_ptext, "CICON test");
		ok = 1;
		for (k = 0; k < 64; k++)
			ok &= c->blk.monoblk.ib_pdata[k] == -1 && c->blk.monoblk.ib_pmask[k] == -1;
		CHECK(ok);
		CHECK(c->file_head[0] == 0 && c->file_head[1] == 0 && c->file_head[2] == 0);
		CHECK(rsc_cicon_nversions(c) == 1);
		v = rsc_cicon_version(c, 0);
		CHECK(v && v->c.num_planes == 4 && v->c.sel_data == NULL && v->c.sel_mask == NULL);
		if (!v)
			continue;
		for (k = 0; k < 5; k++)
			CHECK(v->file_ptr[k] == 0);
		CHECK(v->c.col_data[0] == -1 && v->c.col_data[1] == 0);
		ok = 1;
		for (k = 0; k < 4 * 64; k++)	/* the data at 0x1a8, the mask after it */
			ok &= v->c.col_data[k] == srd16(f.b, 0x1a8 + 2 * (size_t)k);
		for (k = 0; k < 64; k++)
			ok &= v->c.col_mask[k] == srd16(f.b, 0x3a8 + 2 * (size_t)k);
		CHECK(ok);
		rsc_free(r);
		CHECK(rsc_test_live_blocks() == 0);
	}
	free(f.b);
}

/* ---- error paths ------------------------------------------------------------------ */

/* Every tree of r is sound: root ob_next -1, OF_LASTOB on the last object
 * only, and the walk passes and stays inside the tree. */
static int trees_sound(struct rsc *r)
{
	int	t, i, n, k;
	OBJECT	*o;

	for (t = 0; t < rsc_ntrees(r); t++)
	{
		o = rsc_tree_objects(r, t, &n);
		if (!o || n < 1 || o[0].ob_next != -1)
			return 0;
		for (i = 0; i < n; i++)
			if (!(o[i].ob_flags & OF_LASTOB) != (i != n - 1))
				return 0;
		k = rsc_tree_preorder(o, n, NULL);
		if (k < 1 || k > n)
			return 0;
	}
	return 1;
}

/* A read either succeeds (with sound trees) or fails with a proper code
 * and position; nothing leaks.  Returns 1 for success. */
static int read_clean(const uint8_t *b, size_t len)
{
	struct rsc_error	err;
	struct rsc	*r;
	int	ok;

	memset(&err, 0x55, sizeof err);
	r = read_copy(b, len, 8, 16, &err);
	if (r)
	{
		ok = err.code == RSC_OK && trees_sound(r);
		rsc_free(r);
	}
	else
		ok = err.code > RSC_OK && err.code <= RSC_ETOOBIG && err.code != RSC_ENOMEM
		     && err.code != RSC_EARG && err.offset <= len && strstr(err.msg, "at offset 0x");
	CHECK(ok);
	if (!ok)
		fprintf(stderr, "  len %zu: code %d offset %zu %s\n", len, err.code, err.offset, err.msg);
	CHECK(rsc_test_live_blocks() == 0);
	return r != NULL;
}

static void test_truncation(void)
{
	struct file	f = load("cicon.rsc"), g = load("rcs.rsc");
	size_t	n, k, all = 1;
	static const unsigned	bounds[] = { 36, 7118, 15918, 16074, 16564, 16592, 16600, 16906,
					     17634, 29562, 29706, 29744 };

	for (n = 0; n < f.len; n++)
		all &= !read_clean(f.b, n);
	CHECK(all);
	CHECK(read_clean(f.b, f.len));
	/* rcs.rsc: everything up to the end of the tree index is needed */
	all = 1;
	for (n = 0; n < 100; n++)
		all &= !read_clean(g.b, n);
	for (k = 0; k < sizeof bounds / sizeof bounds[0]; k++)
		for (n = bounds[k] > 40 ? bounds[k] - 40 : 0; n <= bounds[k] + 40 && n <= g.len; n++)
			all &= read_clean(g.b, n) == (n >= 29706);
	for (n = 0; n < g.len; n += 97)
		all &= read_clean(g.b, n) == (n >= 29706);
	CHECK(all);
	free(f.b);
	free(g.b);
}

/* Every bit of every header byte flipped. */
static void flip_header(const char *name)
{
	struct file	f = load(name);
	size_t	k;
	int	bit;

	for (k = 0; k < 36; k++)
		for (bit = 0; bit < 8; bit++)
		{
			f.b[k] ^= (uint8_t)(1 << bit);
			read_clean(f.b, f.len);
			f.b[k] ^= (uint8_t)(1 << bit);
		}
	free(f.b);
}

static uint32_t	seed = 12345;

static uint32_t rnd(void)
{
	seed = seed * 1103515245u + 12345u;
	return seed >> 8;
}

/* count deterministic random mutations of one to three bytes each */
static void mutate(const char *name, int count)
{
	struct file	f = load(name);
	uint8_t	*m = malloc(f.len);
	int	i, j, nm, ok = 0;

	for (i = 0; i < count; i++)
	{
		memcpy(m, f.b, f.len);
		nm = 1 + (int)(rnd() % 3);
		for (j = 0; j < nm; j++)
		{
			size_t	pos = rnd() % f.len;

			if (rnd() & 1)
				m[pos] ^= (uint8_t)(1 << (rnd() % 8));
			else
				m[pos] = (uint8_t)rnd();
		}
		ok += read_clean(m, f.len);
	}
	printf("  %s: %d of %d mutations still read\n", name, ok, count);
	free(m);
	free(f.b);
}

static void test_mutations(void)
{
	flip_header("rcs.rsc");
	flip_header("rcslow.rsc");
	flip_header("cicon.rsc");
	mutate("rcs.rsc", 2000);
	mutate("cicon.rsc", 2000);
}

/* ---- files built here --------------------------------------------------------------- */

static void put16(uint8_t *b, size_t off, unsigned v)
{
	b[off] = (uint8_t)(v >> 8);
	b[off + 1] = (uint8_t)v;
}

static void put32(uint8_t *b, size_t off, uint32_t v)
{
	put16(b, off, v >> 16);
	put16(b, off + 2, v & 0xffff);
}

static void put_obj(uint8_t *b, size_t off, int next, int head, int tail, unsigned type,
		    unsigned flags, uint32_t spec, unsigned x, unsigned y, unsigned w, unsigned h)
{
	put16(b, off, (unsigned)next & 0xffff);
	put16(b, off + 2, (unsigned)head & 0xffff);
	put16(b, off + 4, (unsigned)tail & 0xffff);
	put16(b, off + 6, type);
	put16(b, off + 8, flags);
	put16(b, off + 10, 0);
	put32(b, off + 12, spec);
	put16(b, off + 16, x);
	put16(b, off + 18, y);
	put16(b, off + 20, w);
	put16(b, off + 22, h);
}

/* A small plain file of 154 bytes: a tree (an IBOX with a G_STRING and a
 * G_IMAGE), a free string table with a NULL entry, a free image. */
#define MINI_LEN	154
#define MINI_OBJ	78

static void build_mini(uint8_t *b)
{
	static const unsigned	hdr[18] = { 0, 78, 0, 0, 52, 66, 36, 48, 74, 150,
					    3, 1, 0, 0, 1, 2, 1, 154 };
	int	k;

	memset(b, 0, MINI_LEN + 16);
	for (k = 0; k < 18; k++)
		put16(b, 2 * (size_t)k, hdr[k]);
	memcpy(b + 36, "Hello", 6);
	memcpy(b + 42, "Free", 5);
	b[48] = 0x12;
	b[49] = 0x34;
	b[50] = 0x56;
	b[51] = 0x78;
	put32(b, 52, 48);		/* BITBLK */
	put16(b, 56, 2);
	put16(b, 58, 2);
	put16(b, 64, 1);
	put32(b, 66, 42);		/* free strings */
	put32(b, 70, FILE_NULL);
	put32(b, 74, 52);		/* free images */
	put_obj(b, 78, -1, 1, 2, G_IBOX, 0, 0, 0x0a, 0x05, 0x0a, 0x05);
	put_obj(b, 102, 2, -1, -1, G_STRING, 0, 36, 1, 1, 5, 1);
	put_obj(b, 126, 0, -1, -1, G_IMAGE, OF_LASTOB, 52, 0x0302, 0xff02, 2, 2);
	put32(b, 150, 78);		/* tree index */
}

/* Reads b; returns the error code (RSC_OK on success, the model freed). */
static int read_code(const uint8_t *b, size_t len, size_t *off)
{
	struct rsc_error	err;
	struct rsc	*r = read_copy(b, len, 8, 16, &err);

	*off = err.offset;
	if (r)
	{
		rsc_free(r);
		return RSC_OK;
	}
	CHECK(rsc_test_live_blocks() == 0);
	return err.code;
}

static void test_mini(void)
{
	uint8_t	b[MINI_LEN + 16];
	struct rsc_error	err;
	struct rsc	*r;
	OBJECT	*o;
	BITBLK	*bb;
	size_t	off;
	int	n;

	build_mini(b);
	r = read_copy(b, MINI_LEN, 8, 16, &err);
	CHECK(r != NULL);
	if (r)
	{
		o = rsc_tree_objects(r, 0, &n);
		CHECK(n == 3 && rsc_tree_preorder(o, n, NULL) == 3);
		CHECK_STR(o[1].ob_spec.free_string, "Hello");
		CHECK(o[1].ob_x == 8 && o[1].ob_y == 16 && o[1].ob_width == 40);
		CHECK(o[2].ob_x == 19 && o[2].ob_y == 31);	/* 2 * 8 + 3, 2 * 16 - 1 */
		bb = o[2].ob_spec.bitblk;
		CHECK(bb && bb->bi_pdata && bb->bi_pdata[0] == 0x1234 && bb->bi_pdata[1] == 0x5678);
		CHECK(rsc_nfrstr(r) == 1);	/* the NULL entry is dropped */
		CHECK_STR(rsc_frstr(r, 0), "Free");
		CHECK(rsc_nfrimg(r) == 1 && rsc_frimg(r, 0)->bi_pdata[1] == 0x5678);
		rsc_free(r);
	}

	/* mixed cell sizes, a NULL buffer */
	CHECK(rsc_atari_read(b, MINI_LEN, 8, 0, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_atari_read(b, MINI_LEN, 0, 16, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_atari_read(b, MINI_LEN, -8, -16, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_atari_read(NULL, 0, 8, 16, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_atari_read(b, MINI_LEN, 256, 16, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_test_live_blocks() == 0);
	r = rsc_atari_read(b, 10, 8, 16, NULL);	/* no report wanted */
	CHECK(r == NULL);

	/* the tree root outside the object table, misaligned */
	put32(b, 150, 200);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 150);
	put32(b, 150, 79);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 150);
	put32(b, 150, 150);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 150);
	build_mini(b);

	/* without OF_LASTOB the run ends at the end of the table */
	put16(b, 126 + 8, 0);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_OK);
	/* OF_LASTOB on object 1 cuts the tree before its last child */
	put16(b, 102 + 8, OF_LASTOB);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ELINK && off == MINI_OBJ);
	build_mini(b);
	/* a tree starting at the last object: a lone root */
	put32(b, 150, 126);
	put16(b, 126 + 2, 0xffff);
	put16(b, 126 + 4, 0xffff);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_OK);
	build_mini(b);

	/* a link cycle; an object reached from two parents */
	put16(b, 102, 1);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ECYCLE && off == MINI_OBJ);
	build_mini(b);
	put16(b, 102 + 2, 2);
	put16(b, 102 + 4, 2);
	put16(b, 126, 1);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ECYCLE && off == MINI_OBJ);
	build_mini(b);
	put16(b, 78 + 4, 9);		/* ob_tail out of range */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ELINK && off == MINI_OBJ);
	build_mini(b);

	/* a string without NUL at the end of the buffer, one past the end */
	memcpy(b + MINI_LEN, "abc", 3);
	put32(b, 102 + 12, MINI_LEN);
	CHECK(read_code(b, MINI_LEN + 3, &off) == RSC_ESTRING && off == MINI_LEN);
	CHECK(read_code(b, MINI_LEN + 4, &off) == RSC_OK);	/* with its NUL */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ESTRING && off == 102 + 12);
	build_mini(b);
	put32(b, 102 + 12, FILE_NULL);	/* a NULL string is fine */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_OK);
	build_mini(b);

	/* image data past the end, a BITBLK pointer outside the table */
	put32(b, 52, MINI_LEN - 2);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ETRUNC && off == 52);
	put32(b, 52, 1000);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 52);
	put32(b, 52, FILE_NULL);		/* no data: a blank image */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_OK);
	build_mini(b);
	put32(b, 126 + 12, 53);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 126 + 12);
	build_mini(b);

	/* free string past the end; free image outside the BITBLK table */
	put32(b, 66, 500);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ESTRING && off == 66);
	build_mini(b);
	put32(b, 74, 66);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 74);
	put32(b, 74, FILE_NULL);		/* dropped */
	r = read_copy(b, MINI_LEN, 8, 16, &err);
	CHECK(r && rsc_nfrimg(r) == 0);
	rsc_free(r);
	build_mini(b);

	/* tables past the end */
	put16(b, 20, 4);		/* nobs 4: the table ends at 174 */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_ETRUNC && off == 2);
	build_mini(b);
	put16(b, 18, 1000);		/* trindex */
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 18);
	build_mini(b);

	/* negative BITBLK sizes */
	put16(b, 56, 0xfffe);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 56);
	build_mini(b);
	put16(b, 58, 0xffff);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 58);
	build_mini(b);
	put16(b, 56, 0xfffe);		/* in a free image */
	put16(b, 126 + 6, G_BOX);
	CHECK(read_code(b, MINI_LEN, &off) == RSC_EOFFSET && off == 56);
	build_mini(b);

	/* cell sizes: RSC_MAX_CELL (127) is the largest */
	CHECK(RSC_MAX_CELL == 127);
	CHECK(rsc_atari_read(b, MINI_LEN, 128, 16, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_atari_read(b, MINI_LEN, 8, 128, &err) == NULL && err.code == RSC_EARG);
	r = rsc_atari_read(b, MINI_LEN, 127, 127, &err);
	CHECK(r != NULL);
	if (r)
	{
		o = rsc_tree_objects(r, 0, &n);
		CHECK(o[0].ob_width == 10 * 127);
		rsc_free(r);
	}

	/* the same root twice in the tree index (moved to the end) */
	put16(b, 18, MINI_LEN);
	put16(b, 22, 2);
	put32(b, MINI_LEN, MINI_OBJ);
	put32(b, MINI_LEN + 4, MINI_OBJ);
	CHECK(read_code(b, MINI_LEN + 8, &off) == RSC_ELINK && off == MINI_LEN + 4);
	/* object 1 as the root of a second tree: the first ends before it,
	 * and its links leave it */
	put32(b, MINI_LEN + 4, MINI_OBJ + 24);
	CHECK(read_code(b, MINI_LEN + 8, &off) == RSC_ELINK && off == MINI_OBJ);
	/* three lone objects without OF_LASTOB are three trees */
	put_obj(b, 78, -1, -1, -1, G_IBOX, 0, 0, 1, 1, 1, 1);
	put_obj(b, 102, -1, -1, -1, G_STRING, 0, 36, 2, 2, 2, 2);
	put_obj(b, 126, -1, -1, -1, G_IMAGE, 0, 52, 3, 3, 3, 3);
	put16(b, 22, 3);
	put32(b, MINI_LEN + 4, 102);
	put32(b, MINI_LEN + 8, 126);
	r = read_copy(b, MINI_LEN + 12, 0, 0, &err);
	CHECK(r && rsc_ntrees(r) == 3);
	if (r)
	{
		for (n = 0; n < 3; n++)
		{
			int	cnt;

			o = rsc_tree_objects(r, n, &cnt);
			CHECK(cnt == 1 && o[0].ob_x == n + 1 && trees_sound(r));
		}
		CHECK_STR(rsc_tree_objects(r, 1, NULL)[0].ob_spec.free_string, "Hello");
		rsc_free(r);
	}
	/* a root that has OF_LASTOB is a tree of one object even with the next
	 * objects not roots: the second and third object are then ignored */
	put16(b, 22, 1);
	put16(b, 78 + 8, OF_LASTOB);
	r = read_copy(b, MINI_LEN + 12, 0, 0, &err);
	CHECK(r && rsc_ntrees(r) == 1);
	if (r)
	{
		rsc_tree_objects(r, 0, &n);
		CHECK(n == 1);
		rsc_free(r);
	}
	build_mini(b);

	/* an unreachable object of a run: its spec is not decoded */
	put16(b, 78 + 4, 1);		/* the root's only child is 1 */
	put16(b, 102, 0);
	put32(b, 126 + 12, 9999);	/* a bad BITBLK pointer in object 2 */
	r = read_copy(b, MINI_LEN, 0, 0, &err);
	CHECK(r != NULL);
	if (r)
	{
		o = rsc_tree_objects(r, 0, &n);
		CHECK(n == 3 && o[2].ob_type == G_IMAGE && o[2].ob_spec.bitblk == NULL);
		CHECK(rsc_tree_preorder(o, n, NULL) == 2);
		rsc_free(r);
	}
	build_mini(b);
	CHECK(rsc_test_live_blocks() == 0);
}

/* A colour icon file: a tree of a root and nobj G_CICON children (object
 * i has index first + i - 1, or 0 for all when first < 0), a table of
 * ncicon entries naming as many copies of one block (with the terminator
 * unless noterm, which puts the table at the end); the block: 16 x 2, two
 * versions (2 planes; 1 plane with a selected image).  Returns the
 * length. */
#define CBLK_LEN	130

static void put_cblk(uint8_t *b, size_t blk)
{
	size_t	p;
	int	i;

	put32(b, blk, 0x11111111);
	put32(b, blk + 4, 0x22222222);
	put32(b, blk + 8, 0x33333333);
	put16(b, blk + 12, 0x1041);
	put16(b, blk + 22, 16);
	put16(b, blk + 24, 2);
	put16(b, blk + 28, 2);
	put16(b, blk + 30, 16);
	put16(b, blk + 32, 8);
	put32(b, blk + 34, 2);
	p = blk + 38;
	put16(b, p, 0xaaaa);		/* mono data, mask */
	put16(b, p + 2, 0x5555);
	put16(b, p + 4, 0xffff);
	put16(b, p + 6, 0xfffe);
	memcpy(b + p + 8, "Mini", 4);
	p += 8 + 12;
	put16(b, p, 2);			/* version 1: 2 planes */
	put32(b, p + 2, 1);
	put32(b, p + 6, 2);
	put32(b, p + 18, 1);
	for (i = 0; i < 6; i++)
		put16(b, p + 22 + 2 * (size_t)i, 0x0100 + (unsigned)i);
	p += 22 + 12;
	put16(b, p, 1);			/* version 2: 1 plane, selected */
	put32(b, p + 10, 7);
	for (i = 0; i < 8; i++)
		put16(b, p + 22 + 2 * (size_t)i, 0x0200 + (unsigned)i);
}

static size_t build_cfile(uint8_t *b, size_t room, int nobj, int first, int ncicon, int noterm)
{
	size_t	objs = 36, tr = objs + 24 * ((size_t)nobj + 1), ext = tr + 4, tab = ext + 12;
	size_t	blk = tab + 4 * ((size_t)ncicon + 1), end;
	int	i;

	if (noterm)
	{
		blk = tab;		/* the blocks first, the table at the very end */
		tab = blk + CBLK_LEN * (size_t)ncicon;
		end = tab + 4 * (size_t)ncicon;
	}
	else
		end = blk + CBLK_LEN * (size_t)ncicon;
	if (end > room)
		exit(2);
	memset(b, 0, end);
	put16(b, 0, 4 | 1);
	put16(b, 2, (unsigned)objs);
	put16(b, 12, 36);
	put16(b, 14, 36);
	put16(b, 18, (unsigned)tr);
	put16(b, 20, (unsigned)nobj + 1);
	put16(b, 22, 1);
	put16(b, 34, (unsigned)ext);
	put_obj(b, objs, -1, nobj ? 1 : -1, nobj, G_IBOX, nobj ? 0 : OF_LASTOB, 0, 0, 0, 10, 5);
	for (i = 1; i <= nobj; i++)
		put_obj(b, objs + 24 * (size_t)i, i < nobj ? i + 1 : 0, -1, -1, G_CICON,
			i == nobj ? OF_LASTOB : 0, first < 0 ? 0 : (uint32_t)(first + i - 1), 0, 0, 4, 2);
	put32(b, tr, (uint32_t)objs);
	put32(b, ext, (uint32_t)end);
	put32(b, ext + 4, (uint32_t)tab);
	for (i = 0; i < ncicon; i++)
	{
		put32(b, tab + 4 * (size_t)i, (uint32_t)(blk + CBLK_LEN * (size_t)i));
		put_cblk(b, blk + CBLK_LEN * (size_t)i);
	}
	if (!noterm)
		put32(b, tab + 4 * (size_t)ncicon, FILE_NULL);
	return end;
}

static void test_cfile(void)
{
	static uint8_t	b[65536];
	struct rsc_error	err;
	struct rsc	*r;
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	size_t	len, off, blk;
	int	n, k;

	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r != NULL);
	if (!r)
		fprintf(stderr, "cfile: %s\n", err.msg);
	c = r ? rsc_obj_cicon(r, 0, 1) : NULL;
	CHECK(c != NULL);
	if (c)
	{
		CHECK(rsc_vrsn(r) == 1);
		CHECK(c->file_head[0] == 0x11111111 && c->file_head[1] == 0x22222222
		      && c->file_head[2] == 0x33333333);
		CHECK(c->blk.monoblk.ib_char == 0x1041 && c->blk.monoblk.ib_wicon == 16);
		CHECK(c->blk.monoblk.ib_pdata[0] == (short)0xaaaa && c->blk.monoblk.ib_pdata[1] == 0x5555);
		CHECK(c->blk.monoblk.ib_pmask[0] == -1 && c->blk.monoblk.ib_pmask[1] == -2);
		CHECK_STR(c->blk.monoblk.ib_ptext, "Mini");
		CHECK(rsc_cicon_nversions(c) == 2);
		v = rsc_cicon_version(c, 0);
		CHECK(v->c.num_planes == 2 && !v->c.sel_data && !v->c.sel_mask);
		CHECK(v->file_ptr[0] == 1 && v->file_ptr[1] == 2 && v->file_ptr[2] == 0
		      && v->file_ptr[3] == 0 && v->file_ptr[4] == 1);
		CHECK(v->c.col_data[0] == 0x100 && v->c.col_data[3] == 0x103);
		CHECK(v->c.col_mask[0] == 0x104 && v->c.col_mask[1] == 0x105);
		v = rsc_cicon_version(c, 1);
		CHECK(v->c.num_planes == 1 && v->c.sel_data && v->c.sel_mask);
		CHECK(v->file_ptr[2] == 7 && v->file_ptr[4] == 0);
		CHECK(v->c.col_data[1] == 0x201 && v->c.col_mask[0] == 0x202);
		CHECK(v->c.sel_data[0] == 0x204 && v->c.sel_mask[1] == 0x207);
	}
	rsc_free(r);

	/* truncated at every length */
	n = 1;
	for (off = 0; off < len; off++)
		n &= !read_clean(b, off);
	CHECK(n);

	/* the index out of range; a NULL index; no terminator */
	len = build_cfile(b, sizeof b, 1, 1, 1, 0);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 36 + 24 + 12);
	put32(b, 36 + 24 + 12, FILE_NULL);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r && rsc_obj_cicon(r, 0, 1) == NULL);
	rsc_free(r);
	len = build_cfile(b, sizeof b, 1, 0, 1, 1);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == len - 4);
	/* rssize past the end; a bad total length; a bad table offset */
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	put16(b, 34, 300);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 34);
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	put32(b, 36 + 48 + 4, (uint32_t)len + 1);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 36 + 48 + 4);
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	put32(b, 36 + 48 + 8, 101);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 36 + 48 + 8);
	/* no table at all: a G_CICON object is then an error */
	put32(b, 36 + 48 + 8, 0);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 36 + 24 + 12);
	/* too many planes; the version count larger than the file */
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	blk = 36 + 48 + 4 + 12 + 8;
	put16(b, blk + 38 + 20, 17);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == blk + 58);
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	put32(b, blk + 34, 3);		/* the third version would start at the end */
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == len);
	put32(b, blk + 34, 4);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == blk + 34);
	/* a negative colour icon size */
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	put16(b, blk + 22, 0xfff0);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == blk);
	/* a table that lists a block twice */
	len = build_cfile(b, sizeof b, 1, 0, 2, 0);
	put32(b, 104, rd32(b, 100));	/* the table is at 100 */
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == 104);

	/* more than 254 colour icons */
	len = build_cfile(b, sizeof b, 300, 0, 300, 0);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r != NULL);
	if (r)
	{
		rsc_tree_objects(r, 0, &n);
		CHECK(n == 301);
		for (k = 1; k <= 300; k++)
			CHECK(rsc_obj_cicon(r, 0, k) && rsc_obj_cicon(r, 0, k) != rsc_obj_cicon(r, 0, 1 + k % 300));
		rsc_free(r);
	}
	CHECK(rsc_test_live_blocks() == 0);
}

/* ---- bounds on what a small file can make the reader do -------------------------- */

static uint8_t	big[1200000];

/* Every coordinate word with every cell size 1..RSC_MAX_CELL: a file of
 * 16384 objects (a lone root, the rest unreachable but converted too)
 * whose x, y, w, h are the words 0..65535; the result must be the exact
 * fix_chpos value and fit a short. */
static void test_coords(void)
{
	size_t	tr = 36, objs = 40, n = 16384, k, len = objs + 24 * n;
	struct rsc_error	err;
	struct rsc	*r;
	OBJECT	*o;
	int	cell, cnt, ok = 1;

	memset(big, 0, len);
	put16(big, 2, (unsigned)objs);
	put16(big, 18, (unsigned)tr);
	put16(big, 20, (unsigned)n);
	put16(big, 22, 1);
	put32(big, tr, (uint32_t)objs);
	for (k = 0; k < n; k++)
	{
		put_obj(big, objs + 24 * k, -1, -1, -1, G_BOX, 0, 0, (unsigned)(4 * k),
			(unsigned)(4 * k + 1), (unsigned)(4 * k + 2), (unsigned)(4 * k + 3));
	}
	for (cell = 1; cell <= RSC_MAX_CELL; cell++)
	{
		r = rsc_atari_read(big, len, cell, cell, &err);
		ok &= r != NULL;
		o = r ? rsc_tree_objects(r, 0, &cnt) : NULL;
		for (k = 0; o && k < 4 * n; k++)
		{
			long	hi = (long)(k >> 8), want = (long)(k & 0xff) * cell + (hi > 128 ? hi - 256 : hi);
			short	*f = &o[k / 4].ob_x;	/* ob_x, ob_y, ob_width, ob_height */

			ok &= want >= -32768 && want <= 32767 && f[k % 4] == want;
		}
		rsc_free(r);
	}
	CHECK(ok);
	/* the extremes and the sign boundary of the high byte */
	put16(big, 20, 1);
	put_obj(big, objs, -1, -1, -1, G_BOX, OF_LASTOB, 0, 0x80ff, 0x8001, 0x8101, 0xff00);
	r = rsc_atari_read(big, objs + 24, 127, 8, &err);
	CHECK(r != NULL);
	if (r)
	{
		o = rsc_tree_objects(r, 0, NULL);
		CHECK(o[0].ob_x == 32513);		/* 255 * 127 + 128 */
		CHECK(o[0].ob_y == 8 + 128);		/* high byte 128 is +128 */
		CHECK(o[0].ob_width == 127 - 127);	/* high byte 129 is -127 */
		CHECK(o[0].ob_height == -1);		/* high byte 255 is -1 */
		rsc_free(r);
	}
	CHECK(rsc_test_live_blocks() == 0);
}

/* A file of a root and nobj G_TEXT children sharing one TEDINFO whose
 * text has slen characters, and its template and validation tlen each
 * (the TEDINFO at 36, the objects, the tree index, then the strings).
 * Returns the length. */
static size_t build_shared_ted(int nobj, size_t slen, size_t tlen)
{
	size_t	ted = 36, objs = ted + 28, tr, at;
	int	i, k;

	tr = objs + 24 * ((size_t)nobj + 1);
	at = tr + 4;
	memset(big, 0, at + slen + 2 * tlen + 3);
	for (k = 0; k < 3; k++)
	{
		size_t	n = k ? tlen : slen;

		memset(big + at, 'a' + k, n);
		put32(big, ted + 4 * (size_t)k, (uint32_t)at);
		at += n + 1;
	}
	put16(big, 2, (unsigned)objs);
	put16(big, 4, (unsigned)ted);
	put16(big, 12, 36);
	put16(big, 18, (unsigned)tr);
	put16(big, 20, (unsigned)nobj + 1);
	put16(big, 22, 1);
	put16(big, 24, 1);
	put16(big, 34, (unsigned)at);
	put_obj(big, objs, -1, 1, nobj, G_IBOX, 0, 0, 0, 0, 10, 5);
	for (i = 1; i <= nobj; i++)
		put_obj(big, objs + 24 * (size_t)i, i < nobj ? i + 1 : 0, -1, -1, G_TEXT,
			i == nobj ? OF_LASTOB : 0, (uint32_t)ted, 0, 0, 4, 1);
	put32(big, tr, (uint32_t)objs);
	return at;
}

/* A colour icon file in b (room bytes) of one block of nver versions of a
 * 0 x 0 icon, named by every one of ntab table entries.  Returns the
 * length. */
static size_t build_dup_table_in(uint8_t *b, size_t room, long nver, long ntab)
{
	size_t	objs = 36, tr = objs + 24, ext = tr + 4, tab = ext + 12;
	size_t	blk = tab + 4 * (size_t)ntab + 4, p, end;
	long	k;

	end = blk + 38 + 12 + 22 * (size_t)nver;
	if (end > room)
		exit(2);
	memset(b, 0, end);
	put16(b, 0, 4);
	put16(b, 2, (unsigned)objs);
	put16(b, 18, (unsigned)tr);
	put16(b, 20, 1);
	put16(b, 22, 1);
	put16(b, 34, (unsigned)ext);
	put_obj(b, objs, -1, -1, -1, G_IBOX, OF_LASTOB, 0, 0, 0, 10, 5);
	put32(b, tr, (uint32_t)objs);
	put32(b, ext, (uint32_t)end);
	put32(b, ext + 4, (uint32_t)tab);
	for (k = 0; k < ntab; k++)
		put32(b, tab + 4 * (size_t)k, (uint32_t)blk);
	put32(b, tab + 4 * (size_t)ntab, FILE_NULL);
	put32(b, blk + 34, (uint32_t)nver);
	for (k = 0, p = blk + 50; k < nver; k++, p += 22)
		put16(b, p, 1);
	return end;
}

static size_t build_dup_table(long nver, long ntab)
{
	return build_dup_table_in(big, sizeof big, nver, ntab);
}

/* The colour version cap: a block of RSC_MAX_VERSIONS versions reads, one
 * of RSC_MAX_VERSIONS + 1 is refused at the block, and so is the 4 MB file
 * of one block of 190000 versions, at once: the count is checked before
 * any version (it took 20 s when versions were appended at the end of the
 * chain one by one; the clock bound is only a safety net). */
static void test_versions_cap(void)
{
	size_t	room = 4300000, len, off, blk = 36 + 24 + 4 + 12 + 8;
	uint8_t	*b = malloc(room);
	struct rsc_error	err;
	struct rsc	*r;
	clock_t	t0;

	if (!b)
		exit(2);
	len = build_dup_table_in(b, room, RSC_MAX_VERSIONS, 1);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r && rsc_obj_cicon(r, 0, 0) == NULL);	/* the block is only listed */
	rsc_free(r);
	/* named by an object as well: every version decoded */
	put16(b, 36 + 6, G_CICON);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r && rsc_obj_cicon(r, 0, 0) && rsc_cicon_nversions(rsc_obj_cicon(r, 0, 0)) == RSC_MAX_VERSIONS);
	rsc_free(r);
	len = build_dup_table_in(b, room, RSC_MAX_VERSIONS + 1, 1);
	CHECK(read_code(b, len, &off) == RSC_ECICON && off == blk);
	t0 = clock();
	len = build_dup_table_in(b, room, 190000, 1);
	CHECK(len > 4000000);
	r = read_copy(b, len, 0, 0, &err);
	CHECK(r == NULL && err.code == RSC_ECICON && err.offset == blk);
	CHECK(strncmp(err.msg, "too many colour versions", 24) == 0);
	CHECK(clock() - t0 < 2 * CLOCKS_PER_SEC);
	free(b);
	CHECK(rsc_test_live_blocks() == 0);
}

/* A colour icon file of one 16 x 1 block with one version of planes
 * planes; planes_hdr is where its header is.  Returns the length. */
static size_t	planes_hdr;

static size_t build_planes(int planes)
{
	size_t	objs = 36, tr = objs + 48, ext = tr + 4, tab = ext + 12, blk = tab + 8, end;

	memset(big, 0, 4096);
	put16(big, 0, 4);
	put16(big, 2, (unsigned)objs);
	put16(big, 18, (unsigned)tr);
	put16(big, 20, 2);
	put16(big, 22, 1);
	put16(big, 34, (unsigned)ext);
	put_obj(big, objs, -1, 1, 1, G_IBOX, 0, 0, 0, 0, 10, 5);
	put_obj(big, objs + 24, 0, -1, -1, G_CICON, OF_LASTOB, 0, 0, 0, 2, 1);
	put32(big, tr, (uint32_t)objs);
	put32(big, tab, (uint32_t)blk);
	put32(big, tab + 4, FILE_NULL);
	put16(big, blk + 22, 16);
	put16(big, blk + 24, 1);
	put32(big, blk + 34, 1);
	planes_hdr = blk + 38 + 4 + 12;
	put16(big, planes_hdr, (unsigned)planes);
	end = planes_hdr + 22 + 2 * ((size_t)planes + 1);
	put32(big, ext, (uint32_t)end);
	put32(big, ext + 4, (uint32_t)tab);
	return end;
}

static int big_code(size_t len, size_t *off, char *msg)
{
	struct rsc_error	err;
	struct rsc	*r = rsc_atari_read(big, len, 0, 0, &err);

	*off = err.offset;
	if (msg)
		strcpy(msg, err.msg);
	CHECK(r != NULL || rsc_test_live_blocks() == 0);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
	return r ? RSC_OK : err.code;
}

static void test_hostile(void)
{
	char	msg[96];
	size_t	len, off;
	int	n, first_fail = 0;
	clock_t	t0;

	/* many objects sharing one TEDINFO: the budget decides */
	for (n = 1; n <= 60 && !first_fail; n++)
	{
		len = build_shared_ted(n, 1000, 1000);
		if (big_code(len, &off, msg) != RSC_OK)
		{
			first_fail = n;
			CHECK(big_code(len, &off, msg) == RSC_ETOOBIG);
			CHECK(strncmp(msg, "decoded size exceeds the limit", 30) == 0);
		}
	}
	CHECK(first_fail >= 20 && first_fail <= 30);
	len = build_shared_ted(2000, 1000, 1000);
	CHECK(big_code(len, &off, msg) == RSC_ETOOBIG);
	/* a TEDINFO text of 32767 characters: te_txtlen would not fit */
	len = build_shared_ted(1, 32766, 3);
	CHECK(big_code(len, &off, NULL) == RSC_OK);
	len = build_shared_ted(1, 32767, 3);
	CHECK(big_code(len, &off, NULL) == RSC_ETOOBIG && off == 36 + 24);
	/* and the template: te_tmplen */
	len = build_shared_ted(1, 3, 32766);
	CHECK(big_code(len, &off, NULL) == RSC_OK);
	len = build_shared_ted(1, 3, 32767);
	CHECK(big_code(len, &off, NULL) == RSC_ETOOBIG && off == 36 + 26);
	/* RSC_MAX_PLANES planes are taken, one more is not */
	CHECK(big_code(build_planes(RSC_MAX_PLANES), &off, NULL) == RSC_OK);
	CHECK(big_code(build_planes(RSC_MAX_PLANES + 1), &off, NULL) == RSC_ECICON && off == planes_hdr);

	/* many G_CICON objects naming one block */
	len = build_cfile(big, sizeof big, 1000, -1, 1, 0);
	CHECK(big_code(len, &off, msg) == RSC_ETOOBIG);
	CHECK(strncmp(msg, "decoded size exceeds the limit", 30) == 0);
	len = build_cfile(big, sizeof big, 20, -1, 1, 0);
	CHECK(big_code(len, &off, NULL) == RSC_OK);

	/* a table naming one block of RSC_MAX_VERSIONS versions 250000 times:
	 * 1 MB, refused at the second entry, so the block is checked once (the
	 * clock bound is only a safety net) */
	t0 = clock();
	len = build_dup_table(RSC_MAX_VERSIONS, 250000);
	CHECK(len > 1000000);
	CHECK(big_code(len, &off, msg) == RSC_ECICON && off == 36 + 24 + 4 + 12 + 4);
	CHECK(strncmp(msg, "colour icon block listed twice", 30) == 0);
	len = build_dup_table(RSC_MAX_VERSIONS, 1);
	CHECK(big_code(len, &off, NULL) == RSC_OK);
	CHECK(clock() - t0 < 10 * CLOCKS_PER_SEC);

	/* a negative ICONBLK size in rcs.rsc (the first ICONBLK at 16600) */
	{
		struct file	f = load("rcs.rsc");

		put16(f.b, 16600 + 22, 0xffd0);
		CHECK(read_code(f.b, f.len, &off) == RSC_EOFFSET && off == 16600 + 22);
		put16(f.b, 16600 + 22, 48);
		put16(f.b, 16600 + 24, 0xffff);
		CHECK(read_code(f.b, f.len, &off) == RSC_EOFFSET && off == 16600 + 24);
		free(f.b);
	}
}

/* ---- every charge of the budget changes an outcome ---------------------------- */

/* The families of files below are each a small file that the budget
 * refuses once k (objects, versions or table entries) passes a boundary,
 * and each family leans on some charges of the reader: dropping one of
 * them would move the boundary out of its window.  The windows were
 * measured on a 64-bit host (struct sizes enter the charges) and are
 * +-5 %; a family's boundary k comes from a binary search. */

static int	family;		/* which file build_family makes */

/* A tree of a root and n children of type type with spec spec at objs,
 * and the header words for it (the tree index at 36). */
static size_t put_tree(size_t objs, long n, unsigned type, uint32_t spec)
{
	long	i;

	put16(big, 2, (unsigned)objs);
	put16(big, 18, 36);
	put16(big, 20, (unsigned)(n + 1));
	put16(big, 22, 1);
	put32(big, 36, (uint32_t)objs);
	put_obj(big, objs, -1, n ? 1 : -1, n ? (int)n : -1, G_IBOX, n ? 0 : OF_LASTOB, 0, 0, 0, 10, 5);
	for (i = 1; i <= n; i++)
		put_obj(big, objs + 24 * (size_t)i, i < n ? (int)i + 1 : 0, -1, -1, type,
			i == n ? OF_LASTOB : 0, spec, 0, 0, 1, 1);
	return objs + 24 * ((size_t)n + 1);
}

/* A string of n characters at off, with its NUL; returns the end. */
static size_t put_str(size_t off, size_t n)
{
	memset(big + off, 'x', n);
	big[off + n] = 0;
	return off + n + 1;
}

/* A colour icon block at blk: w x h, nver versions of planes 1 (with a
 * selected image when sel); returns the end. */
static size_t put_cblock(size_t blk, int w, int h, long nver, int sel)
{
	size_t	pb = rsc_plane_bytes(w, h), p = blk + 38 + 2 * pb + 12;
	long	v;

	put16(big, blk + 22, (unsigned)w);
	put16(big, blk + 24, (unsigned)h);
	put32(big, blk + 34, (uint32_t)nver);
	for (v = 0; v < nver; v++)
	{
		put16(big, p, 1);
		put32(big, p + 10, sel ? 1 : 0);
		p += 22 + 2 * pb * (sel ? 2 : 1);
	}
	return p;
}

/* A file of the family with k: its length. */
static size_t build_family(long k)
{
	size_t	p, s, end;
	long	i;

	memset(big, 0, sizeof big);
	switch (family)
	{
	case 0:		/* G_STRING objects sharing a string of 100 */
		end = put_tree(140, k, G_STRING, 40);
		put_str(40, 99);
		return end;
	case 1:		/* G_TEXT objects sharing a TEDINFO, three strings of 100 */
	case 2:		/* the same with three empty strings */
		put16(big, 4, 40);
		put16(big, 24, 1);
		for (i = 0; i < 3; i++)
			put32(big, 40 + 4 * (size_t)i, (uint32_t)(68 + 100 * i));
		for (i = 0; i < 3; i++)
			put_str(68 + 100 * (size_t)i, family == 1 ? 99 : 0);
		return put_tree(368, k, G_TEXT, 40);
	case 3:		/* G_ICON objects sharing an ICONBLK 16 x 50 with a text of 200 */
	case 4:		/* the same 0 x 0, no data, no text */
		put16(big, 6, 40);
		put16(big, 26, 1);
		if (family == 3)
		{
			put32(big, 40, 74);
			put32(big, 44, 174);
			put32(big, 48, 274);
			put16(big, 62, 16);
			put16(big, 64, 50);
			put_str(274, 199);
		}
		else
		{
			put32(big, 40, FILE_NULL);
			put32(big, 44, FILE_NULL);
			put32(big, 48, FILE_NULL);
		}
		return put_tree(476, k, G_ICON, 40);
	case 5:		/* G_IMAGE objects sharing a BITBLK of 10 x 10 bytes */
	case 6:		/* the same of 4 x 2 */
		put16(big, 8, 40);
		put16(big, 28, 1);
		put32(big, 40, 54);
		put16(big, 44, family == 5 ? 10 : 4);
		put16(big, 46, family == 5 ? 10 : 2);
		return put_tree(160, k, G_IMAGE, 40);
	case 7:		/* G_CICON objects sharing a block 64 x 64, a selected version */
	case 8:		/* the same 0 x 0 without versions */
	case 9:		/* the same 0 x 0 with one version */
	case 10:	/* the same 0 x 0 with RSC_MAX_VERSIONS versions */
		p = put_tree(40, k, G_CICON, 0);
		put16(big, 0, 4);
		put16(big, 34, (unsigned)p);
		put32(big, p + 4, (uint32_t)(p + 12));
		put32(big, p + 12, (uint32_t)(p + 20));
		put32(big, p + 16, FILE_NULL);
		if (family == 7)
			end = put_cblock(p + 20, 64, 64, 1, 1);
		else
			end = put_cblock(p + 20, 0, 0, family == 8 ? 0 : family == 9 ? 1 : RSC_MAX_VERSIONS, 0);
		put32(big, p, (uint32_t)end);
		return end;
	case 11:	/* k free strings naming one empty string */
	case 12:	/* k free strings naming one string of 30000 */
		s = 40 + 4 * (size_t)k;
		end = put_str(s, family == 11 ? 0 : 30000);
		put16(big, 10, 40);
		put16(big, 30, (unsigned)k);
		for (i = 0; i < k; i++)
			put32(big, 40 + 4 * (size_t)i, (uint32_t)s);
		return end;
	case 14:	/* k empty 0 x 0 blocks, each named by three G_CICON objects */
		put16(big, 0, 4);
		put16(big, 34, 40);
		put32(big, 44, 52);
		p = 52 + 4 * (size_t)k + 4;
		end = put_tree(p, 3 * k, G_CICON, 0);
		for (i = 1; i <= 3 * k; i++)
			put32(big, p + 24 * (size_t)i + 12, (uint32_t)((i - 1) / 3));
		for (i = 0; i < k; i++)
		{
			put32(big, 52 + 4 * (size_t)i, (uint32_t)end);
			end = put_cblock(end, 0, 0, 0, 0);
		}
		put32(big, 52 + 4 * (size_t)k, FILE_NULL);
		put32(big, 40, (uint32_t)end);
		return end;
	default:	/* k free images naming one BITBLK of 2 x 2 bytes */
		s = 40 + 4 * (size_t)k;
		put16(big, 8, (unsigned)s);
		put16(big, 28, 1);
		put16(big, 16, 40);
		put16(big, 32, (unsigned)k);
		put32(big, s, (uint32_t)(s + 14));
		put16(big, s + 4, 2);
		put16(big, s + 6, 2);
		for (i = 0; i < k; i++)
			put32(big, 40 + 4 * (size_t)i, (uint32_t)s);
		return s + 18;
	}
}

/* The smallest k in lo..hi the budget refuses (hi + 1 for none); k - 1
 * reads. */
static long boundary(long lo, long hi)
{
	char	msg[96];
	size_t	off;
	long	a = lo, b = hi + 1, m;

	while (a < b)
	{
		int	c;

		m = a + (b - a) / 2;
		c = big_code(build_family(m), &off, msg);
		CHECK(c == RSC_OK || c == RSC_ETOOBIG);
		if (c != RSC_OK)
			b = m;
		else
			a = m + 1;
	}
	if (a <= hi)
	{
		CHECK(big_code(build_family(a), &off, msg) == RSC_ETOOBIG);
		CHECK(strncmp(msg, "decoded size exceeds the limit", 30) == 0);
		CHECK(a == lo || big_code(build_family(a - 1), &off, msg) == RSC_OK);
	}
	return a;
}

static void test_charges(void)
{
	/* family: the measured boundary */
	static const long	want[] =
	{
		1160, 209, 2793, 105, 13489, 336, 5091, 13, 698, 320, 19, 7301, 7, 1175, 563
	};
	/* the largest k a family can have (16-bit header offsets, 32767
	 * objects in a tree, the size of big) */
	static const long	most[] =
	{
		32000, 32000, 32000, 32000, 32000, 32000, 32000, 2500, 2500, 2500,
		2500, 15000, 15000, 15000, 9000
	};
	long	k;

	for (family = 0; family < 15; family++)
	{
		k = boundary(1, most[family]);
		if (getenv("RSC_PRINT_BOUNDARY"))
			printf("  family %d: boundary %ld\n", family, k);
		CHECK(k >= want[family] - want[family] / 20 && k <= want[family] + want[family] / 20);
	}
}

/* ---- allocation failures ------------------------------------------------------------- */

/* Every allocation of reading b fails in turn: a clean ENOMEM each time,
 * until the read succeeds.  Returns the number of failures injected. */
static long fail_each(const uint8_t *b, size_t len)
{
	struct rsc_error	err;
	struct rsc	*r;
	long	n;

	for (n = 0;; n++)
	{
		rsc_test_fail_alloc_after(n);
		r = rsc_atari_read(b, len, 8, 16, &err);
		if (r)
			break;
		CHECK(err.code == RSC_ENOMEM);
		if (err.code != RSC_ENOMEM)
			fprintf(stderr, "  allocation %ld: %s\n", n, err.msg);
		CHECK(rsc_test_live_blocks() == 0);
		if (err.code != RSC_ENOMEM)
			break;
	}
	rsc_test_fail_alloc_after(-1);
	rsc_free(r);
	printf("  %ld allocation failures injected\n", n);
	CHECK(rsc_test_live_blocks() == 0);
	return n;
}

static void test_alloc_failures(void)
{
	struct file	f = load("cicon.rsc"), g = load("rcs.rsc");
	uint8_t	b[MINI_LEN + 16], c[2000];
	size_t	len;

	CHECK(fail_each(f.b, f.len) > 5);
	CHECK(fail_each(g.b, g.len) > 500);
	free(g.b);
	build_mini(b);
	CHECK(fail_each(b, MINI_LEN) > 5);
	len = build_cfile(c, sizeof c, 2, 0, 2, 0);
	CHECK(fail_each(c, len) > 10);
	free(f.b);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		golden_dir = argv[1];
	test_rcs("rcs.rsc", 0);
	test_rcs("rcslow.rsc", 1);
	test_cicon();
	test_mini();
	test_cfile();
	test_coords();
	test_hostile();
	test_versions_cap();
	test_charges();
	test_truncation();
	test_mutations();
	test_alloc_failures();
	printf("rsc-test (reader): %ld checks, %ld failed\n", checks, failures);
	return failures ? 1 : 0;
}
