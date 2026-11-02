/* TEST_EDIT.C	host tests of the editing calls of the resource library
 *		(src/rsc): move, copy, walk, colour versions by index, resize
 *		of a colour icon, rsc_validate, and the names that follow
 *		edits (rscnames.h), with a fuzz against a naive model;
 *		make rsc-test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rscmodel.h"
#include "rscfile.h"
#include "rscnames.h"

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

static long live(void)
{
	return rsc_test_live_blocks();
}

/* ---- a deep fingerprint of a model ------------------------------------------ */

static void hb(uint64_t *h, const void *p, size_t n)
{
	const unsigned char	*b = p;

	while (n--)
	{
		*h ^= *b++;
		*h *= 1099511628211ULL;
	}
}

static void hi(uint64_t *h, long v)
{
	hb(h, &v, sizeof v);
}

static void hs(uint64_t *h, const char *s)
{
	hi(h, s != NULL);
	if (s)
		hb(h, s, strlen(s) + 1);
}

static void hmem(uint64_t *h, const void *p, size_t n)
{
	hi(h, p != NULL);
	if (p)
		hb(h, p, n);
}

/* The content of the spec of o and its scalar fields, not its links nor
 * OF_LASTOB; the opaque file fields of colour icons only with file. */
static void hash_obj(uint64_t *h, const OBJECT *o, int file)
{
	const struct rsc_cicon	*c;
	const struct rsc_cicon_version	*v;
	const TEDINFO	*te;
	const ICONBLK	*ib;
	const BITBLK	*bb;
	size_t	pb;
	int	k;

	hi(h, o->ob_type);
	hi(h, o->ob_flags & ~OF_LASTOB);
	hi(h, o->ob_state);
	hi(h, o->ob_x);
	hi(h, o->ob_y);
	hi(h, o->ob_width);
	hi(h, o->ob_height);
	switch (rsc_obj_spec_kind(o))
	{
	case RSC_SPEC_VALUE:
		hi(h, (long)o->ob_spec.index);
		break;
	case RSC_SPEC_STRING:
		hs(h, o->ob_spec.free_string);
		break;
	case RSC_SPEC_TEDINFO:
		te = o->ob_spec.tedinfo;
		hi(h, te != NULL);
		if (!te)
			break;
		hs(h, te->te_ptext);
		hs(h, te->te_ptmplt);
		hs(h, te->te_pvalid);
		hi(h, te->te_font);
		hi(h, te->te_fontid);
		hi(h, te->te_just);
		hi(h, te->te_color);
		hi(h, te->te_fontsize);
		hi(h, te->te_thickness);
		hi(h, te->te_txtlen);
		hi(h, te->te_tmplen);
		break;
	case RSC_SPEC_ICONBLK:
		ib = o->ob_spec.iconblk;
		hi(h, ib != NULL);
		if (!ib)
			break;
		pb = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);
		hmem(h, ib->ib_pmask, pb);
		hmem(h, ib->ib_pdata, pb);
		hs(h, ib->ib_ptext);
		hb(h, &ib->ib_char, 11 * sizeof(short));
		break;
	case RSC_SPEC_BITBLK:
		bb = o->ob_spec.bitblk;
		hi(h, bb != NULL);
		if (!bb)
			break;
		hmem(h, bb->bi_pdata, rsc_bitblk_bytes(bb));
		hb(h, &bb->bi_wb, 5 * sizeof(short));
		break;
	case RSC_SPEC_CICON:
		c = (const struct rsc_cicon *)o->ob_spec.ciconblk;
		hi(h, c != NULL);
		if (!c)
			break;
		ib = &c->blk.monoblk;
		hi(h, ib->ib_ptext == c->text);
		hmem(h, ib->ib_pmask, c->mono_bytes);
		hmem(h, ib->ib_pdata, c->mono_bytes);
		hb(h, &ib->ib_char, 11 * sizeof(short));
		hb(h, c->text, sizeof c->text);
		hi(h, (long)c->mono_bytes);
		hi(h, c->nver);
		if (file)
			hb(h, c->file_head, sizeof c->file_head);
		k = 0;
		for (v = (const struct rsc_cicon_version *)c->blk.mainlist; v && k < 100;
		     v = (const struct rsc_cicon_version *)v->c.next_res, k++)
		{
			hi(h, v->c.num_planes);
			hi(h, (long)v->data_bytes);
			hmem(h, v->c.col_data, v->data_bytes);
			hmem(h, v->c.col_mask, c->mono_bytes);
			hmem(h, v->c.sel_data, v->data_bytes);
			hmem(h, v->c.sel_mask, c->mono_bytes);
			if (file)
				hb(h, v->file_ptr, sizeof v->file_ptr);
		}
		hi(h, c->last == (c->nver ? rsc_cicon_version(c, c->nver - 1) : NULL));
		break;
	}
}

static uint64_t hash_rsc(const struct rsc *r)
{
	uint64_t	h = 14695981039346656037ULL;
	const OBJECT	*o;
	int	t, i, n, cw, ch;

	rsc_cell(r, &cw, &ch);
	hi(&h, cw);
	hi(&h, ch);
	hi(&h, rsc_vrsn(r));
	hi(&h, rsc_ntrees(r));
	for (t = 0; t < rsc_ntrees(r); t++)
	{
		o = rsc_tree_objects(r, t, &n);
		hi(&h, n);
		for (i = 0; i < n; i++)
		{
			hi(&h, o[i].ob_next);
			hi(&h, o[i].ob_head);
			hi(&h, o[i].ob_tail);
			hi(&h, o[i].ob_flags & OF_LASTOB);
			hash_obj(&h, &o[i], 1);
		}
	}
	hi(&h, rsc_nfrstr(r));
	for (i = 0; i < rsc_nfrstr(r); i++)
		hs(&h, rsc_frstr(r, i));
	hi(&h, rsc_nfrimg(r));
	for (i = 0; i < rsc_nfrimg(r); i++)
	{
		const BITBLK	*bb = rsc_frimg(r, i);

		hmem(&h, bb->bi_pdata, rsc_bitblk_bytes(bb));
		hb(&h, &bb->bi_wb, 5 * sizeof(short));
	}
	return h;
}

/* The same for the content of a tree in preorder (depth and object), so
 * that two trees with the same shape and objects have the same value
 * whatever their array order; file: as for hash_obj. */
struct dumpctx
{
	uint64_t	h;
	const OBJECT	*o;
	int	file;
};

static int dump_cb(void *ctx, int obj, int depth, int parent)
{
	struct dumpctx	*d = ctx;

	(void)parent;
	hi(&d->h, depth);
	hash_obj(&d->h, &d->o[obj], d->file);
	return RSC_WALK_CONTINUE;
}

static uint64_t hash_shape(const struct rsc *r, int file)
{
	struct dumpctx	d;
	int	t, n;

	d.h = 14695981039346656037ULL;
	d.file = file;
	hi(&d.h, rsc_ntrees(r));
	for (t = 0; t < rsc_ntrees(r); t++)
	{
		d.o = rsc_tree_objects(r, t, &n);
		hi(&d.h, rsc_walk(r, t, dump_cb, &d));
	}
	hi(&d.h, rsc_nfrstr(r));
	for (t = 0; t < rsc_nfrstr(r); t++)
		hs(&d.h, rsc_frstr(r, t));
	hi(&d.h, rsc_nfrimg(r));
	for (t = 0; t < rsc_nfrimg(r); t++)
	{
		const BITBLK	*bb = rsc_frimg(r, t);

		hmem(&d.h, bb->bi_pdata, rsc_bitblk_bytes(bb));
		hb(&d.h, &bb->bi_wb, 5 * sizeof(short));
	}
	return d.h;
}

/* ---- objects made from a seed ----------------------------------------------------- */

static const unsigned	types[] = { G_BOX, G_IBOX, G_STRING, G_BUTTON, G_TEXT, G_FTEXT, G_ICON,
				    G_IMAGE, G_CICON, G_TITLE };
#define NTYPES	((int)(sizeof types / sizeof types[0]))
#define NSEEDS	64

static void words(short *w, int n, int seed)
{
	int	i;

	for (i = 0; i < n; i++)
		w[i] = (short)(seed * 257 + i * 3 + 1);
}

/* Sets flags, state, geometry and the spec of the existing object i of
 * type type from seed; returns the number of calls that failed. */
static int fill_spec(struct rsc *r, int t, int i, unsigned type, int seed)
{
	TEDINFO	te;
	ICONBLK	ib;
	BITBLK	bb;
	struct rsc_cicon	*c;
	short	d[8], m[8], col[8 * 4], sel[8 * 4];
	char	s[32];
	int	bad = 0, v, p;

	memset(&te, 0, sizeof te);
	memset(&ib, 0, sizeof ib);
	memset(&bb, 0, sizeof bb);
	bad += rsc_obj_set_flags(r, t, i, (unsigned)(seed * 5) & 0x5f) != 0;
	bad += rsc_obj_set_state(r, t, i, (unsigned)seed & 0x3f) != 0;
	bad += rsc_obj_set_geom(r, t, i, seed, (int)type, seed + 1, 2) != 0;
	snprintf(s, sizeof s, "s%d", seed);
	switch (type)
	{
	case G_STRING:
	case G_BUTTON:
	case G_TITLE:
		bad += rsc_obj_set_str(r, t, i, RSC_STR_SPEC, s) != 0;
		break;
	case G_TEXT:
	case G_FTEXT:
		te.te_ptext = s;
		te.te_ptmplt = "tmpl";
		te.te_pvalid = seed & 1 ? "XX" : NULL;
		te.te_color = (short)seed;
		te.te_just = (short)(seed & 3);
		bad += rsc_obj_set_tedinfo(r, t, i, &te) != 0;
		break;
	case G_ICON:
		words(d, 8, seed);
		words(m, 8, seed + 1);
		ib.ib_pmask = m;
		ib.ib_pdata = d;
		ib.ib_ptext = s;
		ib.ib_wicon = 16;
		ib.ib_hicon = 8;
		ib.ib_char = (short)seed;
		bad += rsc_obj_set_iconblk(r, t, i, &ib) != 0;
		break;
	case G_IMAGE:
		words(d, 4, seed);
		bb.bi_pdata = d;
		bb.bi_wb = 2;
		bb.bi_hl = 4;
		bb.bi_color = (short)seed;
		bad += rsc_obj_set_bitblk(r, t, i, &bb) != 0;
		break;
	case G_CICON:
		words(d, 8, seed);
		words(m, 8, seed + 1);
		ib.ib_pmask = m;
		ib.ib_pdata = d;
		ib.ib_wicon = 16;
		ib.ib_hicon = 8;
		ib.ib_char = (short)seed;
		bad += rsc_obj_set_cicon(r, t, i, &ib, s) != 0;
		c = rsc_obj_cicon(r, t, i);
		for (v = 0; c && v < 1 + seed % 3; v++)
		{
			p = 1 + (seed + v) % 4;
			words(col, 8 * p, seed + v);
			words(sel, 8 * p, seed + v + 9);
			bad += rsc_cicon_add_version(c, p, col, m, v & 1 ? sel : NULL, v & 1 ? m : NULL) != v;
		}
		break;
	default:
		bad += rsc_obj_set_value(r, t, i, (uint32_t)seed * 7 + 1) != 0;
		break;
	}
	return bad;
}

/* The fingerprint an object made by fill_spec has: made once. */
static uint64_t expect_hash(int ti, int seed)
{
	static uint64_t	cache[NTYPES][NSEEDS];
	static char	have[NTYPES][NSEEDS];
	struct rsc	*r;

	if (!have[ti][seed])
	{
		r = rsc_new();
		rsc_tree_add(r, 1);
		rsc_obj_add(r, 0, 0, types[ti]);
		fill_spec(r, 0, 1, types[ti], seed);
		cache[ti][seed] = 14695981039346656037ULL;
		hash_obj(&cache[ti][seed], &rsc_tree_objects(r, 0, NULL)[1], 1);
		have[ti][seed] = 1;
		rsc_free(r);
	}
	return cache[ti][seed];
}

/* Adds an object of type index ti and seed under parent; its index, or -1. */
static int add_obj(struct rsc *r, int t, int parent, int ti, int seed)
{
	int	i = rsc_obj_add(r, t, parent, types[ti]);

	if (i < 0)
		return -1;
	return fill_spec(r, t, i, types[ti], seed) ? -1 : i;
}

/* The shape of the subtree of tree t at start as "0(1 2(4 5) 3)". */
struct shapectx
{
	char	buf[1024];
	size_t	len;
	int	depth;
};

static int shape_cb(void *ctx, int obj, int depth, int parent)
{
	struct shapectx	*s = ctx;
	int	k;

	(void)parent;
	if (s->len > sizeof s->buf - 40)
		return RSC_WALK_STOP;
	if (s->len)
	{
		if (depth > s->depth)
			s->buf[s->len++] = '(';
		else
		{
			for (k = depth; k < s->depth; k++)
				s->buf[s->len++] = ')';
			s->buf[s->len++] = ' ';
		}
	}
	s->len += (size_t)snprintf(s->buf + s->len, sizeof s->buf - s->len, "%d", obj);
	s->depth = depth;
	return RSC_WALK_CONTINUE;
}

static const char *shape_from(const struct rsc *r, int t, int start)
{
	static char	out[1024];
	struct shapectx	s;
	int	k, rc;

	s.len = 0;
	s.depth = 0;
	s.buf[0] = '\0';
	rc = rsc_walk_from(r, t, start, shape_cb, &s);
	if (rc < 0)
	{
		snprintf(out, sizeof out, "E%d", rc);
		return out;
	}
	for (k = 0; k < s.depth; k++)
		s.buf[s.len++] = ')';
	s.buf[s.len] = '\0';
	memcpy(out, s.buf, s.len + 1);
	return out;
}

static const char *shape(const struct rsc *r, int t)
{
	return shape_from(r, t, 0);
}

/* The links of tree t are an AES tree, the root's ob_next is -1 and OF_LASTOB is
 * on the last array element only, and rsc_validate agrees. */
static int tree_ok(struct rsc *r, int t)
{
	OBJECT	*o;
	int	n, i;

	o = rsc_tree_objects(r, t, &n);
	if (!o || n < 1 || o[0].ob_next != -1)
		return 0;
	for (i = 0; i < n; i++)
		if (!(o[i].ob_flags & OF_LASTOB) != (i != n - 1))
			return 0;
	return rsc_tree_preorder(o, n, NULL) > 0;
}

static int model_ok(struct rsc *r)
{
	struct rsc_error	e;
	int	t;

	for (t = 0; t < rsc_ntrees(r); t++)
		if (!tree_ok(r, t))
			return 0;
	return rsc_validate(r, &e) == 0;
}

/* A dialog: 0 root; 1, 2, 3 its children; 4, 5 the children of 2:
 *   0(1 2(4 5) 3)
 * the objects of different types and seeds (seed = index + 1). */
static int build_sample(struct rsc *r)
{
	int	t = rsc_tree_add(r, 1);
	int	ok = t >= 0;

	ok = ok && rsc_obj_set_type(r, t, 0, G_BOX) == 0 && fill_spec(r, t, 0, G_BOX, 1) == 0;
	ok = ok && add_obj(r, t, 0, 2, 2) == 1;	/* G_STRING */
	ok = ok && add_obj(r, t, 0, 1, 3) == 2;	/* G_IBOX */
	ok = ok && add_obj(r, t, 0, 4, 4) == 3;	/* G_TEXT */
	ok = ok && add_obj(r, t, 2, 6, 5) == 4;	/* G_ICON */
	ok = ok && add_obj(r, t, 2, 8, 8) == 5;	/* G_CICON with three versions */
	return ok ? t : -1;
}

/* build_sample plus a second tree, three free strings and two free images */
static int build_more(struct rsc *r)
{
	BITBLK	bb;
	short	d[4];
	int	t, k;

	if (build_sample(r) != 0)
		return -1;
	t = rsc_tree_add(r, 1);
	if (t != 1 || fill_spec(r, 1, 0, G_IBOX, 7) || add_obj(r, 1, 0, 3, 8) != 1)
		return -1;
	for (k = 0; k < 3; k++)
	{
		char	s[16];

		snprintf(s, sizeof s, "str%d", k);
		if (rsc_frstr_add(r, s) != k)
			return -1;
	}
	memset(&bb, 0, sizeof bb);
	bb.bi_wb = 2;
	bb.bi_hl = 2;
	for (k = 0; k < 2; k++)
	{
		words(d, 2, k + 20);
		bb.bi_pdata = d;
		if (rsc_frimg_add(r, &bb) != k)
			return -1;
	}
	return 0;
}

/* Runs op on fresh models from build for allocation failures n = 0, 1, ...
 * until it succeeds; each failure must leave the model as it was (same
 * fingerprint, valid) and the live block count; returns the number of
 * failures, -1 on a broken promise or a run that never succeeds. */
