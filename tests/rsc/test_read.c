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

/* A read either succeeds or fails with a proper code and position; nothing
 * leaks.  Returns 1 for success. */
static int read_clean(const uint8_t *b, size_t len)
{
	struct rsc_error	err;
	struct rsc	*r;
	int	ok;

	memset(&err, 0x55, sizeof err);
	r = read_copy(b, len, 8, 16, &err);
	if (r)
	{
		ok = err.code == RSC_OK;
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
	CHECK(rsc_test_live_blocks() == 0);
}

/* A colour icon file: a tree of a root and nobj G_CICON children (object
 * i has index first + i), a table of ncicon entries all naming one block
 * (with the terminator unless noterm), the block: 16 x 2, two versions
 * (2 planes; 1 plane with a selected image).  Returns the length. */
static size_t build_cfile(uint8_t *b, size_t room, int nobj, int first, int ncicon, int noterm)
{
	size_t	objs = 36, tr = objs + 24 * ((size_t)nobj + 1), ext = tr + 4, tab = ext + 12;
	size_t	blk = tab + 4 * ((size_t)ncicon + 1), p, end;
	int	i;

	end = blk + 38 + 8 + 12 + 22 + 12 + 22 + 16;
	if (noterm)
	{
		blk = tab - 0;	/* the block first, the table at the very end */
		end = blk + 38 + 8 + 12 + 22 + 12 + 22 + 16;
		tab = end;
		end += 4 * (size_t)ncicon;
	}
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
			i == nobj ? OF_LASTOB : 0, (uint32_t)(first + i - 1), 0, 0, 4, 2);
	put32(b, tr, (uint32_t)objs);
	put32(b, ext, (uint32_t)end);
	put32(b, ext + 4, (uint32_t)tab);
	for (i = 0; i < ncicon; i++)
		put32(b, tab + 4 * (size_t)i, (uint32_t)blk);
	if (!noterm)
		put32(b, tab + 4 * (size_t)ncicon, FILE_NULL);
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
	return end;
}

static void test_cfile(void)
{
	static uint8_t	b[20000];
	struct rsc_error	err;
	struct rsc	*r;
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	size_t	len, off;
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
	off = 36 + 48 + 4 + 12 + 8;	/* the block */
	put16(b, off + 38 + 20, 17);
	CHECK(read_code(b, len, &off) == RSC_ECICON);
	len = build_cfile(b, sizeof b, 1, 0, 1, 0);
	off = 36 + 48 + 4 + 12 + 8;
	put32(b, off + 34, 3);
	CHECK(read_code(b, len, &off) == RSC_ECICON);

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
	CHECK(rsc_test_live_blocks() == 0);
	return n;
}

static void test_alloc_failures(void)
{
	struct file	f = load("cicon.rsc");
	uint8_t	b[MINI_LEN + 16], c[2000];
	size_t	len;

	CHECK(fail_each(f.b, f.len) > 5);
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
	test_truncation();
	test_mutations();
	test_alloc_failures();
	printf("rsc-test (reader): %ld checks, %ld failed\n", checks, failures);
	return failures ? 1 : 0;
}