static int fail_sweep(int (*build)(struct rsc *), int (*op)(struct rsc *))
{
	struct rsc	*r;
	uint64_t	before;
	long	lv, n;
	int	fails = 0, rc = -1, bad = 0;

	for (n = 0; n < 200; n++)
	{
		r = rsc_new();
		if (!r || build(r) < 0)
			return -1;
		before = hash_rsc(r);
		lv = live();
		rsc_test_fail_alloc_after(n);
		rc = op(r);
		rsc_test_fail_alloc_after(-1);
		if (rc == 0)
		{
			bad |= !model_ok(r);
			rsc_free(r);
			break;
		}
		if (hash_rsc(r) != before || live() != lv || !model_ok(r))
			bad = 1;
		fails++;
		rsc_free(r);
	}
	CHECK(live() == 0);
	return bad || rc != 0 ? -1 : fails;
}

static int build_sample_only(struct rsc *r)
{
	return build_sample(r);
}

/* ---- rsc_obj_parent, rsc_obj_move ---------------------------------------------------- */

static int op_move_a(struct rsc *r)
{
	return rsc_obj_move(r, 0, 5, 0, 0);
}

static void test_move(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	uint64_t	before;
	int	n, i, tag[6];

	CHECK(build_sample(r) == 0);
	CHECK_STR(shape(r, 0), "0(1 2(4 5) 3)");
	CHECK(rsc_obj_parent(r, 0, 0) == -1);
	CHECK(rsc_obj_parent(r, 0, 1) == 0 && rsc_obj_parent(r, 0, 3) == 0);
	CHECK(rsc_obj_parent(r, 0, 4) == 2 && rsc_obj_parent(r, 0, 5) == 2);
	CHECK(rsc_obj_parent(r, 0, 6) == -2 && rsc_obj_parent(r, 0, -1) == -2);
	CHECK(rsc_obj_parent(r, 1, 0) == -2);
	o = rsc_tree_objects(r, 0, &n);
	for (i = 0; i < 6; i++)
		tag[i] = o[i].ob_x;

	/* to the front, to the end, into a parent, between siblings */
	CHECK(rsc_obj_move(r, 0, 3, 0, 0) == 0);
	CHECK_STR(shape(r, 0), "0(3 1 2(4 5))");
	CHECK(model_ok(r));
	CHECK(rsc_obj_move(r, 0, 3, 0, -1) == 0);
	CHECK_STR(shape(r, 0), "0(1 2(4 5) 3)");
	CHECK(rsc_obj_move(r, 0, 1, 2, 1) == 0);
	CHECK_STR(shape(r, 0), "0(2(4 1 5) 3)");
	CHECK(rsc_obj_move(r, 0, 5, 0, 1) == 0);
	CHECK_STR(shape(r, 0), "0(2(4 1) 5 3)");
	CHECK(rsc_obj_parent(r, 0, 5) == 0 && rsc_obj_parent(r, 0, 1) == 2);
	/* the last child into the empty parent 1, and out again */
	CHECK(rsc_obj_move(r, 0, 3, 1, 0) == 0);
	CHECK_STR(shape(r, 0), "0(2(4 1(3)) 5)");
	CHECK(rsc_obj_move(r, 0, 3, 0, 5 - 4) == 0);
	CHECK_STR(shape(r, 0), "0(2(4 1) 3 5)");
	/* pos counts the siblings without the object: moving within one parent */
	CHECK(rsc_obj_move(r, 0, 2, 0, 2) == 0);
	CHECK_STR(shape(r, 0), "0(3 5 2(4 1))");
	CHECK(rsc_obj_move(r, 0, 5, 0, 2) == 0);
	CHECK_STR(shape(r, 0), "0(3 2(4 1) 5)");
	CHECK(rsc_obj_move(r, 0, 5, 0, 2) == 0);	/* where it is: nothing changes */
	CHECK_STR(shape(r, 0), "0(3 2(4 1) 5)");
	CHECK(rsc_obj_move(r, 0, 4, 2, -1) == 0);
	CHECK_STR(shape(r, 0), "0(3 2(1 4) 5)");
	/* indices, tags and the array stay: only links changed */
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 6);
	for (i = 0; i < 6; i++)
		CHECK(o[i].ob_x == tag[i]);
	CHECK(tree_ok(r, 0));
	CHECK(rsc_obj_move(r, 0, 2, 0, 0) == 0 && rsc_obj_move(r, 0, 3, 0, 1) == 0);
	CHECK_STR(shape(r, 0), "0(2(1 4) 3 5)");

	/* errors leave everything as it was */
	before = hash_rsc(r);
	CHECK(rsc_obj_move(r, 0, 0, 2, 0) == -1);		/* the root */
	CHECK(rsc_obj_move(r, 0, 2, 2, 0) == -1);		/* under itself */
	CHECK(rsc_obj_move(r, 0, 2, 1, 0) == -1);		/* below itself */
	CHECK(rsc_obj_move(r, 0, 2, 4, -1) == -1);
	CHECK(rsc_obj_move(r, 0, 1, 0, 4) == -1);		/* 3 children of 0 (1 is not one): last is 3 */
	CHECK(rsc_obj_move(r, 0, 1, 0, 5) == -1);
	CHECK(rsc_obj_move(r, 0, 1, 0, -2) == -1);
	CHECK(rsc_obj_move(r, 0, 1, 6, 0) == -1);
	CHECK(rsc_obj_move(r, 0, 1, -1, 0) == -1);
	CHECK(rsc_obj_move(r, 0, 6, 0, 0) == -1);
	CHECK(rsc_obj_move(r, 0, -1, 0, 0) == -1);
	CHECK(rsc_obj_move(r, 1, 1, 0, 0) == -1);
	CHECK(rsc_obj_move(r, -1, 1, 0, 0) == -1);
	CHECK(hash_rsc(r) == before);
	/* an object that is not reachable (deleted) neither moves nor takes children */
	CHECK(rsc_obj_delete(r, 0, 3) == 0);
	before = hash_rsc(r);
	CHECK(rsc_obj_move(r, 0, 3, 0, 0) == -1);
	CHECK(rsc_obj_move(r, 0, 1, 3, 0) == -1);
	CHECK(hash_rsc(r) == before);
	/* broken links */
	o = rsc_tree_objects(r, 0, &n);
	o[2].ob_tail = 99;
	before = hash_rsc(r);
	CHECK(rsc_obj_move(r, 0, 5, 0, 0) == -1);
	CHECK(hash_rsc(r) == before);
	rsc_free(r);
	CHECK(live() == 0);

	CHECK(fail_sweep(build_sample_only, op_move_a) == 1);	/* the scratch */
}

/* the cycle check on a long chain (iterative), and a moved subtree keeps its indices */
static void test_move_deep(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	int	n = 20000, k, i;

	CHECK(rsc_tree_add(r, n) == 0);
	o = rsc_tree_objects(r, 0, NULL);
	/* 0 -> 1 -> 2 -> ... a chain; 0 also has the child n - 1 ... keep it a chain */
	for (k = 0; k < n; k++)
	{
		o[k].ob_head = o[k].ob_tail = (short)(k + 1 < n ? k + 1 : -1);
		o[k].ob_next = (short)(k ? k - 1 : -1);
	}
	CHECK(rsc_walk(r, 0, dump_cb, &(struct dumpctx){ 0, o, 0 }) == n);
	CHECK(rsc_obj_move(r, 0, 1, n - 1, 0) == -1);	/* 1 under its last descendant */
	CHECK(rsc_obj_move(r, 0, n - 1, 0, 0) == 0);
	CHECK(rsc_obj_parent(r, 0, n - 1) == 0 && rsc_obj_parent(r, 0, n - 2) == n - 3);
	CHECK(rsc_walk(r, 0, dump_cb, &(struct dumpctx){ 0, o, 0 }) == n);
	for (i = 1; i < 5; i++)
		CHECK(rsc_obj_parent(r, 0, i) == (i == 1 ? 0 : i - 1));
	rsc_free(r);
}

/* ---- copying ------------------------------------------------------------------------- */

static int op_copy_sub(struct rsc *r)
{
	int	map[6];

	return rsc_obj_copy_subtree(r, 0, 3, 0, r, 0, 2, map) < 0 ? -1 : 0;
}

static int op_copy_tree(struct rsc *r)
{
	return rsc_tree_copy(r, 0, r, 0, NULL) < 0 ? -1 : 0;
}

static int op_copy_cicon(struct rsc *r)
{
	return rsc_obj_copy_subtree(r, 0, 0, -1, r, 0, 5, NULL) < 0 ? -1 : 0;
}

static void test_copy(void)
{
	struct rsc	*a = rsc_new(), *b = rsc_new();
	struct rsc_cicon	*c1, *c2;
	OBJECT	*o, *o2;
	uint64_t	before, h5;
	int	map[16], map2[16], n, i, root, v;

	CHECK(build_more(a) == 0 && build_sample(b) == 0);
	/* a subtree inside its own tree: 2(4 5) as the last child of 0 */
	for (i = 0; i < 16; i++)
		map[i] = 99;
	root = rsc_obj_copy_subtree(a, 0, 0, -1, a, 0, 2, map);
	CHECK(root == 6);
	CHECK_STR(shape(a, 0), "0(1 2(4 5) 3 6(7 8))");
	CHECK(map[0] == -1 && map[1] == -1 && map[2] == 6 && map[3] == -1 && map[4] == 7
	      && map[5] == 8 && map[6] == 99);
	o = rsc_tree_objects(a, 0, &n);
	CHECK(n == 9 && model_ok(a));
	{
		static const int	pair[3][2] = { { 2, 6 }, { 4, 7 }, { 5, 8 } };

		for (i = 0; i < 3; i++)
		{
			uint64_t	x = 14695981039346656037ULL, y = 14695981039346656037ULL;

			hash_obj(&x, &o[pair[i][0]], 1);
			hash_obj(&y, &o[pair[i][1]], 1);
			CHECK(x == y);
		}
		h5 = 14695981039346656037ULL;
		hash_obj(&h5, &o[5], 1);
	}
	CHECK(o[4].ob_spec.iconblk != o[7].ob_spec.iconblk
	      && o[4].ob_spec.iconblk->ib_pdata != o[7].ob_spec.iconblk->ib_pdata
	      && o[4].ob_spec.iconblk->ib_ptext != o[7].ob_spec.iconblk->ib_ptext);
	c1 = rsc_obj_cicon(a, 0, 5);
	c2 = rsc_obj_cicon(a, 0, 8);
	CHECK(c1 && c2 && c1 != c2 && c1->blk.monoblk.ib_ptext == c1->text
	      && c2->blk.monoblk.ib_ptext == c2->text);
	CHECK(rsc_cicon_nversions(c2) == rsc_cicon_nversions(c1) && rsc_cicon_nversions(c2) > 1);
	for (v = 0; v < rsc_cicon_nversions(c1); v++)
		CHECK(rsc_cicon_version(c1, v) != rsc_cicon_version(c2, v)
		      && rsc_cicon_version(c1, v)->c.col_data != rsc_cicon_version(c2, v)->c.col_data);
	CHECK(o[0].ob_flags & OF_LASTOB ? 0 : 1);
	CHECK(o[8].ob_flags & OF_LASTOB);

	/* as the first child, in the middle; into a leaf; a single object */
	root = rsc_obj_copy_subtree(a, 0, 0, 0, a, 0, 3, NULL);
	CHECK(root == 9);
	CHECK_STR(shape(a, 0), "0(9 1 2(4 5) 3 6(7 8))");
	root = rsc_obj_copy_subtree(a, 0, 2, 1, a, 0, 1, NULL);
	CHECK(root == 10);
	CHECK_STR(shape(a, 0), "0(9 1 2(4 10 5) 3 6(7 8))");
	root = rsc_obj_copy_subtree(a, 0, 1, 0, a, 0, 3, NULL);
	CHECK(root == 11 && rsc_obj_parent(a, 0, 11) == 1);
	/* the copy goes into the subtree it is a copy of (a snapshot is copied) */
	root = rsc_obj_copy_subtree(a, 0, 2, -1, a, 0, 2, map);
	CHECK(root == 12);
	CHECK_STR(shape_from(a, 0, 2), "2(4 10 5 12(13 15 14))");
	CHECK(map[2] == 12 && map[4] == 13 && map[5] == 14 && map[10] == 15 && map[3] == -1);
	/* the whole tree 0 of b as an object of tree 1 of a, between models */
	root = rsc_obj_copy_subtree(a, 1, 0, -1, b, 0, 0, map2);
	CHECK(root == 2 && map2[0] == 2 && map2[5] == 7);
	CHECK_STR(shape(a, 1), "0(1 2(3 4(6 7) 5))");
	o = rsc_tree_objects(a, 1, &n);
	o2 = rsc_tree_objects(b, 0, NULL);
	CHECK(n == 8 && model_ok(a) && model_ok(b));
	for (i = 0; i < 6; i++)
	{
		uint64_t	x = 14695981039346656037ULL, y = 14695981039346656037ULL;

		hash_obj(&x, &o2[i], 1);
		hash_obj(&y, &o[map2[i]], 1);
		CHECK(x == y);
	}
	/* free the source: the copy holds nothing of it */
	rsc_free(b);
	b = NULL;
	CHECK(rsc_obj_cicon(a, 1, 7)->blk.monoblk.ib_pdata[0] == (short)(8 * 257 + 1));
	CHECK(rsc_cicon_version(rsc_obj_cicon(a, 1, 7), 0)->c.col_data[0] != 0);

	/* the file_head and file_ptr of a colour icon are copied as they are */
	c1 = rsc_obj_cicon(a, 0, 5);
	c1->file_head[0] = 11;
	c1->file_head[2] = 33;
	rsc_cicon_version(c1, 0)->file_ptr[1] = 77;
	rsc_cicon_version(c1, 1)->file_ptr[4] = 99;
	root = rsc_obj_copy_subtree(a, 0, 0, -1, a, 0, 5, NULL);
	c2 = rsc_obj_cicon(a, 0, root);
	CHECK(c2->file_head[0] == 11 && c2->file_head[2] == 33);
	CHECK(rsc_cicon_version(c2, 0)->file_ptr[1] == 77 && rsc_cicon_version(c2, 1)->file_ptr[4] == 99);
	CHECK(c2->mono_bytes == c1->mono_bytes && c2->last == rsc_cicon_version(c2, rsc_cicon_nversions(c2) - 1));
	/* a TEDINFO's lengths are copied as they are (the copy keeps what the file had) */
	o = rsc_tree_objects(a, 0, NULL);
	o[3].ob_spec.tedinfo->te_txtlen = 40;
	o[3].ob_spec.tedinfo->te_tmplen = 3;
	root = rsc_obj_copy_subtree(a, 0, 0, -1, a, 0, 3, NULL);
	o = rsc_tree_objects(a, 0, NULL);
	CHECK(o[root].ob_spec.tedinfo != o[3].ob_spec.tedinfo && o[root].ob_spec.tedinfo->te_txtlen == 40
	      && o[root].ob_spec.tedinfo->te_tmplen == 3);

	/* errors change nothing, also not the map */
	before = hash_rsc(a);
	for (i = 0; i < 16; i++)
		map[i] = 123;
	CHECK(rsc_obj_copy_subtree(a, 0, 0, -2, a, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 99, a, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, -1, 0, a, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 999, 0, a, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, 0, -1, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, 0, 999, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 5, 0, 0, a, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, 5, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, -1, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, NULL, 0, 2, map) == -1);
	CHECK(rsc_obj_copy_subtree(NULL, 0, 0, 0, a, 0, 2, map) == -1);
	CHECK(hash_rsc(a) == before && map[0] == 123);
	/* an unreachable destination parent or source */
	CHECK(rsc_obj_delete(a, 0, 3) == 0);
	before = hash_rsc(a);
	CHECK(rsc_obj_copy_subtree(a, 0, 3, 0, a, 0, 2, NULL) == -1);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, 0, 3, NULL) == -1);
	CHECK(hash_rsc(a) == before);
	/* another cell size */
	{
		struct rsc	*c = rsc_new();

		CHECK(build_sample(c) == 0 && rsc_set_cell(c, 8, 16) == 0);
		CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, c, 0, 2, NULL) == -1);
		CHECK(rsc_tree_copy(a, -1, c, 0, NULL) == -1);
		CHECK(rsc_obj_copy_subtree(c, 0, 0, 0, a, 0, 2, NULL) == -1);
		CHECK(rsc_set_cell(a, 8, 16) == 0);
		CHECK(rsc_obj_copy_subtree(a, 0, 0, -1, c, 0, 2, NULL) >= 0);
		CHECK(rsc_tree_copy(a, -1, c, 0, NULL) >= 0);
		rsc_free(c);
	}
	/* broken links of the source subtree */
	rsc_free(a);
	a = rsc_new();
	CHECK(build_sample(a) == 0);
	o = rsc_tree_objects(a, 0, NULL);
	o[4].ob_next = 40;
	before = hash_rsc(a);
	CHECK(rsc_obj_copy_subtree(a, 0, 0, 0, a, 0, 2, NULL) == -1);
	CHECK(rsc_tree_copy(a, -1, a, 0, NULL) == -1);
	CHECK(hash_rsc(a) == before);
	b = rsc_new();
	CHECK(build_sample(b) == 0);
	CHECK(rsc_obj_copy_subtree(b, 0, 0, 0, a, 0, 1, NULL) >= 0);	/* 1 is sound: not looked beyond */
	CHECK(rsc_obj_copy_subtree(b, 0, 0, 0, a, 0, 2, NULL) == -1);
	rsc_free(b);
	rsc_free(a);
	CHECK(live() == 0);

	CHECK(fail_sweep(build_sample_only, op_copy_sub) > 3);
	CHECK(fail_sweep(build_sample_only, op_copy_cicon) > 3);
	CHECK(fail_sweep(build_sample_only, op_copy_tree) > 3);
}

/* a tree of n objects: the root with n - 1 leaves */
static void fan(struct rsc *r, int n)
{
	OBJECT	*o;
	int	k;

	CHECK(rsc_tree_add(r, n) == rsc_ntrees(r) - 1);
	o = rsc_tree_objects(r, rsc_ntrees(r) - 1, NULL);
	o[0].ob_head = 1;
	o[0].ob_tail = (short)(n - 1);
	for (k = 1; k < n; k++)
		o[k].ob_next = (short)(k < n - 1 ? k + 1 : 0);
}

static void test_copy_limits(void)
{
	struct rsc	*r = rsc_new(), *s1 = rsc_new(), *s2 = rsc_new();
	OBJECT	*o;
	uint64_t	before;
	int	n;

	fan(r, 20000);
	fan(s1, 12767);
	fan(s2, 12766);
	CHECK(rsc_obj_copy_subtree(r, 0, 0, -1, r, 0, 0, NULL) == -1);	/* 20000 + 20000 */
	before = hash_rsc(r);
	CHECK(rsc_tree_copy(r, -1, r, 0, NULL) == 1);			/* a tree has its own limit */
	CHECK(rsc_obj_move(r, 1, 7, 0, 0) == 0);
	o = rsc_tree_objects(r, 1, &n);
	CHECK(n == 20000 && o[0].ob_head == 7);
	CHECK(rsc_tree_delete(r, 1) == 0);
	CHECK(hash_rsc(r) == before);
	CHECK(rsc_obj_copy_subtree(r, 0, 0, -1, r, 0, 1, NULL) == 20000);	/* 20001 objects */
	before = hash_rsc(r);
	/* RSC_MAX_OBJECTS is 32767: 12766 more fit, 12767 do not */
	CHECK(rsc_obj_copy_subtree(r, 0, 0, -1, s1, 0, 0, NULL) == -1);
	CHECK(hash_rsc(r) == before);
	CHECK(rsc_obj_copy_subtree(r, 0, 0, -1, s2, 0, 0, NULL) == 20001);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == RSC_MAX_OBJECTS && model_ok(r));
	CHECK(rsc_obj_copy_subtree(r, 0, 0, -1, s2, 0, 1, NULL) == -1);
	rsc_free(r);
	rsc_free(s1);
	rsc_free(s2);
	CHECK(live() == 0);
}

static void test_tree_copy(void)
{
	struct rsc	*a = rsc_new(), *b = rsc_new();
	OBJECT	*o;
	uint64_t	before;
	int	map[16], n, i, t;

	CHECK(build_more(a) == 0 && build_sample(b) == 0);
	/* dead slots are dropped, the order of the array kept: 6 objects, 2 and 3 deleted */
	CHECK(rsc_obj_delete(b, 0, 2) == 0);
	CHECK(rsc_obj_move(b, 0, 3, 0, 0) == 0);
	CHECK_STR(shape(b, 0), "0(3 1)");
	for (i = 0; i < 16; i++)
		map[i] = 99;
	t = rsc_tree_copy(a, 1, b, 0, map);
	CHECK(t == 1 && rsc_ntrees(a) == 3);
	CHECK(map[0] == 0 && map[1] == 1 && map[2] == -1 && map[3] == 2 && map[4] == -1
	      && map[5] == -1 && map[6] == 99);
	o = rsc_tree_objects(a, 1, &n);
	CHECK(n == 3 && o[0].ob_next == -1 && o[2].ob_flags & OF_LASTOB && !(o[1].ob_flags & OF_LASTOB));
	CHECK_STR(shape(a, 1), "0(2 1)");
	CHECK_STR(shape(a, 2), "0(1)");		/* the old tree 1 moved up */
	CHECK(model_ok(a));
	/* first, last, the same model */
	CHECK(rsc_tree_copy(a, 0, a, 3 - 1, NULL) == 0);
	CHECK_STR(shape(a, 0), "0(1)");
	CHECK(rsc_tree_copy(a, -1, a, 1, NULL) == 4);
	CHECK(rsc_tree_copy(a, rsc_ntrees(a), a, 1, NULL) == 5);
	CHECK(rsc_ntrees(a) == 6 && model_ok(a));
	CHECK(hash_shape(a, 1) == hash_shape(a, 1));
	/* errors */
	before = hash_rsc(a);
	CHECK(rsc_tree_copy(a, -2, b, 0, NULL) == -1);
	CHECK(rsc_tree_copy(a, 7, b, 0, NULL) == -1);
	CHECK(rsc_tree_copy(a, 0, b, 1, NULL) == -1);
	CHECK(rsc_tree_copy(a, 0, b, -1, NULL) == -1);
	CHECK(rsc_tree_copy(a, 0, NULL, 0, NULL) == -1);
	CHECK(rsc_tree_copy(NULL, 0, b, 0, NULL) == -1);
	CHECK(hash_rsc(a) == before);
	rsc_free(a);
	rsc_free(b);
	CHECK(live() == 0);
}

/* ---- trees and free items ------------------------------------------------------------- */

static void test_moves(void)
{
	struct rsc	*r = rsc_new();
	BITBLK	*b0;
	uint64_t	before;
	short	d[2] = { 5, 6 };
	BITBLK	bb;
	const char	*s0;
	int	k;

	CHECK(build_more(r) == 0);
	CHECK(rsc_tree_add(r, 1) == 2 && rsc_tree_add(r, 2) == 3);
	rsc_tree_objects(r, 2, NULL)[0].ob_x = 20;
	rsc_tree_objects(r, 3, NULL)[0].ob_x = 30;
	rsc_tree_objects(r, 1, NULL)[0].ob_x = 10;
	rsc_tree_objects(r, 0, NULL)[0].ob_x = 0;
	CHECK(rsc_tree_move(r, 0, 3) == 0);	/* 1 2 3 0 */
	CHECK(rsc_tree_objects(r, 0, NULL)[0].ob_x == 10 && rsc_tree_objects(r, 1, NULL)[0].ob_x == 20
	      && rsc_tree_objects(r, 2, NULL)[0].ob_x == 30 && rsc_tree_objects(r, 3, NULL)[0].ob_x == 0);
	CHECK(rsc_tree_move(r, 3, 1) == 0);	/* 1 0 2 3 */
	CHECK(rsc_tree_objects(r, 0, NULL)[0].ob_x == 10 && rsc_tree_objects(r, 1, NULL)[0].ob_x == 0
	      && rsc_tree_objects(r, 2, NULL)[0].ob_x == 20 && rsc_tree_objects(r, 3, NULL)[0].ob_x == 30);
	CHECK(rsc_tree_move(r, 0, -1) == 0);	/* 0 2 3 1 */
	CHECK(rsc_tree_objects(r, 0, NULL)[0].ob_x == 0 && rsc_tree_objects(r, 3, NULL)[0].ob_x == 10);
	CHECK(rsc_tree_move(r, 2, 2) == 0 && rsc_tree_objects(r, 2, NULL)[0].ob_x == 30);
	before = hash_rsc(r);
	CHECK(rsc_tree_move(r, 4, 0) == -1 && rsc_tree_move(r, -1, 0) == -1);
	CHECK(rsc_tree_move(r, 0, 4) == -1 && rsc_tree_move(r, 0, -2) == -1);
	CHECK(hash_rsc(r) == before);

	s0 = rsc_frstr(r, 0);
	CHECK(rsc_frstr_move(r, 0, 2) == 0);
	CHECK_STR(rsc_frstr(r, 0), "str1");
	CHECK_STR(rsc_frstr(r, 2), "str0");
	CHECK(rsc_frstr(r, 2) == s0);
	CHECK(rsc_frstr_move(r, 2, 0) == 0);
	CHECK_STR(rsc_frstr(r, 0), "str0");
	CHECK_STR(rsc_frstr(r, 1), "str1");
	CHECK(rsc_frstr_move(r, 0, -1) == 0);
	CHECK_STR(rsc_frstr(r, 2), "str0");
	CHECK(rsc_frstr_move(r, 1, 1) == 0);
	CHECK(rsc_frstr_move(r, 3, 0) == -1 && rsc_frstr_move(r, 0, 3) == -1
	      && rsc_frstr_move(r, -1, 0) == -1 && rsc_frstr_move(r, 0, -2) == -1);
	CHECK_STR(rsc_frstr(r, 0), "str1");

	b0 = rsc_frimg(r, 0);
	CHECK(rsc_frimg_move(r, 0, 1) == 0);
	CHECK(rsc_frimg(r, 1) == b0);
	CHECK(rsc_frimg_move(r, 1, -1) == 0 && rsc_frimg(r, 1) == b0);
	CHECK(rsc_frimg_move(r, 1, 0) == 0 && rsc_frimg(r, 0) == b0);
	CHECK(rsc_frimg_move(r, 2, 0) == -1 && rsc_frimg_move(r, 0, 2) == -1
	      && rsc_frimg_move(r, -1, 0) == -1 && rsc_frimg_move(r, 0, -2) == -1);

	/* rsc_frimg_set */
	memset(&bb, 0, sizeof bb);
	bb.bi_pdata = d;
	bb.bi_wb = 2;
	bb.bi_hl = 1;
	bb.bi_color = 3;
	bb.bi_wb = 4;
	CHECK(rsc_frimg_set(r, 1, &bb) == 0);
	CHECK(rsc_frimg(r, 1)->bi_pdata != d && rsc_frimg(r, 1)->bi_pdata[1] == 6
	      && rsc_frimg(r, 1)->bi_hl == 1 && rsc_frimg(r, 1)->bi_color == 3);
	bb.bi_pdata = NULL;
	bb.bi_wb = 4;
	CHECK(rsc_frimg_set(r, 0, &bb) == 0 && rsc_frimg(r, 0)->bi_pdata == NULL);
	before = hash_rsc(r);
	CHECK(rsc_frimg_set(r, 2, &bb) == -1 && rsc_frimg_set(r, -1, &bb) == -1
	      && rsc_frimg_set(r, 0, NULL) == -1);
	CHECK(hash_rsc(r) == before);
	for (k = 0; k < 4; k++)
		CHECK(rsc_frimg_set(r, 0, &bb) == 0);
	rsc_free(r);
	CHECK(live() == 0);
}

static int op_frimg_set(struct rsc *r)
{
	short	d[4] = { 1, 2, 3, 4 };
	BITBLK	bb;

	memset(&bb, 0, sizeof bb);
	bb.bi_pdata = d;
	bb.bi_wb = 4;
	bb.bi_hl = 2;
	return rsc_frimg_set(r, 1, &bb);
}

static void test_frimg_set_fail(void)
{
	CHECK(fail_sweep(build_more, op_frimg_set) == 2);	/* record, data */
}

/* ---- colour versions by index, resize of a colour icon ---------------------------------- */

static short	pl_a[8 * 16], pl_b[8], pl_s[8 * 16];

/* a version's planes marked by tag (the first word) */
static void planes_for(int planes, int tag)
{
	words(pl_a, 8 * planes, tag);
	words(pl_b, 8, tag + 100);
	words(pl_s, 8 * planes, tag + 200);
}

/* the tag of version v (the first word of its first plane over 257) or -1 */
static int tag_of(struct rsc_cicon *c, int v)
{
	struct rsc_cicon_version	*p = rsc_cicon_version(c, v);

	return p ? (p->c.col_data[0] - 1) / 257 : -1;
}

static int sample_cicon_obj(struct rsc *r)
{
	return rsc_obj_cicon(r, 0, 5) ? 0 : -1;
}

static int op_insert_version(struct rsc *r)
{
	planes_for(3, 40);
	return rsc_cicon_insert_version(rsc_obj_cicon(r, 0, 5), 1, 3, pl_a, pl_b, pl_s, pl_b) < 0 ? -1 : 0;
}

static int op_set_version(struct rsc *r)
{
	planes_for(3, 41);
	return rsc_cicon_set_version(rsc_obj_cicon(r, 0, 5), 1, 3, pl_a, pl_b, pl_s, pl_b);
}

static int op_resize_cicon(struct rsc *r)
{
	return rsc_cicon_resize(rsc_obj_cicon(r, 0, 5), 40, 20);
}

static void test_versions_edit(void)
{
	struct rsc	*r = rsc_new();
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	uint64_t	before;
	int	k, tags[3];

	CHECK(build_sample(r) == 0 && sample_cicon_obj(r) == 0);
	c = rsc_obj_cicon(r, 0, 5);
	CHECK(rsc_cicon_nversions(c) == 3);
	for (k = 0; k < 3; k++)
		tags[k] = tag_of(c, k);
	CHECK(tags[0] == 8 && tags[1] == 9 && tags[2] == 10);

	/* insert: first, middle, end, at the count, with -1 */
	planes_for(2, 40);
	CHECK(rsc_cicon_insert_version(c, 0, 2, pl_a, pl_b, NULL, NULL) == 0);
	CHECK(rsc_cicon_nversions(c) == 4 && tag_of(c, 0) == 40 && tag_of(c, 1) == 8 && tag_of(c, 3) == 10);
	CHECK(rsc_cicon_version(c, 0)->c.num_planes == 2 && rsc_cicon_version(c, 0)->c.sel_data == NULL);
	CHECK(rsc_cicon_version(c, 0)->data_bytes == 2 * 16 && rsc_cicon_version(c, 0)->c.col_data != pl_a);
	planes_for(1, 41);
	CHECK(rsc_cicon_insert_version(c, 2, 1, pl_a, pl_b, pl_s, pl_b) == 2);
	CHECK(tag_of(c, 2) == 41 && tag_of(c, 3) == 9 && tag_of(c, 4) == 10 && rsc_cicon_nversions(c) == 5);
	planes_for(4, 42);
	CHECK(rsc_cicon_insert_version(c, -1, 4, pl_a, pl_b, NULL, NULL) == 5);
	CHECK(c->last == rsc_cicon_version(c, 5) && tag_of(c, 5) == 42 && c->last->c.next_res == NULL);
	planes_for(1, 43);
	CHECK(rsc_cicon_insert_version(c, 6, 1, pl_a, pl_b, NULL, NULL) == 6);
	CHECK(c->last == rsc_cicon_version(c, 6) && tag_of(c, 6) == 43 && tag_of(c, 5) == 42);
	CHECK(rsc_cicon_version(c, 5)->c.next_res == &c->last->c);
	CHECK(rsc_cicon_version(c, 0)->file_ptr[0] == 0 && model_ok(r));
	for (k = 0; k < 7; k++)
		CHECK(rsc_cicon_version(c, k)->c.num_planes >= 1);

	/* errors */
	before = hash_rsc(r);
	CHECK(rsc_cicon_insert_version(c, -2, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 8, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 0, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 17, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 1, NULL, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 1, pl_a, NULL, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 1, pl_a, pl_b, pl_s, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, 0, 1, pl_a, pl_b, NULL, pl_b) == -1);
	CHECK(rsc_cicon_set_version(c, -1, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_set_version(c, 7, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_set_version(c, 0, 0, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_set_version(c, 0, 1, pl_a, pl_b, pl_s, NULL) == -1);
	CHECK(hash_rsc(r) == before);

	/* set: first, middle, last; the chain, the tail and the sizes stay right */
	rsc_cicon_version(c, 0)->file_ptr[1] = 5;
	planes_for(5, 50);
	CHECK(rsc_cicon_set_version(c, 0, 5, pl_a, pl_b, pl_s, pl_b) == 0);
	v = rsc_cicon_version(c, 0);
	CHECK(tag_of(c, 0) == 50 && v->c.num_planes == 5 && v->data_bytes == 5 * 16 && v->file_ptr[1] == 0
	      && v->c.sel_data != NULL && tag_of(c, 1) == 8);
	planes_for(1, 51);
	CHECK(rsc_cicon_set_version(c, 3, 1, pl_a, pl_b, NULL, NULL) == 0);
	CHECK(tag_of(c, 3) == 51 && tag_of(c, 2) == 41 && tag_of(c, 4) == 10
	      && rsc_cicon_version(c, 2)->c.next_res == &rsc_cicon_version(c, 3)->c);
	planes_for(2, 52);
	CHECK(rsc_cicon_set_version(c, 6, 2, pl_a, pl_b, NULL, NULL) == 0);
	CHECK(c->last == rsc_cicon_version(c, 6) && tag_of(c, 6) == 52 && c->last->c.next_res == NULL
	      && rsc_cicon_nversions(c) == 7 && model_ok(r));
	/* the limit */
	planes_for(1, 60);
	while (rsc_cicon_nversions(c) < RSC_MAX_VERSIONS)
		CHECK(rsc_cicon_insert_version(c, 1, 1, pl_a, pl_b, NULL, NULL) == 1);
	before = hash_rsc(r);
	CHECK(rsc_cicon_insert_version(c, 0, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(rsc_cicon_insert_version(c, -1, 1, pl_a, pl_b, NULL, NULL) == -1);
	CHECK(hash_rsc(r) == before);
	CHECK(rsc_cicon_set_version(c, 1, 2, pl_a, pl_b, NULL, NULL) == 0);	/* set still works */
	CHECK(model_ok(r));
	rsc_free(r);
	CHECK(live() == 0);

	CHECK(fail_sweep(build_sample_only, op_insert_version) == 5);
	CHECK(fail_sweep(build_sample_only, op_set_version) == 5);
}

/* bytes b[0..n) as a short array of the machine */
static short *shorts_of(const unsigned char *b, size_t n)
{
	short	*s = rsc_malloc(n);

	memcpy(s, b, n);
	return s;
}

static int bytes_eq(const void *p, const unsigned char *want, size_t n)
{
	return p && memcmp(p, want, n) == 0;
}

static void test_cicon_resize(void)
{
	static const unsigned char	m_data[4] = { 1, 2, 3, 4 }, m_mask[4] = { 5, 6, 7, 8 },
		col[8] = { 10, 11, 12, 13, 14, 15, 16, 17 }, cmask[4] = { 20, 21, 22, 23 },
		sel[8] = { 30, 31, 32, 33, 34, 35, 36, 37 };
	struct rsc	*r = rsc_new();
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	ICONBLK	ib;
	short	*pd, *pm, *cd, *cm, *sd, *sm;
	uint64_t	before;
	size_t	nb;
	int	k;
	static const unsigned char	e1[4 * 3] = { 1, 2, 0, 0, 3, 4, 0, 0, 0, 0, 0, 0 },
		e2[4 * 3] = { 5, 6, 0, 0, 7, 8, 0, 0, 0, 0, 0, 0 },
		e3[8 * 3] = { 10, 11, 0, 0, 12, 13, 0, 0, 0, 0, 0, 0, 14, 15, 0, 0, 16, 17, 0, 0, 0, 0, 0, 0 },
		e4[4 * 3] = { 20, 21, 0, 0, 22, 23, 0, 0, 0, 0, 0, 0 },
		e5[8 * 3] = { 30, 31, 0, 0, 32, 33, 0, 0, 0, 0, 0, 0, 34, 35, 0, 0, 36, 37, 0, 0, 0, 0, 0, 0 };

	memset(&ib, 0, sizeof ib);
	ib.ib_wicon = 16;
	ib.ib_hicon = 2;
	ib.ib_char = 7;
	ib.ib_xtext = 9;
	pd = shorts_of(m_data, 4);
	pm = shorts_of(m_mask, 4);
	ib.ib_pdata = pd;
	ib.ib_pmask = pm;
	cd = shorts_of(col, 8);
	cm = shorts_of(cmask, 4);
	sd = shorts_of(sel, 8);
	sm = shorts_of(cmask, 4);
	CHECK(rsc_tree_add(r, 1) == 0 && rsc_obj_add(r, 0, 0, G_CICON) == 1);
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, "RS") == 0);
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(rsc_cicon_add_version(c, 2, cd, cm, sd, sm) == 0);
	CHECK(rsc_cicon_add_version(c, 1, cd, cm, NULL, NULL) == 1);
	rsc_mfree(pd);
	rsc_mfree(pm);
	rsc_mfree(cd);
	rsc_mfree(cm);
	rsc_mfree(sd);
	rsc_mfree(sm);

	/* 32x3: the top left of every plane kept, zeros elsewhere */
	CHECK(rsc_cicon_resize(c, 32, 3) == 0);
	CHECK(c->blk.monoblk.ib_wicon == 32 && c->blk.monoblk.ib_hicon == 3 && c->mono_bytes == 12);
	CHECK(c->blk.monoblk.ib_char == 7 && c->blk.monoblk.ib_xtext == 9);
	CHECK(bytes_eq(c->blk.monoblk.ib_pdata, e1, 12) && bytes_eq(c->blk.monoblk.ib_pmask, e2, 12));
	v = rsc_cicon_version(c, 0);
	CHECK(v->data_bytes == 24 && v->c.num_planes == 2);
	CHECK(bytes_eq(v->c.col_data, e3, 24) && bytes_eq(v->c.col_mask, e4, 12));
	CHECK(bytes_eq(v->c.sel_data, e5, 24) && v->c.sel_mask != NULL);
	v = rsc_cicon_version(c, 1);
	CHECK(v->data_bytes == 12 && bytes_eq(v->c.col_data, e3, 12) && v->c.sel_data == NULL
	      && v->c.sel_mask == NULL);
	CHECK(model_ok(r));
	/* back to 16x1: cropped; the zeros of the padding stay where they were cut */
	CHECK(rsc_cicon_resize(c, 16, 1) == 0);
	CHECK(c->mono_bytes == 2 && bytes_eq(c->blk.monoblk.ib_pdata, m_data, 2));
	v = rsc_cicon_version(c, 0);
	CHECK(v->data_bytes == 4 && bytes_eq(v->c.col_data, col, 2) && bytes_eq((char *)v->c.col_data + 2, col + 4, 2));
	CHECK(bytes_eq(v->c.col_mask, cmask, 2) && bytes_eq((char *)v->c.sel_data + 2, sel + 4, 2));
	CHECK(rsc_cicon_nversions(c) == 2 && c->last == rsc_cicon_version(c, 1) && model_ok(r));
	/* 8x1 has the same plane as 16x1; 0x0 has none */
	CHECK(rsc_cicon_resize(c, 8, 1) == 0 && c->blk.monoblk.ib_wicon == 8 && c->mono_bytes == 2);
	CHECK(bytes_eq(c->blk.monoblk.ib_pdata, m_data, 2));
	CHECK(rsc_cicon_resize(c, 0, 0) == 0 && c->mono_bytes == 0 && rsc_cicon_version(c, 0)->data_bytes == 0);
	CHECK(rsc_cicon_resize(c, 16, 1) == 0 && c->mono_bytes == 2);
	{
		static const unsigned char	z[2] = { 0, 0 };

		CHECK(bytes_eq(c->blk.monoblk.ib_pdata, z, 2));	/* what was cropped away is gone */
	}
	CHECK(model_ok(r));

	/* NULL data stays NULL */
	memset(&ib, 0, sizeof ib);
	ib.ib_wicon = 16;
	ib.ib_hicon = 2;
	CHECK(rsc_obj_add(r, 0, 0, G_CICON) == 2 && rsc_obj_set_cicon(r, 0, 2, &ib, NULL) == 0);
	c = rsc_obj_cicon(r, 0, 2);
	CHECK(rsc_cicon_resize(c, 64, 7) == 0 && c->blk.monoblk.ib_pdata == NULL
	      && c->blk.monoblk.ib_pmask == NULL && c->mono_bytes == 8 * 7);

	/* errors and the size limit */
	before = hash_rsc(r);
	CHECK(rsc_cicon_resize(c, -1, 1) == -1 && rsc_cicon_resize(c, 1, -1) == -1);
	CHECK(rsc_cicon_resize(c, 32768, 1) == -1 && rsc_cicon_resize(c, 1, 32768) == -1);
	c->blk.monoblk.ib_wicon = 32;			/* against the contract: size and data disagree */
	CHECK(rsc_cicon_resize(c, 16, 16) == -1);
	c->blk.monoblk.ib_wicon = 64;
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(rsc_cicon_resize(c, 32767, 32767) == -1);		/* 134 MB a plane */
	CHECK(rsc_cicon_resize(c, 8192, 4096) == -1);		/* 4 MB a plane, 10 planes: over 32 MB */
	CHECK(rsc_cicon_resize(c, 1024, 1024) == 0);		/* 128 KB a plane: 7 planes fit */
	CHECK(hash_rsc(r) != before);
	nb = 0;
	for (k = 0; k < 2; k++)
		nb += rsc_cicon_version(c, k)->data_bytes;
	CHECK(nb == 3 * 128 * 1024);
	rsc_free(r);
	CHECK(live() == 0);

	CHECK(fail_sweep(build_sample_only, op_resize_cicon) == 10);	/* 2 mono planes, 2 + 4 + 2 of the versions */
}

/* ---- rsc_walk -------------------------------------------------------------------------- */

struct rec
{
	int	n, obj[32], depth[32], parent[32];
	int	skip_at, stop_at;
};

static int rec_cb(void *ctx, int obj, int depth, int parent)
{
	struct rec	*r = ctx;

	if (r->n < 32)
	{
		r->obj[r->n] = obj;
		r->depth[r->n] = depth;
		r->parent[r->n] = parent;
	}
	r->n++;
	if (obj == r->stop_at)
		return RSC_WALK_STOP;
	if (obj == r->skip_at)
		return RSC_WALK_SKIP;
	return RSC_WALK_CONTINUE;
}

static void test_walk(void)
{
	static const int	obj[6] = { 0, 1, 2, 4, 5, 3 }, dep[6] = { 0, 1, 1, 2, 2, 1 },
		par[6] = { -1, 0, 0, 2, 2, 0 };
	struct rsc	*r = rsc_new();
	struct rec	rec;
	OBJECT	*o;
	int	k, rc, n;

	CHECK(build_more(r) == 0);
	memset(&rec, 0, sizeof rec);
	rec.skip_at = rec.stop_at = -5;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == 6 && rec.n == 6);
	for (k = 0; k < 6; k++)
		CHECK(rec.obj[k] == obj[k] && rec.depth[k] == dep[k] && rec.parent[k] == par[k]);
	/* skip the children of 2 */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = 2;
	rec.stop_at = -5;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == 4 && rec.n == 4);
	CHECK(rec.obj[0] == 0 && rec.obj[1] == 1 && rec.obj[2] == 2 && rec.obj[3] == 3 && rec.depth[3] == 1);
	/* skip the root: only the root */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = 0;
	rec.stop_at = -5;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == 1);
	/* stop at 4 */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = -5;
	rec.stop_at = 4;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == 4 && rec.obj[3] == 4);
	/* from 2: depth 0 there, parent -1 */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = rec.stop_at = -5;
	CHECK(rsc_walk_from(r, 0, 2, rec_cb, &rec) == 3);
	CHECK(rec.obj[0] == 2 && rec.depth[0] == 0 && rec.parent[0] == -1 && rec.obj[1] == 4
	      && rec.depth[1] == 1 && rec.parent[1] == 2 && rec.obj[2] == 5);
	/* a leaf; the second tree */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = rec.stop_at = -5;
	CHECK(rsc_walk_from(r, 0, 3, rec_cb, &rec) == 1 && rec.parent[0] == -1);
	CHECK(rsc_walk(r, 1, rec_cb, &rec) == 2);
	/* errors: the callback is not called */
	memset(&rec, 0, sizeof rec);
	rec.skip_at = rec.stop_at = -5;
	CHECK(rsc_walk(r, 2, rec_cb, &rec) == RSC_WALK_EARG);
	CHECK(rsc_walk(r, -1, rec_cb, &rec) == RSC_WALK_EARG);
	CHECK(rsc_walk(r, 0, NULL, &rec) == RSC_WALK_EARG);
	CHECK(rsc_walk_from(r, 0, 6, rec_cb, &rec) == RSC_WALK_EARG);
	CHECK(rsc_walk_from(r, 0, -1, rec_cb, &rec) == RSC_WALK_EARG);
	o = rsc_tree_objects(r, 0, &n);
	o[3].ob_head = o[3].ob_tail = 1;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == RSC_WALK_ESHARED);
	o[3].ob_head = o[3].ob_tail = 77;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == RSC_WALK_ERANGE);
	o[3].ob_head = o[3].ob_tail = -1;
	o[0].ob_tail = 1;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == RSC_WALK_ECHAIN);
	CHECK(rec.n == 0);
	o[0].ob_tail = 3;
	CHECK(rsc_walk(r, 0, rec_cb, &rec) == 6);
	/* allocation failure */
	rec.n = 0;
	rec.skip_at = rec.stop_at = -5;
	rsc_test_fail_alloc_after(0);
	rc = rsc_walk(r, 0, rec_cb, &rec);
	rsc_test_fail_alloc_after(-1);
	CHECK(rc == RSC_WALK_ENOMEM && rec.n == 0);
	rsc_free(r);
	CHECK(live() == 0);
}

/* ---- rsc_validate ----------------------------------------------------------------------- */

struct file
{
	uint8_t	*b;
	size_t	len;
};

static struct file load_path(const char *path)
{
	struct file	f = { NULL, 0 };
	FILE	*fp = fopen(path, "rb");
	long	n;

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

static struct file load_golden(const char *name)
{
	char	path[256];

	snprintf(path, sizeof path, "tests/golden/%s", name);
	return load_path(path);
}

static int mentions(const char *msg, const char *what)
{
	return strstr(msg, what) != NULL;
}

/* A fresh build_more model mutated by f; rsc_validate must refuse it with
 * code and the position (offset, and text in the message). */
typedef void (*mutate_fn)(struct rsc *r);

static void m_cicon_loop(struct rsc *r);
static void unloop(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 5);

	rsc_cicon_version(c, 2)->c.next_res = NULL;
}

/* writer: the writer refuses the model too, with the same code (it does not
 * look at everything validate does) */
static void expect_bad(mutate_fn f, int code, size_t offset, const char *where, int writer)
{
	struct rsc	*r = rsc_new();
	struct rsc_error	e;
	uint8_t	*out;
	size_t	len;
	struct rsc_error	we;
	int	rc, wrc;

	memset(&e, 0x55, sizeof e);
	CHECK(build_more(r) == 0 && rsc_validate(r, &e) == 0 && e.code == RSC_OK);
	f(r);
	rc = rsc_validate(r, &e);
	CHECK(rc == -1 && e.code == code);
	CHECK(e.offset == offset);
	if (where)
		CHECK(mentions(e.msg, where));
	if (rc != -1 || e.code != code || e.offset != offset || (where && !mentions(e.msg, where)))
		fprintf(stderr, "  validate gave %d code %d offset %lu: %s (wanted %d %lu %s)\n", rc, e.code,
			(unsigned long)e.offset, e.msg, code, (unsigned long)offset, where ? where : "");
	/* the writer refuses what validate refuses, with the same code, unless
	 * the writer ignores it (the writer does not look at OF_LASTOB) */
	{
		int	cw, ch;

		rsc_cell(r, &cw, &ch);
		wrc = rsc_atari_write(r, cw, ch, &out, &len, &we);
	}
	if (writer)
	{
		CHECK(wrc == -1);
		CHECK(wrc != -1 || we.code == code);
	}
	if (wrc == 0)
		rsc_mfree(out);
	CHECK(rsc_validate(r, NULL) == -1);
	if (f == m_cicon_loop)
		unloop(r);
	rsc_free(r);
}

static void m_head(struct rsc *r)
{
	rsc_tree_objects(r, 0, NULL)[0].ob_head = 40;
}

static void m_cycle(struct rsc *r)
{
	OBJECT	*o = rsc_tree_objects(r, 0, NULL);

	o[3].ob_head = o[3].ob_tail = 1;
}

static void m_chain(struct rsc *r)
{
	rsc_tree_objects(r, 0, NULL)[0].ob_tail = 1;
}

static void m_lastob_extra(struct rsc *r)
{
	rsc_tree_objects(r, 0, NULL)[1].ob_flags |= OF_LASTOB;
}

static void m_lastob_gone(struct rsc *r)
{
	rsc_tree_objects(r, 1, NULL)[1].ob_flags &= ~OF_LASTOB;
}

static void m_coord_x(struct rsc *r)
{
	rsc_set_cell(r, 8, 16);
	rsc_tree_objects(r, 0, NULL)[4].ob_x = 255 * 8 + 129;
}

static void m_coord_h(struct rsc *r)
{
	rsc_set_cell(r, 8, 16);
	rsc_tree_objects(r, 1, NULL)[1].ob_height = -128;
}

static void m_te_text(struct rsc *r)
{
	TEDINFO	*te = rsc_tree_objects(r, 0, NULL)[3].ob_spec.tedinfo;
	char	*big = rsc_malloc(32768);

	memset(big, 'x', 32767);
	big[32767] = '\0';
	rsc_mfree(te->te_ptext);
	te->te_ptext = big;
}

static void m_te_tmpl(struct rsc *r)
{
	TEDINFO	*te = rsc_tree_objects(r, 0, NULL)[3].ob_spec.tedinfo;
	char	*big = rsc_malloc(40000);

	memset(big, 'y', 39999);
	big[39999] = '\0';
	rsc_mfree(te->te_ptmplt);
	te->te_ptmplt = big;
}

static void m_icon_neg(struct rsc *r)
{
	rsc_tree_objects(r, 0, NULL)[4].ob_spec.iconblk->ib_wicon = -1;
}

static void m_image_neg(struct rsc *r)
{
	BITBLK	bb;
	int	i;

	memset(&bb, 0, sizeof bb);
	bb.bi_wb = 2;
	bb.bi_hl = 2;
	i = rsc_obj_add(r, 1, 0, G_IMAGE);
	rsc_obj_set_bitblk(r, 1, i, &bb);
	rsc_tree_objects(r, 1, NULL)[i].ob_spec.bitblk->bi_hl = -2;
}

static void m_cicon_size(struct rsc *r)
{
	rsc_obj_cicon(r, 0, 5)->blk.monoblk.ib_hicon = 9;
}

static void m_cicon_mono(struct rsc *r)
{
	rsc_obj_cicon(r, 0, 5)->mono_bytes = 8;
}

static void m_cicon_planes(struct rsc *r)
{
	rsc_cicon_version(rsc_obj_cicon(r, 0, 5), 1)->c.num_planes = 17;
}

static void m_cicon_planes0(struct rsc *r)
{
	rsc_cicon_version(rsc_obj_cicon(r, 0, 5), 0)->c.num_planes = 0;
}

static void m_cicon_planes_data(struct rsc *r)
{
	rsc_cicon_version(rsc_obj_cicon(r, 0, 5), 2)->c.num_planes = 2;	/* it has 3 */
}

static void m_cicon_loop(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 5);

	rsc_cicon_version(c, 2)->c.next_res = &rsc_cicon_version(c, 0)->c;
}

static void m_cicon_many(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 5);
	struct rsc_cicon_version	*v;
	int	k;

	for (k = 0; k < RSC_MAX_VERSIONS; k++)
	{
		v = rsc_malloc(sizeof *v);
		memset(v, 0, sizeof *v);
		v->c.num_planes = 1;
		c->last->c.next_res = &v->c;
		c->last = v;
		c->nver++;
	}
}

static void m_cicon_nver(struct rsc *r)
{
	rsc_obj_cicon(r, 0, 5)->nver = 2;
}

static void m_cicon_last(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 5);

	c->last = (struct rsc_cicon_version *)rsc_cicon_version(c, 0);
}

static void m_cicon_text(struct rsc *r)
{
	rsc_obj_cicon(r, 0, 5)->text[12] = 'x';
}

static void m_cicon_ptext(struct rsc *r)
{
	rsc_obj_cicon(r, 0, 5)->blk.monoblk.ib_ptext = NULL;
}

static void m_frimg_neg(struct rsc *r)
{
	rsc_frimg(r, 1)->bi_wb = -4;
}

static void test_validate(void)
{
	static const char	*golden[] = { "rcs.rsc", "rcslow.rsc", "cicon.rsc" };
	struct rsc	*r, *r2;
	struct rsc_error	e;
	struct file	f;
	uint8_t	*out;
	size_t	len;
	OBJECT	*o;
	long	lv;
	int	k, n, t, cell, rc;

	/* models that are fine: built, the goldens (at both cell sizes), and after edits */
	r = rsc_new();
	CHECK(rsc_validate(r, &e) == 0 && e.code == RSC_OK);		/* an empty resource */
	CHECK(build_more(r) == 0 && rsc_validate(r, &e) == 0);
	rsc_free(r);
	for (k = 0; k < 3; k++)
	{
		f = load_golden(golden[k]);
		for (cell = 0; cell < 2; cell++)
		{
			r = rsc_atari_read(f.b, f.len, cell ? 8 : 0, cell ? 16 : 0, &e);
			CHECK(r != NULL && rsc_validate(r, &e) == 0);
			if (!r)
				continue;
			CHECK(rsc_atari_write(r, cell ? 8 : 0, cell ? 16 : 0, &out, &len, &e) == 0);
			r2 = rsc_atari_read(out, len, cell ? 8 : 0, cell ? 16 : 0, &e);
			CHECK(r2 && rsc_validate(r2, &e) == 0);
			rsc_free(r2);
			rsc_mfree(out);
			/* copies of every tree validate too */
			n = rsc_ntrees(r);
			for (t = 0; t < n; t++)
				CHECK(rsc_tree_copy(r, -1, r, t, NULL) == n + t);
			CHECK(rsc_validate(r, &e) == 0);
			rsc_free(r);
		}
		free(f.b);
	}

	/* what is refused, and where */
	expect_bad(m_head, RSC_ELINK, 0, "tree 0", 1);
	expect_bad(m_cycle, RSC_ECYCLE, 0, "tree 0", 1);
	expect_bad(m_chain, RSC_ELINK, 0, "tree 0", 1);
	expect_bad(m_lastob_extra, RSC_ELINK, 1, "tree 0, object 1", 0);
	expect_bad(m_lastob_gone, RSC_ELINK, 1, "tree 1, object 1", 0);
	expect_bad(m_coord_x, RSC_ETOOBIG, 4, "ob_x", 1);
	expect_bad(m_coord_h, RSC_ETOOBIG, 1, "tree 1, object 1", 1);
	expect_bad(m_te_text, RSC_ETOOBIG, 3, "TEDINFO text", 1);
	expect_bad(m_te_tmpl, RSC_ETOOBIG, 3, "TEDINFO template", 1);
	expect_bad(m_icon_neg, RSC_EARG, 4, "negative icon size", 1);
	expect_bad(m_image_neg, RSC_EARG, 2, "negative image size", 1);
	expect_bad(m_cicon_size, RSC_EARG, 5, "colour icon size", 1);
	expect_bad(m_cicon_mono, RSC_EARG, 5, "colour icon size", 1);
	expect_bad(m_cicon_planes, RSC_ECICON, 5, "colour planes", 1);
	expect_bad(m_cicon_planes0, RSC_ECICON, 5, "colour planes", 1);
	expect_bad(m_cicon_planes_data, RSC_EARG, 5, "num_planes", 1);
	expect_bad(m_cicon_loop, RSC_ECICON, 5, "loops", 1);
	expect_bad(m_cicon_many, RSC_ECICON, 5, "too many colour versions", 1);
	expect_bad(m_cicon_nver, RSC_EARG, 5, "version count", 0);
	expect_bad(m_cicon_last, RSC_EARG, 5, "version count", 0);
	expect_bad(m_cicon_text, RSC_EARG, 5, "text", 0);
	expect_bad(m_cicon_ptext, RSC_EARG, 5, "text", 0);
	expect_bad(m_frimg_neg, RSC_EARG, 1, "free image 1", 1);

	/* objects the root does not reach are not looked at */
	r = rsc_new();
	CHECK(build_sample(r) == 0 && rsc_set_cell(r, 8, 16) == 0);
	CHECK(rsc_obj_delete(r, 0, 4) == 0);
	rsc_tree_objects(r, 0, NULL)[4].ob_x = 30000;
	CHECK(rsc_validate(r, &e) == 0);
	CHECK(rsc_atari_write(r, 8, 16, &out, &len, &e) == 0);
	rsc_mfree(out);
	rsc_tree_objects(r, 0, NULL)[0].ob_x = -128;		/* but the root is */
	CHECK(rsc_validate(r, &e) == -1 && e.code == RSC_ETOOBIG && e.offset == 0);
	rsc_tree_objects(r, 0, NULL)[0].ob_x = -127;
	CHECK(rsc_validate(r, &e) == 0);
	rsc_tree_objects(r, 0, NULL)[0].ob_x = 255 * 8 + 128;
	CHECK(rsc_validate(r, &e) == 0);
	rsc_tree_objects(r, 0, NULL)[0].ob_x = 255 * 8 + 129;
	CHECK(rsc_validate(r, &e) == -1);
	o = rsc_tree_objects(r, 0, NULL);
	o[0].ob_x = 0;
	CHECK(rsc_set_cell(r, 0, 0) == 0 && rsc_validate(r, &e) == 0);	/* file form: any value */
	rsc_free(r);

	/* allocation failures: the scratch only */
	r = rsc_new();
	CHECK(build_more(r) == 0);
	lv = live();
	for (k = 0; k < 6; k++)
	{
		rsc_test_fail_alloc_after(k);
		rc = rsc_validate(r, &e);
		rsc_test_fail_alloc_after(-1);
		CHECK(live() == lv);
		if (rc == 0)
			break;
		CHECK(e.code == RSC_ENOMEM);
	}
	CHECK(rc == 0 && k >= 1);
	CHECK(rsc_validate(NULL, &e) == -1 && e.code == RSC_EARG);
	rsc_free(r);
	CHECK(live() == 0);
}

/* ---- names ------------------------------------------------------------------------------- */

static void names_dump(const struct rsc_names *n, char *buf, size_t size)
{
	struct rsc_name	e;
	size_t	at = 0;
	int	i;

	buf[0] = '\0';
	for (i = 0; i < rsc_names_count(n) && at + 80 < size; i++)
	{
		rsc_names_get(n, i, &e);
		at += (size_t)snprintf(buf + at, size - at, "%d.%d.%d.%d.%d.%s ", e.kind, e.tkind,
				       e.tree, e.obj, e.guessed, e.name);
	}
}

static int add_name(struct rsc_names *n, int kind, int tkind, int tree, int obj, const char *name,
		    int guessed)
{
	return rsc_names_add(n, kind, tkind, tree, obj, name, guessed);
}

/* the sample names: trees, objects, free items, duplicates, high bytes */
static struct rsc_names *sample_names(void)
{
	struct rsc_names	*n = rsc_names_new();
	int	bad = 0;

	bad += add_name(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "MAIN", 0);
	bad += add_name(n, RSC_NAME_OBJ, 0, 0, 1, "OK", 0);
	bad += add_name(n, RSC_NAME_OBJ, 0, 0, 2, "CANCEL", 0);
	bad += add_name(n, RSC_NAME_OBJ, 0, 0, 2, "ALIAS", 0);
	bad += add_name(n, RSC_NAME_OBJ, 0, 0, 5, "ICON", 0);
	bad += add_name(n, RSC_NAME_TREE, RSC_TKIND_MENU, 1, 0, "MENU", 0);
	bad += add_name(n, RSC_NAME_TREE, RSC_TKIND_PANL, 2, 0, "GUESSED", 1);
	bad += add_name(n, RSC_NAME_OBJ, 0, 1, 1, "ITEM", 0);
	bad += add_name(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 0, "ALERT1", 0);
	bad += add_name(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 1, "TEXT", 0);
	bad += add_name(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 2, "ALERT3", 1);
	bad += add_name(n, RSC_NAME_FRIMG, 0, 0, 1, "LOGO", 0);
	bad += add_name(n, RSC_NAME_OBJ, 0, 0, 3, "\xe4\xf6\xfc 8chr", 0);
	CHECK(bad == 0);
	return n;
}

static void test_names_write(void)
{
	static const char	*gold[] = { "src/atari/rcs.dfn", "src/atari/rcslow.dfn" };
	struct rsc_names	*n = sample_names(), *n2, *n3;
	struct rsc	*r = rsc_new();
	struct rsc_error	e;
	uint8_t	buf[4096], big[4096];
	struct file	f;
	char	d1[4096];
	int	len, len2, k, c;

	CHECK(build_more(r) == 0);
	/* the entries of the list that are not guessed, in order; with r only those r has */
	len = rsc_names_write_dfn(n, NULL, NULL, 0, &e);
	CHECK(len == 2 + 14 * 11);		/* 13 entries, 2 guessed */
	CHECK(rsc_names_write_dfn(n, NULL, buf, sizeof buf, &e) == len);
	CHECK(buf[0] == 11 && buf[1] == 0);
	/* the first record: value 0, kind DIAL, "MAIN" and zeros */
	CHECK(buf[2] == 0 && buf[3] == 0 && buf[4] == 3 && buf[5] == 0 && memcmp(buf + 6, "MAIN\0\0\0\0\0\0", 10) == 0);
	/* an object: value tree << 8 | object, kind 0x100 */
	CHECK(buf[2 + 14 + 0] == 1 && buf[2 + 14 + 1] == 0 && buf[2 + 14 + 2] == 0 && buf[2 + 14 + 3] == 1);
	n2 = rsc_names_read_dfn(buf, (size_t)len, &e);
	CHECK(n2 != NULL && rsc_names_count(n2) == 11);
	names_dump(n2, d1, sizeof d1);
	CHECK(mentions(d1, "0.3.0.0.0.MAIN ") && mentions(d1, "2.4.0.0.0.ALERT1 ") && mentions(d1, "3.0.0.1.0.LOGO ")
	      && mentions(d1, "1.0.0.2.0.ALIAS ") && !mentions(d1, "GUESSED") && !mentions(d1, "ALERT3"));
	CHECK(mentions(d1, "\xe4\xf6\xfc 8chr"));
	/* writing it again gives the same bytes */
	len2 = rsc_names_write_dfn(n2, NULL, big, sizeof big, &e);
	CHECK(len2 == len && memcmp(big, buf, (size_t)len) == 0);
	rsc_names_free(n2);
	/* with a resource: tree 2 does not exist, but none of the unguessed entries is above r */
	CHECK(rsc_names_write_dfn(n, r, NULL, 0, &e) == len);
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 1, 77, "FAR", 0) == 0);
	CHECK(add_name(n, RSC_NAME_FRSTR, 0, 0, 9, "FARSTR", 0) == 0);
	CHECK(add_name(n, RSC_NAME_FRIMG, 0, 0, 2, "FARIMG", 0) == 0);
	CHECK(add_name(n, RSC_NAME_TREE, 0, 7, 0, "FARTREE", 0) == 0);
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 9, 1, "FARTOBJ", 0) == 0);
	CHECK(rsc_names_write_dfn(n, r, NULL, 0, &e) == len);
	CHECK(rsc_names_write_dfn(n, NULL, NULL, 0, &e) == len + 14 * 5);
	/* the writer does not allocate */
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_names_write_dfn(n, r, buf, sizeof buf, &e) == len);
	rsc_test_fail_alloc_after(-1);
	/* the buffer */
	memset(big, 0xaa, sizeof big);
	CHECK(rsc_names_write_dfn(n, r, big, (size_t)len, &e) == len && big[len] == 0xaa);
	CHECK(rsc_names_write_dfn(n, r, big, (size_t)len - 1, &e) == -1 && e.code == RSC_ETOOBIG
	      && e.offset == (size_t)len - 1);
	CHECK(rsc_names_write_dfn(n, r, big, 0, &e) == -1 && rsc_names_write_dfn(n, r, big, 1, NULL) == -1);
	/* empty list */
	rsc_names_free(n);
	n = rsc_names_new();
	CHECK(rsc_names_write_dfn(n, r, buf, sizeof buf, &e) == 2 && buf[0] == 0 && buf[1] == 0);
	CHECK(rsc_names_write_dfn(n, r, buf, 1, &e) == -1);
	CHECK(rsc_names_write_dfn(NULL, r, buf, sizeof buf, &e) == -1 && e.code == RSC_EARG);
	/* what the format cannot hold is an error with the position of the record */
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 1, "FINE", 0) == 0);
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 2, "NINECHARS", 0) == 0);
	CHECK(rsc_names_write_dfn(n, r, buf, sizeof buf, &e) == -1 && e.code == RSC_ETOOBIG && e.offset == 2 + 14);
	CHECK(rsc_names_write_dfn(n, r, NULL, 0, NULL) == -1);
	rsc_names_free(n);
	n = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 255, 255, "LAST", 0) == 0);
	CHECK(rsc_names_write_dfn(n, NULL, buf, sizeof buf, &e) == 16 && buf[2] == 255 && buf[3] == 255);
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 256, 0, "TREE256", 0) == 0);
	CHECK(rsc_names_write_dfn(n, NULL, buf, sizeof buf, &e) == -1 && e.code == RSC_ETOOBIG && e.offset == 16);
	CHECK(rsc_names_remove_obj(n, 256, 0) == 1);
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 3, 256, "OBJ256", 0) == 0);
	CHECK(rsc_names_write_dfn(n, NULL, buf, sizeof buf, &e) == -1);
	CHECK(rsc_names_write_dfn(n, r, buf, sizeof buf, &e) == 2);	/* r has no such nodes: skipped */
	rsc_names_free(n);
	/* names of 8 bytes, with and without room for the NUL (the field has 10) */
	n = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_FRIMG, 0, 0, 0, "EIGHTCHR", 0) == 0);
	CHECK(rsc_names_write_dfn(n, NULL, buf, sizeof buf, &e) == 16);
	n2 = rsc_names_read_dfn(buf, 16, &e);
	CHECK(n2 && rsc_names_img_name(n2, 0) && strcmp(rsc_names_img_name(n2, 0), "EIGHTCHR") == 0);
	rsc_names_free(n2);
	rsc_names_free(n);

	/* the real files: read, write, read gives the same entries; with unknown kinds dropped */
	for (k = 0; k < 2; k++)
	{
		f = load_path(gold[k]);
		n = rsc_names_read_dfn(f.b, f.len, &e);
		CHECK(n != NULL);
		if (!n)
			continue;
		len = rsc_names_write_dfn(n, NULL, NULL, 0, &e);
		CHECK(len > 2);
		{
			uint8_t	*w = malloc((size_t)len);

			CHECK(rsc_names_write_dfn(n, NULL, w, (size_t)len, &e) == len);
			n2 = rsc_names_read_dfn(w, (size_t)len, &e);
			CHECK(n2 != NULL);
			/* the same, without the entries of unknown kind */
			n3 = rsc_names_new();
			for (c = 0; c < rsc_names_count(n); c++)
			{
				struct rsc_name	x;

				rsc_names_get(n, c, &x);
				if (x.kind != RSC_NAME_UNKNOWN)
					CHECK(add_name(n3, x.kind, x.tkind, x.tree, x.obj, x.name, 0) == 0);
			}
			if (n2)
			{
				static char	b1[200000], b2[200000];

				names_dump(n2, b1, sizeof b1);
				names_dump(n3, b2, sizeof b2);
				CHECK(strcmp(b1, b2) == 0 && rsc_names_count(n2) > 10);
			}
			rsc_names_free(n2);
			rsc_names_free(n3);
			free(w);
		}
		rsc_names_free(n);
		free(f.b);
	}
	rsc_free(r);
	CHECK(live() == 0);
}

/* the names helpers that follow edits */
static struct rsc_names *moved_names(void)
{
	struct rsc_names	*n = rsc_names_new();
	int	t, bad = 0;
	char	s[16];

	for (t = 0; t < 4; t++)
	{
		snprintf(s, sizeof s, "T%d", t);
		bad += add_name(n, RSC_NAME_TREE, RSC_TKIND_DIAL, t, 0, s, 0);
		snprintf(s, sizeof s, "O%d", t);
		bad += add_name(n, RSC_NAME_OBJ, 0, t, t + 1, s, 0);
		snprintf(s, sizeof s, "S%d", t);
		bad += add_name(n, RSC_NAME_FRSTR, 0, 0, t, s, 0);
		snprintf(s, sizeof s, "I%d", t);
		bad += add_name(n, RSC_NAME_FRIMG, 0, 0, t, s, 0);
	}
	CHECK(bad == 0);
	return n;
}

static void names_one(struct rsc_names *n, int kind, int tree, int obj, const char *name)
{
	char	buf[24];
	struct rsc_name	e;
	int	i, found = 0;

	for (i = 0; i < rsc_names_count(n); i++)
	{
		rsc_names_get(n, i, &e);
		if (strcmp(e.name, name) == 0)
		{
			found++;
			CHECK(e.kind == kind && e.tree == tree && e.obj == obj);
			if (e.kind != kind || e.tree != tree || e.obj != obj)
				fprintf(stderr, "  %s is %d.%d.%d\n", name, e.kind, e.tree, e.obj);
		}
	}
	snprintf(buf, sizeof buf, "%s once", name);
	CHECK(found == 1);
}

static int op_names_copy(struct rsc_names *n)
{
	static const int	map[4] = { -1, 2, 3, 1 };

	return rsc_names_copy_objs(n, 5, n, 0, map, 4) < 0 ? -1 : 0;
}

static void test_names_helpers(void)
{
	struct rsc_names	*n, *m;
	static char	b1[8192], b2[8192];
	int	from, to, k, map[4] = { 0, 3, -1, 1 }, cnt;
	long	lv, ac;
	struct rsc_name	e;

	/* tree moves: every from and to, against the permutation of the indices */
	for (from = 0; from < 4; from++)
		for (to = 0; to < 4; to++)
		{
			n = moved_names();
			CHECK(rsc_names_tree_moved(n, from, to) == 0);
			for (k = 0; k < 4; k++)
			{
				char	s[16];
				int	nk = k == from ? to : k < from && k >= to ? k + 1 : k > from && k <= to ? k - 1 : k;

				snprintf(s, sizeof s, "T%d", k);
				names_one(n, RSC_NAME_TREE, nk, 0, s);
				snprintf(s, sizeof s, "O%d", k);
				names_one(n, RSC_NAME_OBJ, nk, k + 1, s);
				snprintf(s, sizeof s, "S%d", k);
				names_one(n, RSC_NAME_FRSTR, 0, k, s);
			}
			CHECK(rsc_names_count(n) == 16);
			rsc_names_free(n);
			n = moved_names();
			CHECK(rsc_names_str_moved(n, from, to) == 0 && rsc_names_img_moved(n, to, from) == 0);
			for (k = 0; k < 4; k++)
			{
				char	s[16];
				int	nk = k == from ? to : k < from && k >= to ? k + 1 : k > from && k <= to ? k - 1 : k;
				int	ik = k == to ? from : k < to && k >= from ? k + 1 : k > to && k <= from ? k - 1 : k;

				snprintf(s, sizeof s, "S%d", k);
				names_one(n, RSC_NAME_FRSTR, 0, nk, s);
				snprintf(s, sizeof s, "I%d", k);
				names_one(n, RSC_NAME_FRIMG, 0, ik, s);
				snprintf(s, sizeof s, "T%d", k);
				names_one(n, RSC_NAME_TREE, k, 0, s);
			}
			rsc_names_free(n);
		}
	n = moved_names();
	CHECK(rsc_names_tree_moved(n, -1, 0) == -1 && rsc_names_tree_moved(n, 0, -1) == -1
	      && rsc_names_tree_moved(NULL, 0, 1) == -1 && rsc_names_tree_moved(n, 65536, 0) == -1);
	CHECK(rsc_names_str_moved(n, -1, 0) == -1 && rsc_names_img_moved(NULL, 0, 0) == -1);
	/* an index outside the range moved is left alone */
	CHECK(add_name(n, RSC_NAME_TREE, 0, 9, 0, "T9", 0) == 0);
	CHECK(rsc_names_tree_moved(n, 0, 3) == 0);
	names_one(n, RSC_NAME_TREE, 9, 0, "T9");
	rsc_names_free(n);

	/* removals */
	n = moved_names();
	CHECK(rsc_names_str_removed(n, 1) == 0 && rsc_names_img_removed(n, 3) == 0);
	CHECK(rsc_names_count(n) == 14);
	names_one(n, RSC_NAME_FRSTR, 0, 0, "S0");
	names_one(n, RSC_NAME_FRSTR, 0, 1, "S2");
	names_one(n, RSC_NAME_FRSTR, 0, 2, "S3");
	names_one(n, RSC_NAME_FRIMG, 0, 2, "I2");
	names_one(n, RSC_NAME_TREE, 3, 0, "T3");
	CHECK(rsc_names_str_removed(n, -1) == -1 && rsc_names_img_removed(NULL, 0) == -1);
	CHECK(rsc_names_remove(n, RSC_NAME_TREE, 2, 77) == 1 && rsc_names_remove(n, RSC_NAME_TREE, 2, 0) == 0);
	CHECK(rsc_names_remove(n, RSC_NAME_OBJ, 3, 4) == 1 && rsc_names_remove(n, RSC_NAME_OBJ, 3, 4) == 0);
	CHECK(rsc_names_remove_obj(n, 1, 2) == 1 && rsc_names_remove_obj(n, 1, 3) == 0);
	CHECK(rsc_names_remove(n, RSC_NAME_FRSTR, 55, 0) == 1 && rsc_names_remove(n, RSC_NAME_FRIMG, 0, 9) == 0);
	CHECK(rsc_names_remove(n, RSC_NAME_UNKNOWN, 0, 0) == -1 && rsc_names_remove(NULL, RSC_NAME_TREE, 0, 0) == -1
	      && rsc_names_remove(n, -1, 0, 0) == -1);
	CHECK(rsc_names_count(n) == 14 - 1 - 1 - 1 - 1);
	rsc_names_free(n);
	/* all entries of a node go, several of them too */
	n = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 1, "A", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 1, "B", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 0, 2, "C", 0) == 0);
	CHECK(rsc_names_remove_obj(n, 0, 1) == 2 && rsc_names_count(n) == 1 && rsc_names_obj_name(n, 0, 2));
	rsc_names_free(n);

	/* move_objs: tree 0 objects 1, 2, 3 to tree 5 by map { 0, 3, -1, 1 } */
	n = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 1, "a", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 2, "b", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 0, 3, "c", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 0, "r", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 1, 1, "x", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 7, "far", 0) == 0
	      && add_name(n, RSC_NAME_TREE, 0, 0, 0, "tree", 0) == 0);
	CHECK(rsc_names_move_objs(n, 0, 5, map, 4) == 3);	/* r, a and c; b is mapped to -1 */
	names_one(n, RSC_NAME_OBJ, 5, 0, "r");
	names_one(n, RSC_NAME_OBJ, 5, 3, "a");
	names_one(n, RSC_NAME_OBJ, 0, 2, "b");
	names_one(n, RSC_NAME_OBJ, 5, 1, "c");
	names_one(n, RSC_NAME_OBJ, 1, 1, "x");
	names_one(n, RSC_NAME_OBJ, 0, 7, "far");
	names_one(n, RSC_NAME_TREE, 0, 0, "tree");
	CHECK(rsc_names_move_objs(n, 5, 5, map, 4) == 3);	/* the same tree: renumbered again */
	CHECK(rsc_names_move_objs(n, 0, 5, NULL, 4) == -1 && rsc_names_move_objs(n, 0, 5, map, -1) == -1
	      && rsc_names_move_objs(n, -1, 5, map, 4) == -1 && rsc_names_move_objs(n, 0, -1, map, 4) == -1
	      && rsc_names_move_objs(NULL, 0, 5, map, 4) == -1);
	{
		int	bad[2] = { -2, 0 }, big[1] = { RSC_MAX_OBJECTS };

		CHECK(rsc_names_move_objs(n, 0, 5, bad, 2) == -1 && rsc_names_move_objs(n, 0, 5, big, 1) == -1);
	}
	rsc_names_free(n);

	/* copy_objs: into another list, into the same list, with the guessed flag kept */
	n = rsc_names_new();
	m = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 1, "a", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 2, "b", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 0, 3, "c", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 1, 1, "x", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 0, 1, "a2", 1) == 0 && add_name(n, RSC_NAME_TREE, 0, 0, 0, "tr", 0) == 0);
	CHECK(rsc_names_copy_objs(m, 4, n, 0, map, 4) == 3);	/* a, c, a2 */
	names_dump(m, b1, sizeof b1);
	CHECK(strcmp(b1, "1.0.4.3.0.a 1.0.4.1.0.c 1.0.4.3.1.a2 ") == 0);
	names_dump(n, b2, sizeof b2);
	CHECK(rsc_names_copy_objs(n, 0, n, 0, map, 4) == 3);	/* the same list: appended after */
	names_dump(n, b1, sizeof b1);
	CHECK(strncmp(b1, b2, strlen(b2)) == 0);
	CHECK(mentions(b1, "1.0.0.3.0.a 1.0.0.1.0.c 1.0.0.3.1.a2 "));
	CHECK(rsc_names_count(n) == 9);
	CHECK(rsc_names_copy_objs(m, 4, n, 9, map, 4) == 0);	/* none selected */
	CHECK(rsc_names_copy_objs(m, 4, NULL, 0, map, 4) == -1 && rsc_names_copy_objs(NULL, 4, n, 0, map, 4) == -1
	      && rsc_names_copy_objs(m, 4, n, 0, NULL, 4) == -1 && rsc_names_copy_objs(m, 4, n, 0, map, -1) == -1
	      && rsc_names_copy_objs(m, -1, n, 0, map, 4) == -1 && rsc_names_copy_objs(m, 65536, n, 0, map, 4) == -1
	      && rsc_names_copy_objs(m, 4, n, -1, map, 4) == -1);
	CHECK(rsc_names_count(m) == 3);
	rsc_names_free(m);
	/* the entry limit: nothing is added when it would not fit */
	m = rsc_names_new();
	for (k = 0; k < RSC_NAMES_MAX - 1; k++)
		if (add_name(m, RSC_NAME_FRIMG, 0, 0, k % 60000, "z", 0))
			break;
	CHECK(k == RSC_NAMES_MAX - 1);
	CHECK(rsc_names_copy_objs(m, 4, n, 0, map, 4) == -1 && rsc_names_count(m) == RSC_NAMES_MAX - 1);
	CHECK(rsc_names_copy_objs(m, 4, n, 1, map, 4) == 1 && rsc_names_count(m) == RSC_NAMES_MAX);
	rsc_names_free(m);
	rsc_names_free(n);

	/* failing allocations: the list stays as it was, nothing leaks */
	n = rsc_names_new();
	CHECK(add_name(n, RSC_NAME_OBJ, 0, 0, 1, "a", 0) == 0 && add_name(n, RSC_NAME_OBJ, 0, 0, 2, "b", 0) == 0
	      && add_name(n, RSC_NAME_OBJ, 0, 0, 3, "c", 0) == 0);
	cnt = 0;
	for (k = 0; k < 20; k++)
	{
		names_dump(n, b2, sizeof b2);
		lv = live();
		rsc_test_fail_alloc_after(k);
		ac = op_names_copy(n);
		rsc_test_fail_alloc_after(-1);
		if (ac == 0)
			break;
		cnt++;
		names_dump(n, b1, sizeof b1);
		CHECK(strcmp(b1, b2) == 0 && live() == lv);
	}
	CHECK(ac == 0 && cnt >= 2 && rsc_names_count(n) == 6);
	rsc_names_get(n, 3, &e);
	CHECK(e.tree == 5 && e.obj == 2);
	rsc_names_free(n);
	CHECK(live() == 0);
}

/* ---- the fuzz: random edits against a naive model ------------------------------------------ */

/* The naive model keeps, per tree, one node per array slot with the parent
 * and the ordered list of children: no links, no OF_LASTOB, no scratch.  What
 * the library stores in links is derived from it and compared. */

#define MAXT	6
#define MAXN	40
#define MAXF	8
#define MAXNM	600

struct nnode
{
	int	alive, parent, ti, seed, nch, ch[MAXN];
};

struct ntree
{
	int	n;
	struct nnode	nd[MAXN];
};

struct nname
{
	int	kind, tkind, tree, obj, guessed;
	char	name[16];
};

struct nmodel
{
	int	nt;
	struct ntree	t[MAXT];
	int	nfs, fs[MAXF], nfi, fi[MAXF];
	int	nn;
	struct nname	nm[MAXNM];
};

static uint32_t	rng_state = 1;

/* 0 .. n - 1 */
static int rnd(int n)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (int)(rng_state % (uint32_t)n);
}

static int pick_seed(void)
{
	int	v = rnd(NSEEDS - 1);

	return v + 1;
}

static void n_root(struct ntree *tr, int seed)
{
	memset(tr, 0, sizeof *tr);
	tr->n = 1;
	tr->nd[0].alive = 1;
	tr->nd[0].parent = -1;
	tr->nd[0].ti = 0;
	tr->nd[0].seed = seed;
}

static int n_add(struct ntree *tr, int parent, int ti, int seed)
{
	int	i = tr->n++;
	struct nnode	*nd = &tr->nd[i];

	memset(nd, 0, sizeof *nd);
	nd->alive = 1;
	nd->parent = parent;
	nd->ti = ti;
	nd->seed = seed;
	tr->nd[parent].ch[tr->nd[parent].nch++] = i;
	return i;
}

/* the live objects of the subtree of i, in index order; returns the count */
static int n_subtree(const struct ntree *tr, int i, int *list)
{
	int	mark[MAXN], stack[MAXN], sp = 0, k, c, cnt = 0;

	memset(mark, 0, sizeof mark);
	stack[sp++] = i;
	while (sp)
	{
		k = stack[--sp];
		mark[k] = 1;
		for (c = 0; c < tr->nd[k].nch; c++)
			stack[sp++] = tr->nd[k].ch[c];
	}
	for (k = 0; k < tr->n; k++)
		if (mark[k])
			list[cnt++] = k;
	return cnt;
}

static void n_unlink(struct ntree *tr, int i)
{
	struct nnode	*p = &tr->nd[tr->nd[i].parent];
	int	k;

	for (k = 0; k < p->nch && p->ch[k] != i; k++)
		;
	memmove(&p->ch[k], &p->ch[k + 1], (size_t)(p->nch - k - 1) * sizeof p->ch[0]);
	p->nch--;
}

static void n_delete(struct ntree *tr, int i)
{
	int	list[MAXN], cnt, k;

	if (!tr->nd[i].alive)
		return;
	n_unlink(tr, i);
	cnt = n_subtree(tr, i, list);
	for (k = 0; k < cnt; k++)
	{
		struct nnode	*nd = &tr->nd[list[k]];

		memset(nd, 0, sizeof *nd);
		nd->parent = -1;
	}
}

/* is moving i under parent at pos allowed */
static int n_move_ok(const struct ntree *tr, int i, int parent, int pos)
{
	int	list[MAXN], cnt, k, nsib;

	if (i <= 0 || i >= tr->n || parent < 0 || parent >= tr->n || pos < -1)
		return 0;
	if (!tr->nd[i].alive || !tr->nd[parent].alive)
		return 0;
	cnt = n_subtree(tr, i, list);
	for (k = 0; k < cnt; k++)
		if (list[k] == parent)
			return 0;
	nsib = tr->nd[parent].nch - (tr->nd[i].parent == parent);
	return pos <= nsib;
}

static void n_insert(struct ntree *tr, int parent, int i, int pos)
{
	struct nnode	*p = &tr->nd[parent];

	if (pos < 0 || pos > p->nch)
		pos = p->nch;
	memmove(&p->ch[pos + 1], &p->ch[pos], (size_t)(p->nch - pos) * sizeof p->ch[0]);
	p->ch[pos] = i;
	p->nch++;
	tr->nd[i].parent = parent;
}

static void n_move(struct ntree *tr, int i, int parent, int pos)
{
	n_unlink(tr, i);
	n_insert(tr, parent, i, pos);
}

/* Copies the subtree of si of *str into *dtr as the last objects, linked
 * under dparent at pos; map[old] gets the new index or -1.  Returns the new
 * root.  str may be dtr. */
static int n_copy_sub(struct ntree *dtr, int dparent, int pos, const struct ntree *str, int si, int *map)
{
	struct ntree	src = *str;	/* a snapshot: the destination may be the source */
	int	list[MAXN], cnt, k, c, base = dtr->n;

	cnt = n_subtree(&src, si, list);
	for (k = 0; k < src.n; k++)
		map[k] = -1;
	for (k = 0; k < cnt; k++)
		map[list[k]] = base + k;
	for (k = 0; k < cnt; k++)
	{
		struct nnode	*nd = &dtr->nd[base + k];
		const struct nnode	*sn = &src.nd[list[k]];

		memset(nd, 0, sizeof *nd);
		nd->alive = 1;
		nd->ti = sn->ti;
		nd->seed = sn->seed;
		nd->nch = sn->nch;
		for (c = 0; c < sn->nch; c++)
			nd->ch[c] = map[sn->ch[c]];
		nd->parent = list[k] != si ? map[sn->parent] : -1;
	}
	dtr->n = base + cnt;
	n_insert(dtr, dparent, map[si], pos);
	return map[si];
}

/* the live objects of *str compacted into *dst (index order kept) */
static void n_compact_into(struct ntree *dst, const struct ntree *str, int *map)
{
	struct ntree	src = *str;
	int	k, c, m = 0;

	for (k = 0; k < src.n; k++)
		map[k] = src.nd[k].alive ? m++ : -1;
	memset(dst, 0, sizeof *dst);
	dst->n = m;
	for (k = 0; k < src.n; k++)
	{
		struct nnode	*nd;

		if (map[k] < 0)
			continue;
		nd = &dst->nd[map[k]];
		nd->alive = 1;
		nd->ti = src.nd[k].ti;
		nd->seed = src.nd[k].seed;
		nd->nch = src.nd[k].nch;
		for (c = 0; c < nd->nch; c++)
			nd->ch[c] = map[src.nd[k].ch[c]];
		nd->parent = k ? map[src.nd[k].parent] : -1;
	}
}

static int n_alive_count(const struct ntree *tr)
{
	int	k, c = 0;

	for (k = 0; k < tr->n; k++)
		c += tr->nd[k].alive;
	return c;
}

/* ---- names of the naive model */

static void nm_add(struct nmodel *N, int kind, int tkind, int tree, int obj, const char *name, int guessed)
{
	struct nname	*e = &N->nm[N->nn++];

	e->kind = kind;
	e->tkind = tkind;
	e->tree = tree;
	e->obj = obj;
	e->guessed = guessed;
	strcpy(e->name, name);
}

/* drops the entries for which f says so; remaps the others by g */
static void nm_filter(struct nmodel *N, int kind, int (*newidx)(int, int, int, int), int a, int b, int c)
{
	int	i, j = 0, v;
	int	treekind = kind == RSC_NAME_TREE;

	for (i = 0; i < N->nn; i++)
	{
		struct nname	*e = &N->nm[i];

		if (e->kind != kind && !(treekind && e->kind == RSC_NAME_OBJ))
		{
			N->nm[j++] = *e;
			continue;
		}
		v = newidx(treekind ? e->tree : e->obj, a, b, c);
		if (v < 0)
			continue;
		if (treekind)
			e->tree = v;
		else
			e->obj = v;
		N->nm[j++] = *e;
	}
	N->nn = j;
}

static int idx_removed(int x, int k, int unused1, int unused2)
{
	(void)unused1;
	(void)unused2;
	return x == k ? -1 : x > k ? x - 1 : x;
}

static int idx_moved(int x, int from, int to, int unused)
{
	(void)unused;
	if (x == from)
		return to;
	if (from < to && x > from && x <= to)
		return x - 1;
	if (to < from && x >= to && x < from)
		return x + 1;
	return x;
}

/* the model's names list against the naive one, in order */
static void cmp_names(const struct rsc_names *L, const struct nmodel *N)
{
	struct rsc_name	e;
	int	i, same = rsc_names_count(L) == N->nn;

	CHECK(same);
	for (i = 0; same && i < N->nn; i++)
	{
		const struct nname	*x = &N->nm[i];

		same = rsc_names_get(L, i, &e) == 0 && e.kind == x->kind && e.tkind == x->tkind
		       && e.tree == x->tree && e.obj == x->obj && e.guessed == x->guessed
		       && strcmp(e.name, x->name) == 0;
	}
	CHECK(same);
}

/* ---- comparing */

struct preorder
{
	int	n, list[MAXN * 2];
};

static int pre_cb(void *ctx, int obj, int depth, int parent)
{
	struct preorder	*p = ctx;

	(void)depth;
	(void)parent;
	if (p->n < MAXN * 2)
		p->list[p->n] = obj;
	p->n++;
	return RSC_WALK_CONTINUE;
}

static void n_pre(const struct ntree *tr, int i, int *list, int *cnt)
{
	int	stack[MAXN], sp = 0, k, c;

	stack[sp++] = i;
	while (sp)
	{
		k = stack[--sp];
		list[(*cnt)++] = k;
		for (c = tr->nd[k].nch - 1; c >= 0; c--)
			stack[sp++] = tr->nd[k].ch[c];
	}
}

static void cmp_tree(struct rsc *r, int t, const struct ntree *tr)
{
	OBJECT	*o;
	struct preorder	pre;
	int	list[MAXN], cnt = 0, i, n, ok = 1, par;

	o = rsc_tree_objects(r, t, &n);
	CHECK(o != NULL && n == tr->n);
	if (!o || n != tr->n)
		return;
	for (i = 0; i < n; i++)
	{
		const struct nnode	*nd = &tr->nd[i];
		int	head = nd->nch ? nd->ch[0] : -1, tail = nd->nch ? nd->ch[nd->nch - 1] : -1, next = -1, k;

		if (!(!(o[i].ob_flags & OF_LASTOB) == (i != n - 1)))
			ok = 0;
		if (!nd->alive)
		{
			if (o[i].ob_next != -1 || o[i].ob_head != -1 || o[i].ob_tail != -1 || o[i].ob_type != G_IBOX
			    || (o[i].ob_flags & ~OF_LASTOB) || o[i].ob_state || o[i].ob_spec.index != 0)
				ok = 0;
			if (rsc_obj_parent(r, t, i) != -1)
				ok = 0;
			continue;
		}
		if (i)
		{
			const struct nnode	*p = &tr->nd[nd->parent];

			for (k = 0; k < p->nch && p->ch[k] != i; k++)
				;
			next = k == p->nch - 1 ? nd->parent : p->ch[k + 1];
		}
		par = rsc_obj_parent(r, t, i);
		if (o[i].ob_next != next || o[i].ob_head != head || o[i].ob_tail != tail || par != nd->parent)
			ok = 0;
		{
			uint64_t	h = 14695981039346656037ULL;

			hash_obj(&h, &o[i], 1);
			if (h != expect_hash(nd->ti, nd->seed))
				ok = 0;
		}
	}
	CHECK(ok);
	n_pre(tr, 0, list, &cnt);
	pre.n = 0;
	CHECK(rsc_walk(r, t, pre_cb, &pre) == cnt && pre.n == cnt);
	CHECK(memcmp(pre.list, list, (size_t)cnt * sizeof list[0]) == 0);
	CHECK(cnt == n_alive_count(tr));
}

/* a free image of seed v: 2 x 2 bytes */
static void fill_bb(BITBLK *bb, short *d, int v)
{
	memset(bb, 0, sizeof *bb);
	words(d, 2, v);
	bb->bi_pdata = d;
	bb->bi_wb = 2;
	bb->bi_hl = 2;
	bb->bi_color = (short)v;
}

static void cmp_all(struct rsc *r, const struct nmodel *N, const struct rsc_names *L)
{
	int	t, k;
	char	s[16];

	CHECK(rsc_ntrees(r) == N->nt);
	for (t = 0; t < N->nt && t < rsc_ntrees(r); t++)
		cmp_tree(r, t, &N->t[t]);
	CHECK(rsc_nfrstr(r) == N->nfs && rsc_nfrimg(r) == N->nfi);
	for (k = 0; k < N->nfs && k < rsc_nfrstr(r); k++)
	{
		snprintf(s, sizeof s, "f%d", N->fs[k]);
		CHECK_STR(rsc_frstr(r, k), s);
	}
	for (k = 0; k < N->nfi && k < rsc_nfrimg(r); k++)
	{
		BITBLK	*bb = rsc_frimg(r, k);
		short	d[2];

		words(d, 2, N->fi[k]);
		CHECK(bb->bi_color == N->fi[k] && bb->bi_wb == 2 && bb->bi_hl == 2 && bb->bi_pdata
		      && memcmp(bb->bi_pdata, d, 4) == 0);
	}
	cmp_names(L, N);
	CHECK(model_ok(r));
}

/* write, read, write again: a fixed point, and the names through a .DFN */
static long	fuzz_writes, fuzz_name_bytes;

static void roundtrip(struct rsc *r, const struct nmodel *N, const struct rsc_names *L)
{
	struct rsc_error	e;
	struct rsc	*r2;
	uint8_t	*b1, *b2;
	size_t	l1, l2;
	uint8_t	dfn[2 + 14 * MAXNM];
	struct rsc_names	*L2;
	struct rsc_name	x;
	int	len, i, j = 0, ok = 1;

	if (rsc_atari_write(r, 0, 0, &b1, &l1, &e))
	{
		CHECK(0 && "write");
		fprintf(stderr, "  write failed: %s\n", e.msg);
		return;
	}
	r2 = rsc_atari_read(b1, l1, 0, 0, &e);
	CHECK(r2 != NULL);
	if (r2)
	{
		CHECK(hash_shape(r2, 0) == hash_shape(r, 0));
		CHECK(model_ok(r2));
		CHECK(rsc_atari_write(r2, 0, 0, &b2, &l2, &e) == 0);
		CHECK(l1 == l2 && memcmp(b1, b2, l1) == 0);
		rsc_mfree(b2);
		rsc_free(r2);
	}
	rsc_mfree(b1);
	fuzz_writes++;
	/* names: what the list has for nodes the model has, not guessed */
	len = rsc_names_write_dfn(L, r, dfn, sizeof dfn, &e);
	CHECK(len >= 2);
	if (len < 2)
		return;
	fuzz_name_bytes += len;
	L2 = rsc_names_read_dfn(dfn, (size_t)len, &e);
	CHECK(L2 != NULL);
	if (!L2)
		return;
	for (i = 0; i < N->nn; i++)
	{
		const struct nname	*n = &N->nm[i];
		int	has = n->kind == RSC_NAME_TREE ? n->tree < N->nt
			      : n->kind == RSC_NAME_OBJ ? n->tree < N->nt && n->obj < N->t[n->tree].n
			      : n->kind == RSC_NAME_FRSTR ? n->obj < N->nfs : n->obj < N->nfi;

		if (n->guessed || !has)
			continue;
		if (j >= rsc_names_count(L2) || rsc_names_get(L2, j, &x) || x.kind != n->kind
		    || x.tkind != n->tkind || x.tree != n->tree || x.obj != n->obj || x.guessed
		    || strcmp(x.name, n->name))
			ok = 0;
		j++;
	}
	CHECK(ok && j == rsc_names_count(L2));
	rsc_names_free(L2);
}

/* ---- one run */

struct inj
{
	uint64_t	h;
	long	lv;
	int	on;
};

static long	fuzz_injected, fuzz_failed_by_injection, fuzz_ops[17], fuzz_done[17];
static int	cur_op;

static void inj_begin(struct inj *j, struct rsc *r)
{
	int	k;

	j->h = hash_rsc(r);
	j->lv = live();
	j->on = 0;
	k = rnd(5);
	if (k == 0)
	{
		k = rnd(8);
		rsc_test_fail_alloc_after(k);
		j->on = 1;
		fuzz_injected++;
	}
}

static void inj_end(void)
{
	rsc_test_fail_alloc_after(-1);
}

/* After an op that returned done (it took effect) or not: whether the naive
 * model has to apply it.  A failed op must have a reason (it was not valid,
 * or an allocation was made to fail) and change nothing. */
static int settle(const struct inj *j, struct rsc *r, int valid, int done)
{
	if (done)
	{
		CHECK(valid);
		fuzz_done[cur_op] += valid;
		return valid;
	}
	CHECK(!valid || j->on);
	CHECK(hash_rsc(r) == j->h && live() == j->lv);
	if (valid)
		fuzz_failed_by_injection++;
	return 0;
}

static void make_root(struct rsc *r, int t, int seed)
{
	CHECK(rsc_obj_set_type(r, t, 0, G_BOX) == 0 && fill_spec(r, t, 0, G_BOX, seed) == 0);
}

/* a random model with nt trees of up to MAXN / 2 objects (B and the start of A) */
static void random_model(struct rsc *r, struct nmodel *N, struct rsc_names *L, int nt)
{
	int	t, k, cnt, parent, ti, seed, i;
	char	s[16];

	for (t = 0; t < nt; t++)
	{
		seed = pick_seed();
		CHECK(rsc_tree_add(r, 1) == t);
		make_root(r, t, seed);
		n_root(&N->t[t], seed);
		N->nt++;
		cnt = rnd(MAXN / 2);
		for (k = 0; k < cnt; k++)
		{
			parent = rnd(N->t[t].n);
			ti = rnd(NTYPES);
			seed = pick_seed();
			i = add_obj(r, t, parent, ti, seed);
			CHECK(i == n_add(&N->t[t], parent, ti, seed));
			if (rnd(4) == 0)
			{
				snprintf(s, sizeof s, "n%d", N->nn);
				CHECK(rsc_names_add(L, RSC_NAME_OBJ, 0, t, i, s, 0) == 0);
				nm_add(N, RSC_NAME_OBJ, 0, t, i, s, 0);
			}
		}
		snprintf(s, sizeof s, "T%d", t);
		CHECK(rsc_names_add(L, RSC_NAME_TREE, RSC_TKIND_DIAL, t, 0, s, 0) == 0);
		nm_add(N, RSC_NAME_TREE, RSC_TKIND_DIAL, t, 0, s, 0);
	}
}

static void fuzz_run(uint32_t seed, int iters, int verbose)
{
	struct rsc	*A = rsc_new(), *B = rsc_new();
	struct nmodel	*NA = calloc(1, sizeof *NA), *NB = calloc(1, sizeof *NB);
	struct rsc_names	*LA = rsc_names_new(), *LB = rsc_names_new();
	struct inj	j;
	int	it, op, t, i, k, n, rc, parent, pos, ti, sd, st, si, dt, from, nsib, cnt, map[MAXN], nmap[MAXN];
	int	list[MAXN], done, reported = 0;
	long	fails_before;
	char	s[16];
	struct rsc	*S;
	struct nmodel	*NS;
	struct rsc_names	*LS;

	rng_state = seed * 2654435761u + 12345u;
	if (!rng_state)
		rng_state = 1;
	random_model(B, NB, LB, 3);
	random_model(A, NA, LA, 1 + rnd(2));
	cmp_all(A, NA, LA);
	cmp_all(B, NB, LB);

	for (it = 0; it < iters; it++)
	{
		fails_before = failures;
		op = rnd(17);
		cur_op = op;
		fuzz_ops[op]++;
		switch (op)
		{
		case 0:
		case 1:		/* add an object */
			t = rnd(NA->nt);
			if (NA->t[t].n >= MAXN)
				break;
			parent = rnd(NA->t[t].n);
			if (!NA->t[t].nd[parent].alive)
				parent = 0;
			ti = rnd(NTYPES);
			sd = pick_seed();
			inj_begin(&j, A);
			rc = rsc_obj_add(A, t, parent, types[ti]);
			inj_end();
			if (settle(&j, A, 1, rc >= 0))
			{
				CHECK(fill_spec(A, t, rc, types[ti], sd) == 0);
				CHECK(rc == n_add(&NA->t[t], parent, ti, sd));
				if (rnd(3) == 0)
				{
					snprintf(s, sizeof s, "n%d", NA->nn);
					CHECK(rsc_names_add(LA, RSC_NAME_OBJ, 0, t, rc, s, 0) == 0);
					nm_add(NA, RSC_NAME_OBJ, 0, t, rc, s, 0);
				}
			}
			break;
		case 2:		/* delete a subtree (any index: the root and out of range fail) */
			t = rnd(NA->nt);
			i = rnd(NA->t[t].n + 1);
			inj_begin(&j, A);
			rc = rsc_obj_delete(A, t, i);
			inj_end();
			if (settle(&j, A, i > 0 && i < NA->t[t].n, rc == 0))
				n_delete(&NA->t[t], i);
			break;
		case 3:
		case 4:
		case 5:		/* move or reorder */
			t = rnd(NA->nt);
			i = rnd(NA->t[t].n + 1);
			parent = rnd(NA->t[t].n + 1);
			nsib = (parent < NA->t[t].n ? NA->t[t].nd[parent].nch : 0) + 2;
			pos = rnd(nsib + 1) - 2;
			inj_begin(&j, A);
			rc = rsc_obj_move(A, t, i, parent, pos);
			inj_end();
			if (settle(&j, A, n_move_ok(&NA->t[t], i, parent, pos), rc == 0))
				n_move(&NA->t[t], i, parent, pos);
			break;
		case 6:
		case 7:
		case 8:		/* copy a subtree, from A or from B */
			S = rnd(3) ? A : B;
			NS = S == A ? NA : NB;
			LS = S == A ? LA : LB;
			dt = rnd(NA->nt);
			st = rnd(NS->nt);
			parent = rnd(NA->t[dt].n + 1);
			si = rnd(NS->t[st].n);
			if (!NS->t[st].nd[si].alive)
				si = 0;
			cnt = n_subtree(&NS->t[st], si, list);
			nsib = (parent < NA->t[dt].n ? NA->t[dt].nd[parent].nch : 0) + 2;
			pos = rnd(nsib + 1) - 2;
			if (NA->t[dt].n + cnt > MAXN)
				break;
			inj_begin(&j, A);
			rc = rsc_obj_copy_subtree(A, dt, parent, pos, S, st, si, map);
			inj_end();
			done = settle(&j, A, parent < NA->t[dt].n && NA->t[dt].nd[parent].alive && pos >= -1
					     && pos <= NA->t[dt].nd[parent].nch, rc >= 0);
			if (done)
			{
				n = NS->t[st].n;
				CHECK(rc == n_copy_sub(&NA->t[dt], parent, pos, &NS->t[st], si, nmap));
				CHECK(memcmp(map, nmap, (size_t)n * sizeof map[0]) == 0);
				CHECK(rsc_names_copy_objs(LA, dt, LS, st, map, n) >= 0);
				for (k = 0; k < NS->nn; k++)
				{
					const struct nname	*e = &NS->nm[k];

					if (e->kind == RSC_NAME_OBJ && e->tree == st && e->obj < n && nmap[e->obj] >= 0)
						nm_add(NA, RSC_NAME_OBJ, 0, dt, nmap[e->obj], e->name, e->guessed);
				}
			}
			break;
		case 9:
		case 10:	/* copy a whole tree, at the end or at a position */
			S = rnd(3) ? A : B;
			NS = S == A ? NA : NB;
			LS = S == A ? LA : LB;
			st = rnd(NS->nt);
			if (NA->nt >= MAXT)
				break;
			n = NS->t[st].n;
			pos = rnd(NA->nt + 2) - 1;
			from = NA->nt;		/* where the tree is appended */
			inj_begin(&j, A);
			rc = rsc_tree_copy(A, -1, S, st, map);
			inj_end();
			if (settle(&j, A, 1, rc >= 0))
			{
				CHECK(rc == from);
				n_compact_into(&NA->t[from], &NS->t[st], nmap);
				CHECK(memcmp(map, nmap, (size_t)n * sizeof map[0]) == 0);
				NA->nt++;
				CHECK(rsc_names_copy_objs(LA, from, LS, st, map, n) >= 0);
				for (k = 0; k < NS->nn; k++)
				{
					const struct nname	*e = &NS->nm[k];

					if (e->kind == RSC_NAME_OBJ && e->tree == st && e->obj < n && nmap[e->obj] >= 0)
						nm_add(NA, RSC_NAME_OBJ, 0, from, nmap[e->obj], e->name, e->guessed);
				}
				snprintf(s, sizeof s, "C%d", NA->nn);
				CHECK(rsc_names_add(LA, RSC_NAME_TREE, RSC_TKIND_PANL, from, 0, s, rnd(2)) == 0);
				{
					struct rsc_name	x;

					rsc_names_get(LA, rsc_names_count(LA) - 1, &x);
					nm_add(NA, RSC_NAME_TREE, RSC_TKIND_PANL, from, 0, s, x.guessed);
				}
				/* and to its place, the names following */
				if (pos >= 0 && pos < from)
				{
					CHECK(rsc_tree_move(A, from, pos) == 0);
					CHECK(rsc_names_tree_moved(LA, from, pos) == 0);
					{
						struct ntree	moved = NA->t[from];

						memmove(&NA->t[pos + 1], &NA->t[pos], (size_t)(from - pos) * sizeof NA->t[0]);
						NA->t[pos] = moved;
						nm_filter(NA, RSC_NAME_TREE, idx_moved, from, pos, 0);
					}
				}
			}
			break;
		case 11:	/* move a tree, also with a bad index */
			t = rnd(NA->nt + 1);
			pos = rnd(NA->nt + 2) - 1;
			rc = rsc_tree_move(A, t, pos);
			CHECK((rc == 0) == (t < NA->nt && pos >= -1 && pos < NA->nt));
			if (rc == 0)
			{
				struct ntree	moved = NA->t[t];
				int	to = pos < 0 ? NA->nt - 1 : pos;

				CHECK(rsc_names_tree_moved(LA, t, to) == 0);
				if (to > t)
					memmove(&NA->t[t], &NA->t[t + 1], (size_t)(to - t) * sizeof NA->t[0]);
				else
					memmove(&NA->t[to + 1], &NA->t[to], (size_t)(t - to) * sizeof NA->t[0]);
				NA->t[to] = moved;
				nm_filter(NA, RSC_NAME_TREE, idx_moved, t, to, 0);
			}
			break;
		case 12:	/* delete a tree, or add one */
			if (NA->nt > 1 && rnd(2))
			{
				t = rnd(NA->nt);
				CHECK(rsc_tree_delete(A, t) == 0 && rsc_names_tree_removed(LA, t) == 0);
				memmove(&NA->t[t], &NA->t[t + 1], (size_t)(NA->nt - t - 1) * sizeof NA->t[0]);
				NA->nt--;
				nm_filter(NA, RSC_NAME_TREE, idx_removed, t, 0, 0);
			}
			else if (NA->nt < MAXT)
			{
				sd = pick_seed();
				inj_begin(&j, A);
				rc = rsc_tree_add(A, 1);
				inj_end();
				if (settle(&j, A, 1, rc >= 0))
				{
					CHECK(rc == NA->nt);
					make_root(A, rc, sd);
					n_root(&NA->t[NA->nt], sd);
					snprintf(s, sizeof s, "N%d", NA->nn);
					CHECK(rsc_names_add(LA, RSC_NAME_TREE, RSC_TKIND_MENU, rc, 0, s, 0) == 0);
					nm_add(NA, RSC_NAME_TREE, RSC_TKIND_MENU, rc, 0, s, 0);
					NA->nt++;
				}
			}
			break;
		case 13:	/* compact */
			t = rnd(NA->nt);
			n = NA->t[t].n;
			inj_begin(&j, A);
			rc = rsc_tree_compact(A, t, map);
			inj_end();
			if (settle(&j, A, 1, rc >= 0))
			{
				struct ntree	c;

				n_compact_into(&c, &NA->t[t], nmap);
				CHECK(rc == c.n && memcmp(map, nmap, (size_t)n * sizeof map[0]) == 0);
				NA->t[t] = c;
				CHECK(rsc_names_renumber(LA, t, map, n) == 0);
				{
					int	e, j2 = 0;

					for (e = 0; e < NA->nn; e++)
					{
						struct nname	*x = &NA->nm[e];

						if (x->kind == RSC_NAME_OBJ && x->tree == t)
						{
							if (x->obj >= n || nmap[x->obj] < 0)
								continue;
							x->obj = nmap[x->obj];
						}
						NA->nm[j2++] = *x;
					}
					NA->nn = j2;
				}
			}
			break;
		case 14:	/* free strings */
			if (NA->nfs < MAXF && rnd(2))
			{
				sd = pick_seed();
				snprintf(s, sizeof s, "f%d", sd);
				inj_begin(&j, A);
				rc = rsc_frstr_add(A, s);
				inj_end();
				if (settle(&j, A, 1, rc >= 0))
				{
					CHECK(rc == NA->nfs);
					NA->fs[NA->nfs++] = sd;
				}
			}
			else if (NA->nfs)
			{
				k = rnd(NA->nfs);
				pos = rnd(NA->nfs + 1) - 1;
				switch (rnd(3))
				{
				case 0:
					CHECK(rsc_frstr_delete(A, k) == 0 && rsc_names_str_removed(LA, k) == 0);
					memmove(&NA->fs[k], &NA->fs[k + 1], (size_t)(NA->nfs - k - 1) * sizeof NA->fs[0]);
					NA->nfs--;
					nm_filter(NA, RSC_NAME_FRSTR, idx_removed, k, 0, 0);
					break;
				case 1:
					CHECK(rsc_frstr_move(A, k, pos) == 0);
					{
						int	to = pos < 0 ? NA->nfs - 1 : pos, moved = NA->fs[k];

						CHECK(rsc_names_str_moved(LA, k, to) == 0);
						if (to > k)
							memmove(&NA->fs[k], &NA->fs[k + 1], (size_t)(to - k) * sizeof NA->fs[0]);
						else
							memmove(&NA->fs[to + 1], &NA->fs[to], (size_t)(k - to) * sizeof NA->fs[0]);
						NA->fs[to] = moved;
						nm_filter(NA, RSC_NAME_FRSTR, idx_moved, k, to, 0);
					}
					break;
				default:
					sd = pick_seed();
					snprintf(s, sizeof s, "f%d", sd);
					inj_begin(&j, A);
					rc = rsc_frstr_set(A, k, s);
					inj_end();
					if (settle(&j, A, 1, rc == 0))
						NA->fs[k] = sd;
					break;
				}
				if (rnd(3) == 0 && NA->nfs)
				{
					k = rnd(NA->nfs);
					snprintf(s, sizeof s, "s%d", NA->nn);
					CHECK(rsc_names_add(LA, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, k, s, 0) == 0);
					nm_add(NA, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, k, s, 0);
				}
			}
			break;
		case 15:	/* free images */
			if (NA->nfi < MAXF && rnd(2))
			{
				BITBLK	bb;
				short	d[2];

				sd = pick_seed();
				fill_bb(&bb, d, sd);
				inj_begin(&j, A);
				rc = rsc_frimg_add(A, &bb);
				inj_end();
				if (settle(&j, A, 1, rc >= 0))
				{
					CHECK(rc == NA->nfi);
					NA->fi[NA->nfi++] = sd;
				}
			}
			else if (NA->nfi)
			{
				k = rnd(NA->nfi);
				pos = rnd(NA->nfi + 1) - 1;
				switch (rnd(3))
				{
				case 0:
					CHECK(rsc_frimg_delete(A, k) == 0 && rsc_names_img_removed(LA, k) == 0);
					memmove(&NA->fi[k], &NA->fi[k + 1], (size_t)(NA->nfi - k - 1) * sizeof NA->fi[0]);
					NA->nfi--;
					nm_filter(NA, RSC_NAME_FRIMG, idx_removed, k, 0, 0);
					break;
				case 1:
					CHECK(rsc_frimg_move(A, k, pos) == 0);
					{
						int	to = pos < 0 ? NA->nfi - 1 : pos, moved = NA->fi[k];

						CHECK(rsc_names_img_moved(LA, k, to) == 0);
						if (to > k)
							memmove(&NA->fi[k], &NA->fi[k + 1], (size_t)(to - k) * sizeof NA->fi[0]);
						else
							memmove(&NA->fi[to + 1], &NA->fi[to], (size_t)(k - to) * sizeof NA->fi[0]);
						NA->fi[to] = moved;
						nm_filter(NA, RSC_NAME_FRIMG, idx_moved, k, to, 0);
					}
					break;
				default:
					{
						BITBLK	bb;
						short	d[2];

						sd = pick_seed();
						fill_bb(&bb, d, sd);
						inj_begin(&j, A);
						rc = rsc_frimg_set(A, k, &bb);
						inj_end();
						if (settle(&j, A, 1, rc == 0))
							NA->fi[k] = sd;
					}
					break;
				}
			}
			break;
		default:	/* an edit that must fail: the model stays as it is */
			t = rnd(NA->nt);
			i = rnd(NA->t[t].n);
			if (NA->t[t].nd[i].nch)
			{
				/* under one of its own descendants */
				int	d = NA->t[t].nd[i].ch[rnd(NA->t[t].nd[i].nch)];

				inj_begin(&j, A);
				rc = rsc_obj_move(A, t, i, d, rnd(2) - 1);
				inj_end();
				CHECK(rc == -1);
				CHECK(hash_rsc(A) == j.h && live() == j.lv);
			}
			break;
		}
		cmp_all(A, NA, LA);
		if (failures != fails_before && !reported)
		{
			fprintf(stderr, "  first failure: seed %u, edit %d, op %d\n", (unsigned)seed, it, op);
			reported = 1;
		}
		if (it % 40 == 39)
		{
			roundtrip(A, NA, LA);
			if (verbose)
				fprintf(stderr, "  .. %d\n", it);
		}
	}
	roundtrip(A, NA, LA);
	roundtrip(B, NB, LB);
	rsc_free(A);
	rsc_free(B);
	rsc_names_free(LA);
	rsc_names_free(LB);
	free(NA);
	free(NB);
	CHECK(live() == 0);
}

static void test_fuzz(void)
{
	uint32_t	seed;
	long	before = checks;
	int	k;

	for (seed = 1; seed <= 12; seed++)
		fuzz_run(seed, 700, 0);
	printf("  fuzz: 12 runs of 700 edits, %ld checks, %ld writes read back, %ld allocation failures injected"
	       " (%ld made an edit fail), %ld bytes of names; ops:", checks - before, fuzz_writes, fuzz_injected,
	       fuzz_failed_by_injection, fuzz_name_bytes);
	for (k = 0; k < 17; k++)
		printf(" %ld/%ld", fuzz_done[k], fuzz_ops[k]);
	printf("\n");
}

int main(void)
{
	test_move();
	test_move_deep();
	test_copy();
	test_copy_limits();
	test_tree_copy();
	test_moves();
	test_frimg_set_fail();
	test_versions_edit();
	test_cicon_resize();
	test_walk();
	test_validate();
	test_names_write();
	test_names_helpers();
	test_fuzz();
	printf("test_edit: %ld checks, %ld failures\n", checks, failures);
	return failures ? 1 : 0;
}
