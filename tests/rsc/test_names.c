/* TEST_NAMES.C	host tests of the names of a resource (rscnames.c):
 *		make rsc-test
 *
 * Reads src/atari/rcs.dfn and rcslow.dfn with their resources (paths
 * relative to the repository root, where make runs the tests) and small
 * .DFN, .DEF and .HRD files built here with the structure of the real files
 * that are not part of the tree (EmuTOS' desktop.def, gem.def, icon.def,
 * mform.def, TeraDesk's fm_desktop.hrd), and runs the error paths:
 * truncations, mutations, the budget, the entry limit and allocation
 * failures; then the guesser, the lookups, the check against a resource and
 * the renumbering.  When the environment variable RSC_TEST_EXTERNAL names
 * one or more directories (separated by ':'), every *.dfn, *.def, *.rsd and
 * *.hrd in them is also read with the *.rsc of the same base name, checked
 * against it and guessed (CI does not set it: real files are not part of
 * the tree). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
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

/* ---- files ------------------------------------------------------------------- */

static struct rsc	*rcs_rsc;	/* src/atari/rcs.rsc, file form */
static long	base_live;	/* its blocks */

/* the blocks allocated by the library beyond rcs_rsc */
static long live(void)
{
	return rsc_test_live_blocks() - base_live;
}

struct file
{
	uint8_t	*b;
	size_t	len;
};

/* The file at path, or b NULL when it cannot be read. */
static struct file load_path(const char *path)
{
	struct file	f = { NULL, 0 };
	FILE	*fp = fopen(path, "rb");
	long	n;

	if (!fp)
		return f;
	if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 && fseek(fp, 0, SEEK_SET) == 0)
	{
		f.b = malloc(n ? (size_t)n : 1);
		if (f.b && fread(f.b, 1, (size_t)n, fp) == (size_t)n)
			f.len = (size_t)n;
		else
		{
			free(f.b);
			f.b = NULL;
		}
	}
	fclose(fp);
	return f;
}

static struct file load(const char *path)
{
	struct file	f = load_path(path);

	if (!f.b)
	{
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	return f;
}

static struct rsc *load_rsc(const char *path)
{
	struct file	f = load(path);
	struct rsc_error	err;
	struct rsc	*r = rsc_atari_read(f.b, f.len, 0, 0, &err);

	free(f.b);
	if (!r)
	{
		fprintf(stderr, "cannot decode %s: %s\n", path, err.msg);
		exit(2);
	}
	return r;
}

enum { DFN, DEF, HRD };

/* Reads len bytes of b in format fmt from an exactly sized copy (so that
 * the sanitizer sees any read past the end). */
static struct rsc_names *read_copy(int fmt, const uint8_t *b, size_t len, struct rsc_error *err)
{
	uint8_t	*copy = malloc(len ? len : 1);
	struct rsc_names	*n;

	memcpy(copy, b, len);
	if (fmt == DFN)
		n = rsc_names_read_dfn(copy, len, err);
	else if (fmt == DEF)
		n = rsc_names_read_def(copy, len, err);
	else
		n = rsc_names_read_hrd(copy, len, err);
	free(copy);
	return n;
}

static struct rsc_name entry(const struct rsc_names *n, int i)
{
	struct rsc_name	e;

	memset(&e, 0x55, sizeof e);
	CHECK(rsc_names_get(n, i, &e) == 0);
	return e;
}

/* entry i is kind, tkind, tree, obj, name */
static int is(const struct rsc_names *n, int i, int kind, int tkind, int tree, int obj, const char *name)
{
	struct rsc_name	e;

	if (rsc_names_get(n, i, &e))
		return 0;
	return e.kind == kind && e.tkind == tkind && e.tree == tree && e.obj == obj
	       && strcmp(e.name, name) == 0 && e.order == i;
}

/* ---- builders ------------------------------------------------------------------ */

static void put16le(uint8_t *b, size_t off, unsigned v)
{
	b[off] = (uint8_t)v;
	b[off + 1] = (uint8_t)(v >> 8);
}

static void put16be(uint8_t *b, size_t off, unsigned v)
{
	b[off] = (uint8_t)(v >> 8);
	b[off + 1] = (uint8_t)v;
}

/* .DFN record i: value, kind, the name (at most 10 bytes, without NUL if
 * 10); the rest of the 10 bytes is stale */
static void dfn_rec(uint8_t *b, int i, unsigned val, unsigned kind, const char *name, const char *stale)
{
	size_t	p = 2 + 14 * (size_t)i, k = strlen(name);

	put16le(b, p, val);
	put16le(b, p + 2, kind);
	memset(b + p + 4, 0, 10);
	memcpy(b + p + 4, name, k);
	if (stale && k < 9)
		memcpy(b + p + 5 + k, stale, strlen(stale) < 9 - k ? strlen(stale) : 9 - k);
}

/* A .DFN with all kinds: its length */
#define DFN_N	10
static size_t build_dfn(uint8_t *b)
{
	put16le(b, 0, DFN_N);
	dfn_rec(b, 0, 21, 4, "STNODEF", NULL);
	dfn_rec(b, 1, 0, 2, "MENUTREE", NULL);		/* 8 bytes, no NUL */
	dfn_rec(b, 2, 1, 3, "TOOLBOX", "5");
	dfn_rec(b, 3, 2, 1, "PANEL", "xyz");
	dfn_rec(b, 4, 3, 0, "ANY", NULL);
	dfn_rec(b, 5, 0x30a, 0x100, "OKITEM", NULL);
	dfn_rec(b, 6, 7, 0x101, "STR7", NULL);
	dfn_rec(b, 7, 1, 0x102, "CLIPFULL", NULL);
	dfn_rec(b, 8, 4, 5, "FREE", NULL);			/* never in a file: unknown */
	dfn_rec(b, 9, 0x0102, 0x100, "NINECHARS", NULL);	/* the 9th char is ignored */
	return 2 + 14 * DFN_N;
}

/* .DEF record i */
static void def_rec(uint8_t *b, int i, int t, int o, int ind, int ty, const char *name)
{
	size_t	p = 16 * (size_t)i;

	if (i)
		memset(b + p, 0, 4);
	b[p + 4] = (uint8_t)t;
	b[p + 5] = (uint8_t)o;
	b[p + 6] = (uint8_t)ind;
	b[p + 7] = (uint8_t)ty;
	memset(b + p + 8, 0, 8);
	memcpy(b + p + 8, name, strlen(name) < 8 ? strlen(name) : 8);
}

/* A .DEF like EmuTOS' desktop.def (zero record when zero, flags 0x0101)
 * or gem.def (none): its length */
#define DEF_N	11
static size_t build_def(uint8_t *b, int zero)
{
	put16be(b, 0, DEF_N);
	put16be(b, 2, 0x0101);
	def_rec(b, 0, 0, 0, 0, 2, "ADMENU");
	def_rec(b, 1, 0, 3, 1, 0, "DESKMENU");		/* 8 bytes, no NUL */
	def_rec(b, 2, 0, 4, 1, 0, "FILEMENU");
	def_rec(b, 3, 7, 1, 0, 3, "ADDINFO");		/* [4] of a tree record ignored */
	def_rec(b, 4, 0, 2, 0, 0, "UNKTREE");		/* type 0, indicator 0: a tree */
	def_rec(b, 5, 0, 3, 0, 1, "APANEL");
	def_rec(b, 6, 0, 5, 0, 4, "STNOMORE");
	def_rec(b, 7, 0, 6, 0, 5, "FRSTR6");
	def_rec(b, 8, 0, 1, 0, 6, "ARROW");
	def_rec(b, 9, 1, 2, 7, 0, "ODDIND");		/* type 0, indicator 7: unknown */
	def_rec(b, 10, 1, 2, 0, 9, "TYPE9");		/* unknown type */
	if (!zero)
		return 16 * DEF_N;
	memset(b + 16 * DEF_N, 0, 16);
	return 16 * (DEF_N + 1);
}

static size_t hrd_rec(uint8_t *b, size_t p, int ty, unsigned t, unsigned o, const char *name)
{
	size_t	k = strlen(name);

	b[p] = (uint8_t)ty;
	b[p + 1] = 0;
	put16be(b, p + 2, t);
	put16be(b, p + 4, o);
	memcpy(b + p + 6, name, k + 1);
	return p + 6 + k + 1;
}

/* An .HRD like TeraDesk's, with prefix records, a 16 and a 20 character
 * name, an unknown type, and junk after the end: its length (*end: where
 * the end record ends) */
#define HRD_N	9
static size_t build_hrd(uint8_t *b, size_t *end)
{
	size_t	p;

	memset(b, 0, 8);
	put16be(b, 0, 1);
	put16be(b, 2, 1);
	b[4] = 1;
	p = hrd_rec(b, 8, 7, 0, 0, "PFX_");
	p = hrd_rec(b, p, 1, 0, 0, "MENU");
	p = hrd_rec(b, p, 5, 0, 3, "TDESK");
	p = hrd_rec(b, p, 0, 2, 0, "SIXTEENCHARSNAME");
	p = hrd_rec(b, p, 5, 2, 32766, "TWENTYCHARACTERNAMES");
	p = hrd_rec(b, p, 7, 0, 0, "");
	p = hrd_rec(b, p, 2, 4, 0, "ALNOWIN");
	p = hrd_rec(b, p, 3, 5, 0, "TFREE");
	p = hrd_rec(b, p, 4, 1, 0, "IMG\x80\xff");
	p = hrd_rec(b, p, 9, 3, 4, "WHAT");
	p = hrd_rec(b, p, 5, 300, 1, "Mixed_Case");
	*end = p = hrd_rec(b, p, 6, 0, 0, "");
	memcpy(b + p, "junk", 4);
	return p + 4;
}

/* ---- invariants ------------------------------------------------------------------ */

/* Every entry is well formed. */
static int sound(const struct rsc_names *n)
{
	struct rsc_name	e;
	int	i, ok = 1;
	size_t	k, len;

	for (i = 0; i < rsc_names_count(n); i++)
	{
		if (rsc_names_get(n, i, &e))
			return 0;
		len = strlen(e.name);
		ok &= len >= 1 && len <= RSC_NAME_MAX && e.order == i;
		for (k = 0; k < len; k++)
			ok &= (unsigned char)e.name[k] >= 0x20 && (unsigned char)e.name[k] != 0x7f;
		switch (e.kind)
		{
		case RSC_NAME_TREE:
			ok &= e.tkind >= RSC_TKIND_UNKN && e.tkind <= RSC_TKIND_DIAL && e.obj == 0
			      && e.tree >= 0 && e.tree <= RSC_NAMES_MAX_INDEX;
			break;
		case RSC_NAME_OBJ:
			ok &= e.tkind == RSC_TKIND_UNKN && e.tree >= 0 && e.tree <= RSC_NAMES_MAX_INDEX
			      && e.obj >= 0 && e.obj < RSC_MAX_OBJECTS;
			break;
		case RSC_NAME_FRSTR:
			ok &= (e.tkind == RSC_TKIND_UNKN || e.tkind == RSC_TKIND_ALRT) && e.tree == 0
			      && e.obj >= 0 && e.obj <= RSC_NAMES_MAX_INDEX;
			break;
		case RSC_NAME_FRIMG:
			ok &= e.tkind == RSC_TKIND_UNKN && e.tree == 0 && e.obj >= 0
			      && e.obj <= RSC_NAMES_MAX_INDEX;
			break;
		case RSC_NAME_UNKNOWN:
			ok &= e.tkind == RSC_TKIND_UNKN;
			break;
		default:
			ok = 0;
		}
	}
	return ok;
}

/* After rsc_names_check every entry names a node of r. */
static int in_range(const struct rsc_names *n, const struct rsc *r)
{
	struct rsc_name	e;
	int	i, nobj, ok = 1;

	for (i = 0; i < rsc_names_count(n); i++)
	{
		rsc_names_get(n, i, &e);
		if (e.kind == RSC_NAME_TREE)
			ok &= e.tree < rsc_ntrees(r);
		else if (e.kind == RSC_NAME_OBJ)
			ok &= rsc_tree_objects(r, e.tree, &nobj) && e.obj < nobj;
		else if (e.kind == RSC_NAME_FRSTR)
			ok &= e.obj < rsc_nfrstr(r);
		else if (e.kind == RSC_NAME_FRIMG)
			ok &= e.obj < rsc_nfrimg(r);
		else
			ok = 0;
	}
	return ok;
}


/* A read either succeeds (a sound list that the check against r leaves in
 * range, and that the guesser completes) or fails with a proper code and
 * position; nothing leaks.  Returns 1 for success. */
static int read_clean(int fmt, const uint8_t *b, size_t len, const struct rsc *r)
{
	struct rsc_error	err;
	struct rsc_names	*n;
	int	ok, dropped, before;

	memset(&err, 0x55, sizeof err);
	n = read_copy(fmt, b, len, &err);
	if (n)
	{
		before = rsc_names_count(n);
		ok = err.code == RSC_OK && sound(n);
		ok &= rsc_names_check(n, r, &dropped) == 0 && dropped >= 0
		      && rsc_names_count(n) == before - dropped && in_range(n, r) && sound(n);
		ok &= rsc_names_guess(n, r) >= 0 && sound(n) && in_range(n, r);
		rsc_names_free(n);
	}
	else
		ok = (err.code == RSC_ETRUNC || err.code == RSC_EHEADER || err.code == RSC_EOFFSET
		      || err.code == RSC_ESTRING || err.code == RSC_ETOOBIG)
		     && err.offset <= len && strstr(err.msg, " at offset 0x") != NULL;
	CHECK(ok);
	if (!ok)
		fprintf(stderr, "  fmt %d len %zu: code %d offset %zu %s\n", fmt, len, err.code,
			err.offset, err.msg);
	CHECK(live() == 0);
	return n != NULL;
}

/* The code and offset of a failed read (RSC_OK when it reads). */
static int code_of(int fmt, const uint8_t *b, size_t len, size_t *off)
{
	struct rsc_error	err;
	struct rsc_names	*n = read_copy(fmt, b, len, &err);

	*off = err.offset;
	if (n)
	{
		rsc_names_free(n);
		*off = 0;
		return RSC_OK;
	}
	return err.code;
}

/* ---- the RCS name files ---------------------------------------------------------- */

static int count_kind(const struct rsc_names *n, int kind, int tkind)
{
	struct rsc_name	e;
	int	i, c = 0;

	for (i = 0; i < rsc_names_count(n); i++)
	{
		rsc_names_get(n, i, &e);
		c += e.kind == kind && (tkind < 0 || e.tkind == tkind);
	}
	return c;
}

static void test_rcs_dfn(void)
{
	struct file	f = load("src/atari/rcs.dfn"), g = load("src/atari/rcslow.dfn");
	struct rsc	*low = load_rsc("src/atari/rcslow.rsc");
	struct rsc_error	err;
	struct rsc_names	*n = rsc_names_read_dfn(f.b, f.len, &err), *m;
	struct rsc_name	e;
	OBJECT	*o;
	const char	*s;
	int	dropped, nobj, i;

	CHECK(n != NULL && err.code == RSC_OK);
	CHECK(rsc_names_count(n) == 293 && sound(n));
	CHECK(count_kind(n, RSC_NAME_OBJ, -1) == 217);
	CHECK(count_kind(n, RSC_NAME_TREE, RSC_TKIND_DIAL) == 34);
	CHECK(count_kind(n, RSC_NAME_TREE, RSC_TKIND_MENU) == 2);
	CHECK(count_kind(n, RSC_NAME_TREE, -1) == 36);
	CHECK(count_kind(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT) == 18);
	CHECK(count_kind(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN) == 20);
	CHECK(count_kind(n, RSC_NAME_FRIMG, -1) == 2);
	CHECK(is(n, 18, RSC_NAME_TREE, RSC_TKIND_MENU, 0, 0, "MENUTREE"));
	CHECK(is(n, 0, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 21, "STNODEF"));
	s = rsc_frstr(rcs_rsc, 21);
	CHECK(s && strncmp(s, "[2][Can't find definition", 25) == 0);
	CHECK(is(n, 136, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 0, "STTITLE"));
	CHECK(is(n, 105, RSC_NAME_OBJ, RSC_TKIND_UNKN, 3, 6, "OKITEM"));
	o = rsc_tree_objects(rcs_rsc, 3, &nobj);
	CHECK(o && nobj > 6 && (o[6].ob_type & 0xff) == G_BUTTON);
	CHECK(o && strcmp(o[6].ob_spec.free_string, "   OK   ") == 0);
	CHECK(is(n, 89, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 10, "TRASHCAN"));
	o = rsc_tree_objects(rcs_rsc, 1, &nobj);
	CHECK(o && nobj > 10 && (o[10].ob_type & 0xff) == G_IMAGE);
	/* entry 52 has stale bytes after its NUL */
	CHECK(f.b[2 + 14 * 52 + 4 + 5] == '5');
	e = entry(n, 52);
	CHECK(strcmp(e.name, "GRID") == 0);
	CHECK(rsc_names_find_obj(n, 3, "OKITEM", nobj) == 6);
	CHECK(rsc_names_find_obj(n, 3, "okitem", 7) == 6);
	CHECK(rsc_names_find_obj(n, 3, "OKITEM", 6) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "menutree", 36) == 0);
	CHECK(rsc_names_find_str(n, "STNODEF", 39) == 21);
	CHECK(rsc_names_find_img(n, "clipfull", 2) == 1);
	CHECK_STR(rsc_names_tree_name(n, 1), "TOOLBOX");
	CHECK_STR(rsc_names_obj_name(n, 3, 6), "OKITEM");
	CHECK_STR(rsc_names_str_name(n, 0), "STTITLE");
	CHECK_STR(rsc_names_img_name(n, 0), "CLIPMPTY");
	CHECK(rsc_names_str_name(n, 5) == NULL);	/* the one unnamed free string */
	CHECK(rsc_names_tree_kind(n, 0, &i) == RSC_TKIND_MENU && i == 0);
	CHECK(rsc_names_check(n, rcs_rsc, &dropped) == 0 && dropped == 0);
	CHECK(rsc_names_count(n) == 293);
	/* everything named but free string 5, which is not an alert */
	CHECK(rsc_names_guess(n, rcs_rsc) == 0 && rsc_names_count(n) == 293);
	m = rsc_names_read_dfn(g.b, g.len, &err);
	CHECK(m && rsc_names_count(m) == 293);
	CHECK(rsc_names_check(m, low, &dropped) == 0 && dropped == 0);
	rsc_names_free(m);
	rsc_names_free(n);
	rsc_free(low);
	CHECK(live() == 0);
	free(f.b);
	free(g.b);
}

/* ---- the small samples ----------------------------------------------------------- */

static void test_dfn(void)
{
	uint8_t	b[2 + 14 * DFN_N + 4];
	size_t	len = build_dfn(b), off;
	struct rsc_error	err;
	struct rsc_names	*n = read_copy(DFN, b, len, &err);

	CHECK(n && rsc_names_count(n) == DFN_N && sound(n));
	CHECK(is(n, 0, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 21, "STNODEF"));
	CHECK(is(n, 1, RSC_NAME_TREE, RSC_TKIND_MENU, 0, 0, "MENUTREE"));
	CHECK(is(n, 2, RSC_NAME_TREE, RSC_TKIND_DIAL, 1, 0, "TOOLBOX"));
	CHECK(is(n, 3, RSC_NAME_TREE, RSC_TKIND_PANL, 2, 0, "PANEL"));
	CHECK(is(n, 4, RSC_NAME_TREE, RSC_TKIND_UNKN, 3, 0, "ANY"));
	CHECK(is(n, 5, RSC_NAME_OBJ, RSC_TKIND_UNKN, 3, 10, "OKITEM"));
	CHECK(is(n, 6, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 7, "STR7"));
	CHECK(is(n, 7, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 1, "CLIPFULL"));
	CHECK(is(n, 8, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, 4, 5, "FREE"));
	CHECK(is(n, 9, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 2, "NINECHAR"));
	rsc_names_free(n);
	/* the size rules */
	b[len] = 0;
	CHECK(code_of(DFN, b, len + 1, &off) == RSC_EHEADER && off == len);
	CHECK(code_of(DFN, b, len - 1, &off) == RSC_ETRUNC && off == len - 1);
	CHECK(code_of(DFN, b, 1, &off) == RSC_ETRUNC && off == 1);
	put16le(b, 0, 0);
	CHECK(code_of(DFN, b, 2, &off) == RSC_OK);
	put16le(b, 0, DFN_N - 1);
	CHECK(code_of(DFN, b, len, &off) == RSC_EHEADER && off == len - 14);
	CHECK(code_of(DFN, b, len - 14, &off) == RSC_OK);
	put16le(b, 0, DFN_N);
	/* names */
	b[2 + 14 * 3 + 4] = 0;
	CHECK(code_of(DFN, b, len, &off) == RSC_ESTRING && off == 2 + 14 * 3 + 4);
	b[2 + 14 * 3 + 4] = 0x1f;
	CHECK(code_of(DFN, b, len, &off) == RSC_ESTRING && off == 2 + 14 * 3 + 4);
	b[2 + 14 * 3 + 4] = 0x7f;
	CHECK(code_of(DFN, b, len, &off) == RSC_ESTRING);
	b[2 + 14 * 3 + 4] = 0x20;
	CHECK(code_of(DFN, b, len, &off) == RSC_OK);
	b[2 + 14 * 3 + 4] = 0xe4;
	CHECK(code_of(DFN, b, len, &off) == RSC_OK);
	b[2 + 14 * 3 + 6] = 0x01;	/* "PA\1EL" */
	CHECK(code_of(DFN, b, len, &off) == RSC_ESTRING && off == 2 + 14 * 3 + 4);
	CHECK(live() == 0);
}

static void test_def(void)
{
	uint8_t	b[16 * (DEF_N + 2)];
	size_t	len, off;
	struct rsc_error	err;
	struct rsc_names	*n;
	int	zero;

	for (zero = 0; zero < 2; zero++)
	{
		len = build_def(b, zero);
		n = read_copy(DEF, b, len, &err);
		CHECK(n && rsc_names_count(n) == DEF_N && sound(n));
		CHECK(is(n, 0, RSC_NAME_TREE, RSC_TKIND_MENU, 0, 0, "ADMENU"));
		CHECK(is(n, 1, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 3, "DESKMENU"));
		CHECK(is(n, 2, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 4, "FILEMENU"));
		CHECK(is(n, 3, RSC_NAME_TREE, RSC_TKIND_DIAL, 1, 0, "ADDINFO"));
		CHECK(is(n, 4, RSC_NAME_TREE, RSC_TKIND_UNKN, 2, 0, "UNKTREE"));
		CHECK(is(n, 5, RSC_NAME_TREE, RSC_TKIND_PANL, 3, 0, "APANEL"));
		CHECK(is(n, 6, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 5, "STNOMORE"));
		CHECK(is(n, 7, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 6, "FRSTR6"));
		CHECK(is(n, 8, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 1, "ARROW"));
		CHECK(is(n, 9, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, 0x102, 0x700, "ODDIND"));
		CHECK(is(n, 10, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, 0x102, 9, "TYPE9"));
		rsc_names_free(n);
		/* the count */
		put16be(b, 0, DEF_N + 1);
		CHECK(code_of(DEF, b, len, &off) == (zero ? RSC_ESTRING : RSC_ETRUNC));
		put16be(b, 0, DEF_N + 2);
		CHECK(code_of(DEF, b, len, &off) == RSC_ETRUNC && off == len);
		put16be(b, 0, DEF_N - 1);
		CHECK(code_of(DEF, b, len, &off) == RSC_EHEADER && off == 0);
		put16be(b, 0, DEF_N);
		CHECK(code_of(DEF, b, len - 1, &off) == RSC_ETRUNC && off == len - 16);
		CHECK(code_of(DEF, b, len, &off) == RSC_OK);
	}
	/* a non-zero last record beyond the count */
	b[len - 1] = 1;
	CHECK(code_of(DEF, b, len, &off) == RSC_EHEADER && off == 0);
	b[len - 1] = 0;
	/* the zero record dropped by a truncation: count = records */
	CHECK(code_of(DEF, b, len - 16, &off) == RSC_OK);
	CHECK(code_of(DEF, b, 15, &off) == RSC_ETRUNC && off == 15);
	CHECK(code_of(DEF, b, 0, &off) == RSC_ETRUNC && off == 0);
	/* an empty name */
	memset(b + 16 * 4 + 8, 0, 8);
	CHECK(code_of(DEF, b, len, &off) == RSC_ESTRING && off == 16 * 4 + 8);
	/* like EmuTOS' icon.def: count 3, a zero record; mform.def: record 0 a
	 * free image with flags 0x0200, no zero record */
	memset(b, 0, sizeof b);
	put16be(b, 0, 3);
	put16be(b, 2, 1);
	def_rec(b, 0, 0, 0, 0, 3, "BASIC");
	def_rec(b, 1, 0, 1, 0, 3, "OTHER");
	def_rec(b, 2, 1, 1, 1, 0, "ICON_BEG");
	n = read_copy(DEF, b, 64, &err);
	CHECK(n && rsc_names_count(n) == 3 && is(n, 2, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 1, "ICON_BEG"));
	rsc_names_free(n);
	memset(b, 0, sizeof b);
	put16be(b, 0, 2);
	put16be(b, 2, 0x0200);
	def_rec(b, 0, 0, 0, 0, 6, "ARROW");
	def_rec(b, 1, 0, 7, 0, 6, "OUTLN_CR");
	n = read_copy(DEF, b, 32, &err);
	CHECK(n && rsc_names_count(n) == 2 && is(n, 0, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 0, "ARROW"));
	CHECK(n && is(n, 1, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 7, "OUTLN_CR"));
	rsc_names_free(n);
	CHECK(live() == 0);
}

static void test_hrd(void)
{
	uint8_t	b[512];
	size_t	end, len = build_hrd(b, &end), off, p;
	struct rsc_error	err;
	struct rsc_names	*n = read_copy(HRD, b, len, &err);

	CHECK(n && rsc_names_count(n) == HRD_N && sound(n));
	CHECK(is(n, 0, RSC_NAME_TREE, RSC_TKIND_MENU, 0, 0, "MENU"));
	CHECK(is(n, 1, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 3, "TDESK"));
	CHECK(is(n, 2, RSC_NAME_TREE, RSC_TKIND_DIAL, 2, 0, "SIXTEENCHARSNAME"));
	CHECK(is(n, 3, RSC_NAME_OBJ, RSC_TKIND_UNKN, 2, 32766, "TWENTYCHARACTERN"));
	CHECK(is(n, 4, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 4, "ALNOWIN"));
	CHECK(is(n, 5, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 5, "TFREE"));
	CHECK(is(n, 6, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 1, "IMG\x80\xff"));
	CHECK(is(n, 7, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, 3, 9, "WHAT"));
	CHECK(is(n, 8, RSC_NAME_OBJ, RSC_TKIND_UNKN, 300, 1, "Mixed_Case"));
	rsc_names_free(n);
	/* the end record */
	CHECK(code_of(HRD, b, end, &off) == RSC_OK);
	CHECK(code_of(HRD, b, end - 1, &off) == RSC_ETRUNC && off == end - 1);	/* its name */
	CHECK(code_of(HRD, b, end - 7, &off) == RSC_ETRUNC && off == end - 7);
	b[end - 7] = 7;		/* a prefix: the junk is then a truncated entry */
	CHECK(code_of(HRD, b, len, &off) == RSC_ETRUNC && off == end);
	b[end - 7] = 8;		/* an unknown type with an empty name */
	CHECK(code_of(HRD, b, len, &off) == RSC_ESTRING && off == end - 1);
	b[end - 7] = 6;
	/* the version */
	put16be(b, 0, 2);
	CHECK(code_of(HRD, b, len, &off) == RSC_EHEADER && off == 0);
	put16be(b, 0, 0);
	CHECK(code_of(HRD, b, len, &off) == RSC_EHEADER);
	put16be(b, 0, 1);
	CHECK(code_of(HRD, b, 7, &off) == RSC_ETRUNC && off == 7);
	CHECK(code_of(HRD, b, 8, &off) == RSC_ETRUNC && off == 8);
	/* the object word of the tree, string and image records */
	p = 8 + 6 + 5;		/* the MENU record */
	CHECK(b[p] == 1);
	put16be(b, p + 4, 1);
	CHECK(code_of(HRD, b, len, &off) == RSC_EOFFSET && off == p + 4);
	put16be(b, p + 4, 0);
	p += 6 + 5;		/* TDESK */
	put16be(b, p + 4, 32767);
	CHECK(code_of(HRD, b, len, &off) == RSC_EOFFSET && off == p + 4);
	put16be(b, p + 4, 3);
	b[p + 6] = 0x7f;
	CHECK(code_of(HRD, b, len, &off) == RSC_ESTRING && off == p + 6);
	b[p + 6] = 'T';
	/* a bad character in the cut part of a long name */
	p += 6 + 6 + 6 + 17;	/* TWENTYCHARACTERNAMES */
	CHECK(b[p] == 5 && b[p + 6 + 18] == 'E');
	b[p + 6 + 18] = '\n';
	CHECK(code_of(HRD, b, len, &off) == RSC_ESTRING && off == p + 6);
	b[p + 6 + 18] = 'E';
	CHECK(code_of(HRD, b, len, &off) == RSC_OK);
	CHECK(live() == 0);
}

static void test_by_ext(void)
{
	uint8_t	d[2 + 14 * DFN_N], e[16 * (DEF_N + 1)], h[512];
	size_t	dl = build_dfn(d), el = build_def(e, 1), hend, hl = build_hrd(h, &hend);
	struct rsc_error	err;
	struct rsc_names	*n;
	static const char	*bad[] = { "x.rsc", "dfn", "x.dfnx", "x.df", "", "a.dfn/b", "x.DE" };
	size_t	k;

	n = rsc_names_read_by_ext("RCS.DFN", d, dl, &err);
	CHECK(n && rsc_names_count(n) == DFN_N);
	rsc_names_free(n);
	n = rsc_names_read_by_ext("dir.x/desktop.Def", e, el, &err);
	CHECK(n && rsc_names_count(n) == DEF_N);
	rsc_names_free(n);
	n = rsc_names_read_by_ext("a.rsd", e, el, &err);
	CHECK(n && rsc_names_count(n) == DEF_N);
	rsc_names_free(n);
	n = rsc_names_read_by_ext("fm_desktop.hRd", h, hl, &err);
	CHECK(n && rsc_names_count(n) == HRD_N);
	rsc_names_free(n);
	n = rsc_names_read_by_ext("rcs.dfn", h, hl, &err);	/* the wrong reader */
	CHECK(!n && err.code != RSC_OK);
	for (k = 0; k < sizeof bad / sizeof bad[0]; k++)
	{
		err.code = RSC_OK;
		CHECK(rsc_names_read_by_ext(bad[k], d, dl, &err) == NULL && err.code == RSC_EARG);
	}
	CHECK(rsc_names_read_by_ext(NULL, d, dl, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_names_read_dfn(NULL, 4, &err) == NULL && err.code == RSC_EARG);
	CHECK(rsc_names_read_dfn(NULL, 0, &err) == NULL && err.code == RSC_ETRUNC);
	rsc_names_free(rsc_names_read_dfn(d, dl, NULL));
	CHECK(rsc_names_read_hrd(d, dl, NULL) == NULL);
	CHECK(live() == 0);
}

/* ---- truncations and mutations ------------------------------------------------------- */

static void test_truncation(void)
{
	uint8_t	d[2 + 14 * DFN_N], e[16 * (DEF_N + 1)], h[512];
	size_t	dl = build_dfn(d), el, hend, hl = build_hrd(h, &hend), k;
	struct file	f = load("src/atari/rcs.dfn");
	int	all = 1, zero;

	for (k = 0; k <= dl; k++)
		all &= read_clean(DFN, d, k, rcs_rsc) == (k == dl);
	for (zero = 0; zero < 2; zero++)
	{
		el = build_def(e, zero);
		for (k = 0; k <= el; k++)
			all &= read_clean(DEF, e, k, rcs_rsc) == (k == el || (zero && k == el - 16));
	}
	for (k = 0; k <= hl; k++)
		all &= read_clean(HRD, h, k, rcs_rsc) == (k >= hend);
	for (k = 0; k <= f.len; k += 7)
		all &= read_clean(DFN, f.b, k, rcs_rsc) == (k == f.len);
	CHECK(all);
	free(f.b);
}

/* Each of the first span bytes of b set to each of a few values and with
 * each bit flipped. */
static long sweep(int fmt, uint8_t *b, size_t len, size_t span)
{
	static const uint8_t	val[] = { 0x00, 0xff, 0x01, 0x06, 0x07, 0x20, 0x7f, 0x80, '#', 'a' };
	size_t	k, v;
	long	ok = 0;
	uint8_t	old;
	int	bit;

	for (k = 0; k < span; k++)
	{
		old = b[k];
		for (v = 0; v < sizeof val; v++)
		{
			b[k] = val[v];
			ok += read_clean(fmt, b, len, rcs_rsc);
		}
		for (bit = 0; bit < 8; bit++)
		{
			b[k] = old ^ (uint8_t)(1 << bit);
			ok += read_clean(fmt, b, len, rcs_rsc);
		}
		b[k] = old;
	}
	return ok;
}

static uint32_t	seed = 12345;

static uint32_t rnd(void)
{
	seed = seed * 1103515245u + 12345u;
	return seed >> 8;
}

/* count random mutations of one to three bytes each of f; every draw of
 * the generator is a statement of its own */
static void mutate(int fmt, const struct file *f, int count, const char *what)
{
	uint8_t	*m = malloc(f->len);
	int	i, j, nm, ok = 0;
	uint32_t	a, pos, bit;

	for (i = 0; i < count; i++)
	{
		memcpy(m, f->b, f->len);
		a = rnd();
		nm = 1 + (int)(a % 3);
		for (j = 0; j < nm; j++)
		{
			pos = rnd() % (uint32_t)f->len;
			a = rnd();
			if (a & 1)
			{
				bit = rnd() % 8;
				m[pos] ^= (uint8_t)(1 << bit);
			}
			else
			{
				a = rnd();
				m[pos] = (uint8_t)a;
			}
		}
		ok += read_clean(fmt, m, f->len, rcs_rsc);
	}
	printf("  %s: %d of %d random mutations still read\n", what, ok, count);
	free(m);
}

static void test_mutations(void)
{
	uint8_t	d[2 + 14 * DFN_N], e[16 * (DEF_N + 1)], h[512];
	size_t	dl = build_dfn(d), el, hend, hl = build_hrd(h, &hend);
	struct file	f = load("src/atari/rcs.dfn"), x;
	long	ok;
	int	zero;

	ok = sweep(DFN, d, dl, dl);
	printf("  dfn sample: %ld of %zu single-byte mutations still read\n", ok, dl * 18);
	for (zero = 0; zero < 2; zero++)
	{
		el = build_def(e, zero);
		ok = sweep(DEF, e, el, el);
		printf("  def sample (%s zero record): %ld of %zu single-byte mutations still read\n",
		       zero ? "with" : "without", ok, el * 18);
	}
	ok = sweep(HRD, h, hl, hl);
	printf("  hrd sample: %ld of %zu single-byte mutations still read\n", ok, hl * 18);
	ok = sweep(DFN, f.b, f.len, 2 + 14 * 20);	/* the count and the first 20 records */
	printf("  rcs.dfn head: %ld of %d single-byte mutations still read\n", ok, (2 + 14 * 20) * 18);
	mutate(DFN, &f, 3000, "rcs.dfn");
	x.b = h;
	x.len = hl;
	mutate(HRD, &x, 3000, "hrd sample");
	x.b = e;
	x.len = el;
	mutate(DEF, &x, 3000, "def sample");
	free(f.b);
}

/* ---- the budget and the entry limit ---------------------------------------------- */

/* The charges are pinned with the sizes of a 64-bit host (the list 16
 * bytes, an entry 24); each allocation costs 4 more. */
static void test_budget(void)
{
	struct file	f = load("src/atari/rcs.dfn");
	uint8_t	h[512];
	size_t	hend, hl = build_hrd(h, &hend), want, off, names = 0, k;
	struct rsc_error	err;
	struct rsc_names	*n;
	long	extra;
	int	i;

	n = rsc_names_read_dfn(f.b, f.len, &err);
	for (i = 0; i < rsc_names_count(n); i++)
		names += strlen(entry(n, i).name) + 1 + 4;
	rsc_names_free(n);
	if (sizeof(void *) == 8)
	{
		want = 16 + 4 + 293 * 24 + 4 + names;
		CHECK(rsc_test_names_charged() == want);
		n = read_copy(HRD, h, hl, &err);
		rsc_names_free(n);
		/* names of HRD_N entries, two cut to 16 */
		CHECK(rsc_test_names_charged() == 16 + 4 + HRD_N * 24 + 4 + (5 + 6 + 17 + 17 + 8 + 6 + 6 + 5 + 11)
		      + HRD_N * 4);
		n = read_copy(DFN, (const uint8_t *)"\0\0", 2, &err);
		CHECK(n && rsc_names_count(n) == 0 && rsc_test_names_charged() == 20);
		rsc_names_free(n);
	}
	else
		printf("  exact charges skipped: measured on 64-bit hosts\n");
	/* the boundary, wherever the charges are */
	n = rsc_names_read_dfn(f.b, f.len, &err);
	rsc_names_free(n);
	want = rsc_test_names_charged();
	CHECK(want > 293 * 9 && want < 4 * f.len);
	extra = (long)want - 4 * (long)f.len;
	rsc_test_names_budget(extra);
	n = rsc_names_read_dfn(f.b, f.len, &err);
	CHECK(n != NULL);
	rsc_names_free(n);
	rsc_test_names_budget(extra - 1);
	n = rsc_names_read_dfn(f.b, f.len, &err);
	CHECK(!n && err.code == RSC_ETOOBIG && err.offset == 2 + 14 * 292);
	CHECK(strncmp(err.msg, "names exceed the size limit", 27) == 0);
	/* the list itself, then the array: at offset 0 */
	rsc_test_names_budget(-4 * (long)f.len);
	CHECK(code_of(DFN, f.b, f.len, &off) == RSC_ETOOBIG && off == 0);
	rsc_test_names_budget(-4 * (long)f.len + 40);
	CHECK(code_of(DFN, f.b, f.len, &off) == RSC_ETOOBIG && off == 0);
	/* every budget between the boundary and nothing fails cleanly */
	for (k = 0; k < want; k += 61)
	{
		rsc_test_names_budget((long)k - 4 * (long)f.len);
		CHECK(code_of(DFN, f.b, f.len, &off) == RSC_ETOOBIG);
		CHECK(live() == 0);
	}
	rsc_test_names_budget(RSC_NAMES_BUDGET_EXTRA);
	CHECK(code_of(DFN, f.b, f.len, &off) == RSC_OK);
	CHECK(live() == 0);
	free(f.b);
}

/* An .HRD of count one-character object names: the densest file. */
static size_t build_many(uint8_t *b, long count)
{
	size_t	p;
	long	i;

	memset(b, 0, 8);
	put16be(b, 0, 1);
	for (p = 8, i = 0; i < count; i++)
		p = hrd_rec(b, p, 5, (unsigned)(i & 0xff), 0, "A");
	return hrd_rec(b, p, 6, 0, 0, "");
}

static void test_limit(void)
{
	size_t	len, off;
	uint8_t	*b = malloc(8 + 8 * (size_t)(RSC_NAMES_MAX + 1) + 7);
	struct rsc_error	err;
	struct rsc_names	*n;

	len = build_many(b, RSC_NAMES_MAX);
	n = rsc_names_read_hrd(b, len, &err);
	CHECK(n && rsc_names_count(n) == RSC_NAMES_MAX);
	CHECK(rsc_test_names_charged() <= 4 * len + RSC_NAMES_BUDGET_EXTRA);
	/* the list is full */
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, 0, 0, "X", 0) == -1);
	CHECK(rsc_names_guess(n, rcs_rsc) == -1 && rsc_names_count(n) == RSC_NAMES_MAX);
	rsc_names_free(n);
	len = build_many(b, RSC_NAMES_MAX + 1);
	CHECK(code_of(HRD, b, len, &off) == RSC_ETOOBIG && off == 8 + 8 * (size_t)RSC_NAMES_MAX);
	free(b);
	CHECK(live() == 0);
}

/* ---- allocation failures ----------------------------------------------------------- */

/* Every allocation of reading b fails in turn: a clean RSC_ENOMEM each
 * time, until the read succeeds.  Returns the number of failures. */
static long fail_read(int fmt, const uint8_t *b, size_t len)
{
	struct rsc_error	err;
	struct rsc_names	*n;
	long	k;

	for (k = 0;; k++)
	{
		rsc_test_fail_alloc_after(k);
		n = read_copy(fmt, b, len, &err);
		if (n)
			break;
		CHECK(err.code == RSC_ENOMEM && err.offset == 0);
		CHECK(live() == 0);
		if (err.code != RSC_ENOMEM)
			break;
	}
	rsc_test_fail_alloc_after(-1);
	rsc_names_free(n);
	CHECK(live() == 0);
	return k;
}

/* the names of n on one line, for comparing */
static void dump(const struct rsc_names *n, char *buf, size_t size)
{
	struct rsc_name	e;
	size_t	at = 0;
	int	i;

	buf[0] = '\0';
	for (i = 0; i < rsc_names_count(n) && at < size; i++)
	{
		rsc_names_get(n, i, &e);
		at += (size_t)snprintf(buf + at, size - at, "%d.%d.%d.%d.%d.%s ", e.kind, e.tkind,
				       e.tree, e.obj, e.guessed, e.name);
	}
}

/* Every allocation of guessing on names read from dfn (none: an empty
 * list) fails in turn: -1 and the list unchanged each time, until it
 * succeeds with the result of a run without failures; that adds want
 * entries (-1: do not check). */
static long fail_guess(const uint8_t *dfn, size_t len, const struct rsc *r, int want)
{
	static char	before[40000], after[40000], ref[40000];
	struct rsc_names	*n;
	long	k, was;
	int	got;

	n = dfn ? rsc_names_read_dfn(dfn, len, NULL) : rsc_names_new();
	got = rsc_names_guess(n, r);
	CHECK(want < 0 || got == want);
	want = got;
	dump(n, ref, sizeof ref);
	rsc_names_free(n);
	for (k = 0;; k++)
	{
		n = dfn ? rsc_names_read_dfn(dfn, len, NULL) : rsc_names_new();
		dump(n, before, sizeof before);
		was = rsc_test_live_blocks();
		rsc_test_fail_alloc_after(k);
		got = rsc_names_guess(n, r);
		rsc_test_fail_alloc_after(-1);
		if (got >= 0)
			break;
		dump(n, after, sizeof after);
		CHECK(strcmp(before, after) == 0);
		/* an empty list may keep the entry array it grew */
		CHECK(rsc_test_live_blocks() == was
		      || (dfn == NULL && rsc_test_live_blocks() == was + 1));
		rsc_names_free(n);
		CHECK(live() == 0);
	}
	CHECK(got == want);
	dump(n, after, sizeof after);
	CHECK(strcmp(ref, after) == 0);
	rsc_names_free(n);
	CHECK(live() == 0);
	return k;
}

static void test_alloc_failures(void)
{
	struct file	f = load("src/atari/rcs.dfn");
	uint8_t	d[2 + 14 * DFN_N], e[16 * (DEF_N + 1)], h[512];
	size_t	dl = build_dfn(d), el = build_def(e, 1), hend, hl = build_hrd(h, &hend);
	struct rsc_names	*n;
	int	map[3] = { 0, -1, 1 }, dropped, i, ok;
	long	k;

	CHECK(fail_read(DFN, f.b, f.len) == 2 + 293);
	CHECK(fail_read(DFN, d, dl) == 2 + DFN_N);
	CHECK(fail_read(DEF, e, el) == 2 + DEF_N);
	CHECK(fail_read(HRD, h, hl) == 2 + HRD_N);
	CHECK(fail_read(DFN, (const uint8_t *)"\0\0", 2) == 1);
	/* the guesser: all of rcs.rsc, and only what rcs.dfn leaves (a part) */
	k = fail_guess(NULL, 0, rcs_rsc, 18 + 36);
	CHECK(k > 4);
	put16le(f.b, 0, 30);	/* the first 30 entries only: 18 alerts, 12 trees */
	k = fail_guess(f.b, 2 + 14 * 30, rcs_rsc, 36 - 12);
	CHECK(k > 4);
	/* add */
	n = rsc_names_new();
	for (i = 0, ok = 1; i < 40; i++)
	{
		char	name[8];

		snprintf(name, sizeof name, "N%d", i);
		for (k = 0;; k++)
		{
			rsc_test_fail_alloc_after(k);
			if (rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, i, name, 0) == 0)
				break;
			ok &= rsc_names_count(n) == i;
		}
		rsc_test_fail_alloc_after(-1);
		ok &= k == ((i & (i - 1)) == 0 && i >= 8 ? 2 : i == 0 ? 2 : 1);
	}
	CHECK(ok && rsc_names_count(n) == 40);
	/* check, renumber and tree_removed allocate nothing */
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_names_renumber(n, 1, map, 3) == 0 && rsc_names_count(n) == 2);
	CHECK(rsc_names_tree_removed(n, 0) == 0);
	CHECK(rsc_names_check(n, rcs_rsc, &dropped) == 0);
	CHECK(rsc_test_alloc_count() == 0);
	rsc_test_fail_alloc_after(-1);
	rsc_names_free(n);
	CHECK(live() == 0);
	free(f.b);
}

/* ---- the guesser ------------------------------------------------------------------------ */

/* "NAME NAME ..." of the entries from first on */
static void names_from(const struct rsc_names *n, int first, int count, char *buf, size_t size)
{
	size_t	at = 0;
	int	i;

	buf[0] = '\0';
	for (i = first; i < rsc_names_count(n) && i < first + count && at < size; i++)
		at += (size_t)snprintf(buf + at, size - at, "%s%s", i > first ? " " : "", entry(n, i).name);
}

static void test_guess_rcs(void)
{
	static const char	alerts[] = "ALERT21 ALERT22 ALERT23 ALERT24 ALERT25 ALERT26 ALERT27 "
		"ALERT28 ALERT29 ALERT30 ALERT31 ALERT32 ALERT33 ALERT34 ALERT35 ALERT36 ALERT37 ALERT38";
	static const char	trees[] = "MENU1 DIALOG2 PANEL3 DIALOG4 DIALOG5 PANEL6 DIALOG7 PANEL8 "
		"MENU9 DIALOG10 DIALOG11 DIALOG12 DIALOG13 PANEL14 PANEL15 DIALOG16 DIALOG17 DIALOG18 "
		"DIALOG19 DIALOG20 DIALOG21 DIALOG22 DIALOG23 DIALOG24 PANEL25 PANEL26 PANEL27 PANEL28 "
		"PANEL29 PANEL30 PANEL31 PANEL32 DIALOG33 PANEL34 PANEL35 DIALOG36";
	char	buf[1000];
	struct rsc_names	*n = rsc_names_new();
	struct rsc_name	e;
	int	g, t;

	CHECK(rsc_names_guess(n, rcs_rsc) == 18 + 36);
	names_from(n, 0, 18, buf, sizeof buf);
	CHECK(strcmp(buf, alerts) == 0);
	names_from(n, 18, 36, buf, sizeof buf);
	CHECK(strcmp(buf, trees) == 0);
	names_from(n, 18, 10, buf, sizeof buf);
	printf("  rcs.rsc guessed: %s ...\n", buf);
	e = entry(n, 0);
	CHECK(e.kind == RSC_NAME_FRSTR && e.tkind == RSC_TKIND_ALRT && e.obj == 21 && e.guessed);
	for (t = 0; t < 36; t++)
	{
		e = entry(n, 18 + t);
		CHECK(e.kind == RSC_NAME_TREE && e.tree == t && e.guessed);
		CHECK(rsc_names_tree_kind(n, t, &g) == e.tkind && g == 1);
	}
	CHECK(rsc_names_tree_kind(n, 0, NULL) == RSC_TKIND_MENU);
	CHECK(rsc_names_tree_kind(n, 1, NULL) == RSC_TKIND_DIAL);
	CHECK(rsc_names_tree_kind(n, 2, NULL) == RSC_TKIND_PANL);
	CHECK(rsc_names_tree_kind(n, 36, &g) == -1 && g == 0);
	/* a second time: nothing left */
	CHECK(rsc_names_guess(n, rcs_rsc) == 0 && rsc_names_count(n) == 54);
	rsc_names_free(n);
	CHECK(live() == 0);
}

/* A model like a desktop resource: trees of each kind, alert-shaped and
 * other free strings. */
static struct rsc *build_desk(void)
{
	struct rsc	*r = rsc_new();
	BITBLK	bb;
	OBJECT	*o;
	int	t, a, b, c;

	/* 0: a menu: root, bar, titles, a G_TITLE */
	t = rsc_tree_add(r, 1);
	a = rsc_obj_add(r, t, 0, G_BOX);
	b = rsc_obj_add(r, t, a, G_IBOX);
	rsc_obj_add(r, t, b, G_TITLE);
	rsc_obj_add(r, t, 0, G_IBOX);
	/* 1: a dialog: an exit button */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_BOX);
	a = rsc_obj_add(r, t, 0, G_BUTTON);
	rsc_obj_set_flags(r, t, a, OF_SELECTABLE | OF_EXIT);
	/* 2: a panel */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_BOX);
	rsc_obj_add(r, t, 0, G_STRING);
	/* 3: unknown: a G_STRING root */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_STRING);
	/* 4: an editable field two levels down */
	t = rsc_tree_add(r, 1);
	a = rsc_obj_add(r, t, 0, G_BOX);
	c = rsc_obj_add(r, t, a, G_FTEXT);
	rsc_obj_set_flags(r, t, c, OF_EDITABLE);
	/* 5: a touchexit root, not a box */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_TEXT);
	rsc_obj_set_flags(r, t, 0, OF_TOUCHEXIT);
	/* 6: a G_BOXTEXT root with a default button that the root does not
	 * reach (unlinked) */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_BOXTEXT);
	a = rsc_obj_add(r, t, 0, G_BUTTON);
	rsc_obj_set_flags(r, t, a, OF_DEFAULT);
	o = rsc_tree_objects(r, t, NULL);
	o[0].ob_head = o[0].ob_tail = -1;
	o[a].ob_next = -1;
	/* 7: menu-like, but the third level is a G_STRING: a G_IBOX root */
	t = rsc_tree_add(r, 1);
	a = rsc_obj_add(r, t, 0, G_BOX);
	b = rsc_obj_add(r, t, a, G_IBOX);
	rsc_obj_add(r, t, b, G_STRING);
	/* 8: broken links: judged by the root, a G_BUTTON with OF_EXIT */
	t = rsc_tree_add(r, 2);
	rsc_obj_set_type(r, t, 0, G_BUTTON);
	rsc_obj_set_flags(r, t, 0, OF_EXIT);
	o = rsc_tree_objects(r, t, NULL);
	o[0].ob_head = 1;
	o[0].ob_tail = 7;
	/* 9: broken links again, a G_STRING root without flags; the head of
	 * the root points past the array */
	t = rsc_tree_add(r, 1);
	rsc_obj_set_type(r, t, 0, G_STRING);
	o = rsc_tree_objects(r, t, NULL);
	o[0].ob_head = 5;
	o[0].ob_tail = 5;
	rsc_frstr_add(r, "[1][Alert one][OK]");
	rsc_frstr_add(r, "plain");
	rsc_frstr_add(r, "[3][two][OK]");
	rsc_frstr_add(r, "[");
	rsc_frstr_add(r, "[x");
	rsc_frstr_add(r, "[2]");
	rsc_frstr_add(r, "");
	memset(&bb, 0, sizeof bb);
	bb.bi_wb = 2;
	bb.bi_hl = 2;
	rsc_frimg_add(r, &bb);
	return r;
}

static void test_guess_desk(void)
{
	struct rsc	*r = build_desk();
	struct rsc_names	*n = rsc_names_new();
	char	buf[400];
	int	g;

	CHECK(rsc_ntrees(r) == 10 && rsc_nfrstr(r) == 7);
	CHECK(rsc_names_guess(n, r) == 3 + 10);
	names_from(n, 0, 13, buf, sizeof buf);
	CHECK(strcmp(buf, "ALERT0 ALERT2 ALERT5 MENU1 DIALOG2 PANEL3 TREE4 DIALOG5 DIALOG6 PANEL7 "
		      "PANEL8 DIALOG9 TREE10") == 0);
	if (strcmp(buf, "ALERT0 ALERT2 ALERT5 MENU1 DIALOG2 PANEL3 TREE4 DIALOG5 DIALOG6 PANEL7 "
		   "PANEL8 DIALOG9 TREE10"))
		printf("  desk: %s\n", buf);
	rsc_names_free(n);
	/* named things stay: tree 5 named DIALOG2 (no kind change: PANL),
	 * an object dialog3 (lower case), a free image ALERT2, string 0 named,
	 * an out-of-range tree 40 named "panel3", TREE10 taken, TREE011 (a
	 * leading zero) is another name than TREE11 */
	n = rsc_names_new();
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_PANL, 5, 0, "DIALOG2", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 1, "dialog3", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 0, "ALERT2", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 0, "FIRST", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, 40, 0, "panel3", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 1, "TREE10", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 3, "TREE011", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 4, "Menu1x", 0) == 0);
	CHECK(rsc_names_guess(n, r) == 2 + 9);
	names_from(n, 8, 11, buf, sizeof buf);
	CHECK(strcmp(buf, "ALERT3 ALERT5 MENU1 DIALOG4 PANEL4 TREE4 DIALOG5 PANEL7 PANEL8 "
		      "DIALOG9 TREE11") == 0);
	if (strcmp(buf, "ALERT3 ALERT5 MENU1 DIALOG4 PANEL4 TREE4 DIALOG5 PANEL7 PANEL8 DIALOG9 TREE11"))
		printf("  desk with names: %s\n", buf);
	CHECK(rsc_names_tree_kind(n, 5, &g) == RSC_TKIND_PANL && g == 0);
	CHECK(rsc_names_tree_kind(n, 4, &g) == RSC_TKIND_DIAL && g == 1);
	CHECK(entry(n, 7).guessed == 0 && entry(n, 8).guessed == 1);
	CHECK_STR(rsc_names_str_name(n, 0), "FIRST");
	CHECK(rsc_names_img_name(n, 0) != NULL);
	rsc_names_free(n);
	CHECK(rsc_names_guess(NULL, r) == -1 && rsc_names_guess(n = rsc_names_new(), NULL) == -1);
	rsc_names_free(n);
	rsc_free(r);
	/* an empty resource */
	r = rsc_new();
	n = rsc_names_new();
	CHECK(rsc_names_guess(n, r) == 0 && rsc_names_count(n) == 0);
	rsc_names_free(n);
	rsc_free(r);
	CHECK(live() == 0);
}

/* ---- lookup ---------------------------------------------------------------------------- */

static void test_lookup(void)
{
	struct rsc_names	*n = rsc_names_new();
	static const struct
	{
		int	kind, tree, obj;
		const char	*name;
	} add[] =
	{
		{ RSC_NAME_TREE, 0, 0, "Alpha" },
		{ RSC_NAME_TREE, 1, 0, "ALPHA" },
		{ RSC_NAME_TREE, 2, 0, "Beta" },
		{ RSC_NAME_TREE, 2, 0, "BETA" },
		{ RSC_NAME_TREE, 2, 0, "Second" },
		{ RSC_NAME_TREE, 3, 0, "Gamma" },
		{ RSC_NAME_TREE, 4, 0, "Gamma" },
		{ RSC_NAME_TREE, 5, 0, "#3" },
		{ RSC_NAME_TREE, 6, 0, "#" },
		{ RSC_NAME_TREE, 7, 0, "#x" },
		{ RSC_NAME_TREE, 8, 0, "#12x" },
		{ RSC_NAME_TREE, 20, 0, "FAR" },
		{ RSC_NAME_TREE, 9, 0, "delta" },
		{ RSC_NAME_TREE, 9, 0, "DELTA" },
		{ RSC_NAME_OBJ, 0, 3, "OK" },
		{ RSC_NAME_OBJ, 1, 4, "OK" },
		{ RSC_NAME_OBJ, 1, 5, "ok" },
		{ RSC_NAME_FRSTR, 0, 0, "alpha" },
		{ RSC_NAME_FRSTR, 0, 2, "S\xe4" },
		{ RSC_NAME_FRSTR, 0, 3, "s\xc4" },
		{ RSC_NAME_FRIMG, 0, 1, "Gamma" },
	};
	size_t	k;

	for (k = 0; k < sizeof add / sizeof add[0]; k++)
		CHECK(rsc_names_add(n, add[k].kind, RSC_TKIND_UNKN, add[k].tree, add[k].obj, add[k].name, 0) == 0);
	/* exact first, then without case */
	CHECK(rsc_names_find_tree(n, "Alpha", 10) == 0);
	CHECK(rsc_names_find_tree(n, "ALPHA", 10) == 1);
	CHECK(rsc_names_find_tree(n, "alpha", 10) == RSC_NAMES_AMBIGUOUS);
	CHECK(rsc_names_find_tree(n, "aLPHA", 1) == 0);		/* tree 1 out of range */
	CHECK(rsc_names_find_tree(n, "beta", 10) == 2);		/* two entries, one node */
	CHECK(rsc_names_find_tree(n, "Beta", 10) == 2);
	CHECK(rsc_names_find_tree(n, "Gamma", 10) == RSC_NAMES_AMBIGUOUS);
	CHECK(rsc_names_find_tree(n, "GAMMA", 10) == RSC_NAMES_AMBIGUOUS);
	CHECK(rsc_names_find_tree(n, "Gamma", 4) == 3);
	CHECK(rsc_names_find_tree(n, "dElTa", 10) == 9);
	CHECK(rsc_names_find_tree(n, "Delta", 10) == 9);
	CHECK(rsc_names_find_tree(n, "Gamm", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "Gammaa", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, NULL, 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "FAR", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "FAR", 21) == 20);
	CHECK(rsc_names_find_tree(n, "OK", 10) == RSC_NAMES_NOTFOUND);	/* objects: another scope */
	/* #N */
	CHECK(rsc_names_find_tree(n, "#3", 10) == 3);
	CHECK(rsc_names_find_tree(n, "#0003", 10) == 3);
	CHECK(rsc_names_find_tree(n, "#0", 10) == 0);
	CHECK(rsc_names_find_tree(n, "#9", 10) == 9);
	CHECK(rsc_names_find_tree(n, "#10", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#3", 3) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#99999999999999999999", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#2147483647", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#2147483646", 2147483647) == 2147483646);
	CHECK(rsc_names_find_tree(n, "#21474836470", 2147483647) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#000000000000000000000007", 10) == 7);
	CHECK(rsc_names_find_tree(n, "#", 10) == 6);
	CHECK(rsc_names_find_tree(n, "#x", 10) == 7);
	CHECK(rsc_names_find_tree(n, "#X", 10) == 7);
	CHECK(rsc_names_find_tree(n, "#12x", 10) == 8);
	CHECK(rsc_names_find_tree(n, "#-1", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "# 1", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(n, "#3", 0) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_tree(NULL, "#2", 5) == 2);
	CHECK(rsc_names_find_tree(NULL, "Alpha", 5) == RSC_NAMES_NOTFOUND);
	/* objects, in their tree only */
	CHECK(rsc_names_find_obj(n, 0, "OK", 10) == 3);
	CHECK(rsc_names_find_obj(n, 0, "ok", 10) == 3);
	CHECK(rsc_names_find_obj(n, 1, "OK", 10) == 4);
	CHECK(rsc_names_find_obj(n, 1, "ok", 10) == 5);
	CHECK(rsc_names_find_obj(n, 1, "Ok", 10) == RSC_NAMES_AMBIGUOUS);
	CHECK(rsc_names_find_obj(n, 1, "Ok", 5) == 4);
	CHECK(rsc_names_find_obj(n, 2, "OK", 10) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_obj(n, 2, "#4", 10) == 4);
	CHECK(rsc_names_find_obj(n, 0, "Alpha", 10) == RSC_NAMES_NOTFOUND);
	/* strings and images */
	CHECK(rsc_names_find_str(n, "ALPHA", 5) == 0);
	CHECK(rsc_names_find_str(n, "S\xe4", 5) == 2);
	CHECK(rsc_names_find_str(n, "s\xe4", 5) == 2);	/* only ASCII folds */
	CHECK(rsc_names_find_str(n, "S\xc4", 5) == 3);
	CHECK(rsc_names_find_str(n, "#4", 5) == 4);
	CHECK(rsc_names_find_img(n, "gamma", 2) == 1);
	CHECK(rsc_names_find_img(n, "gamma", 1) == RSC_NAMES_NOTFOUND);
	CHECK(rsc_names_find_img(n, "Alpha", 2) == RSC_NAMES_NOTFOUND);
	/* display names: the first entry */
	CHECK_STR(rsc_names_tree_name(n, 2), "Beta");
	CHECK_STR(rsc_names_tree_name(n, 9), "delta");
	CHECK(rsc_names_tree_name(n, 10) == NULL && rsc_names_tree_name(n, -1) == NULL);
	CHECK(rsc_names_tree_name(NULL, 0) == NULL);
	CHECK_STR(rsc_names_obj_name(n, 1, 5), "ok");
	CHECK(rsc_names_obj_name(n, 2, 5) == NULL);
	CHECK_STR(rsc_names_str_name(n, 3), "s\xc4");
	CHECK_STR(rsc_names_img_name(n, 1), "Gamma");
	CHECK(rsc_names_img_name(n, 0) == NULL);
	rsc_names_free(n);
	CHECK(live() == 0);
}

/* ---- add, check, renumber ------------------------------------------------------------- */

static void test_add(void)
{
	struct rsc_names	*n = rsc_names_new();
	struct rsc_name	e;
	char	long_name[RSC_NAME_MAX + 2];

	memset(long_name, 'L', sizeof long_name - 1);
	long_name[RSC_NAME_MAX + 1] = '\0';
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, long_name, 0) == -1);
	long_name[RSC_NAME_MAX] = '\0';
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, long_name, 1) == 0);
	e = entry(n, 0);
	CHECK(strlen(e.name) == RSC_NAME_MAX && e.guessed == 1 && e.order == 0);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, NULL, 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "A\tB", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "A\x7f", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "A B/\x80", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_ALRT, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, 0, 1, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, -1, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, RSC_NAMES_MAX_INDEX + 1, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_UNKN, RSC_NAMES_MAX_INDEX, 0, "A", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_TREE, -1, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_DIAL, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, RSC_MAX_OBJECTS, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, RSC_MAX_OBJECTS - 1, "A", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, -1, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 3, "A", 0) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_MENU, 0, 3, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 1, 3, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, RSC_NAMES_MAX_INDEX + 1, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_ALRT, 0, 3, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 1, 3, "A", 0) == -1);
	CHECK(rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 3, "A", 7) == 0);
	CHECK(rsc_names_add(n, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_add(n, 9, RSC_TKIND_UNKN, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_add(NULL, RSC_NAME_TREE, RSC_TKIND_UNKN, 0, 0, "A", 0) == -1);
	CHECK(rsc_names_count(n) == 6 && sound(n) && entry(n, 5).guessed == 1);
	CHECK(rsc_names_get(n, 6, &e) == -1 && rsc_names_get(n, -1, &e) == -1);
	CHECK(rsc_names_get(n, 0, NULL) == -1 && rsc_names_get(NULL, 0, &e) == -1);
	CHECK(rsc_names_count(NULL) == 0);
	rsc_names_free(n);
	rsc_names_free(NULL);
	CHECK(live() == 0);
}

static void test_check(void)
{
	struct rsc	*r = build_desk();
	struct rsc_names	*n = rsc_names_new();
	char	buf[400];
	int	dropped = -1;

	rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_MENU, 0, 0, "T0", 0);
	rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_MENU, 10, 0, "T10", 0);
	rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_MENU, 9, 0, "T9", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 4, "O04", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 5, "O05", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 10, 0, "O100", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 8, 1, "O81", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 8, 2, "O82", 0);
	rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 6, "S6", 0);
	rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 7, "S7", 0);
	rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 0, "I0", 0);
	rsc_names_add(n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 1, "I1", 0);
	CHECK(rsc_names_check(n, r, &dropped) == 0 && dropped == 6);
	names_from(n, 0, 20, buf, sizeof buf);
	CHECK(strcmp(buf, "T0 T9 O04 O81 S6 I0") == 0);
	CHECK(rsc_names_check(n, r, NULL) == 0 && rsc_names_count(n) == 6);
	CHECK(rsc_names_check(n, NULL, &dropped) == -1 && dropped == 0);
	CHECK(rsc_names_check(NULL, r, NULL) == -1);
	rsc_names_free(n);
	rsc_free(r);
	CHECK(live() == 0);
}

static void test_renumber(void)
{
	struct rsc_names	*n = rsc_names_new();
	int	map[6] = { 0, -1, 1, -1, 2, 3 }, bad[2] = { 0, -2 }, big[1] = { RSC_MAX_OBJECTS };
	char	buf[400];

	rsc_names_add(n, RSC_NAME_TREE, RSC_TKIND_DIAL, 1, 0, "T1", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 0, "O0", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 1, "O1", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 0, 1, "X1", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 2, "O2", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 3, "O3", 0);
	rsc_names_add(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 1, "S1", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 5, "O5", 0);
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 1, 6, "O6", 0);	/* beyond the map */
	rsc_names_add(n, RSC_NAME_OBJ, RSC_TKIND_UNKN, 2, 5, "Y5", 0);
	CHECK(rsc_names_renumber(n, 1, bad, 2) == -1 && rsc_names_count(n) == 10);
	CHECK(rsc_names_renumber(n, 1, big, 1) == -1 && rsc_names_count(n) == 10);
	CHECK(rsc_names_renumber(n, 1, NULL, 0) == -1 && rsc_names_renumber(NULL, 1, map, 6) == -1);
	CHECK(rsc_names_renumber(n, 1, map, -1) == -1);
	CHECK(rsc_names_renumber(n, 1, map, 6) == 0);
	names_from(n, 0, 20, buf, sizeof buf);
	CHECK(strcmp(buf, "T1 O0 X1 O2 S1 O5 Y5") == 0);
	CHECK(rsc_names_obj_name(n, 1, 1) && strcmp(rsc_names_obj_name(n, 1, 1), "O2") == 0);
	CHECK(rsc_names_obj_name(n, 1, 3) && strcmp(rsc_names_obj_name(n, 1, 3), "O5") == 0);
	CHECK(rsc_names_obj_name(n, 2, 5) && strcmp(rsc_names_obj_name(n, 2, 5), "Y5") == 0);
	CHECK(rsc_names_obj_name(n, 0, 1) && strcmp(rsc_names_obj_name(n, 0, 1), "X1") == 0);
	CHECK(entry(n, 3).obj == 1 && entry(n, 5).obj == 3 && sound(n));
	/* the tree helpers */
	CHECK(rsc_names_tree_removed(n, 1) == 0);
	names_from(n, 0, 20, buf, sizeof buf);
	CHECK(strcmp(buf, "X1 S1 Y5") == 0);
	CHECK(entry(n, 2).tree == 1 && entry(n, 0).tree == 0 && entry(n, 1).obj == 1);
	CHECK(rsc_names_tree_removed(n, -1) == -1 && rsc_names_tree_removed(NULL, 0) == -1);
	rsc_names_free(n);
	CHECK(live() == 0);
}

/* ---- real files, when RSC_TEST_EXTERNAL names directories ------------------------------- */

static int has_ext(const char *name, const char *ext)
{
	size_t	n = strlen(name), k;

	if (n < 5 || name[n - 4] != '.')
		return 0;
	for (k = 0; k < 3; k++)
		if ((name[n - 3 + k] | 0x20) != ext[k])
			return 0;
	return 1;
}

static void external_pair(const char *dir, const char *name)
{
	char	path[1024], rpath[1024];
	struct file	f, g;
	struct rsc_error	err;
	struct rsc_names	*n;
	struct rsc	*r;
	size_t	base = strlen(name) - 4;
	int	dropped = -1, before, guessed;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	snprintf(rpath, sizeof rpath, "%s/%.*s.rsc", dir, (int)base, name);
	g = load_path(rpath);
	if (!g.b)
	{
		snprintf(rpath, sizeof rpath, "%s/%.*s.RSC", dir, (int)base, name);
		g = load_path(rpath);
	}
	f = load_path(path);
	CHECK(f.b != NULL);
	if (!f.b)
		return;
	n = rsc_names_read_by_ext(name, f.b, f.len, &err);
	CHECK(n != NULL);
	if (!n)
		printf("  %s: %s (code %d)\n", path, err.msg, err.code);
	else if (!g.b)
		printf("  %s: %d entries, no resource\n", path, rsc_names_count(n));
	else
	{
		r = rsc_atari_read(g.b, g.len, 0, 0, &err);
		CHECK(r != NULL);
		if (r)
		{
			before = rsc_names_count(n);
			printf("  %s: %d entries (%d tree, %d object, %d alert, %d free string, %d free image,"
			       " %d unknown)", path, before, count_kind(n, RSC_NAME_TREE, -1),
			       count_kind(n, RSC_NAME_OBJ, -1), count_kind(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT),
			       count_kind(n, RSC_NAME_FRSTR, RSC_TKIND_UNKN), count_kind(n, RSC_NAME_FRIMG, -1),
			       count_kind(n, RSC_NAME_UNKNOWN, -1));
			CHECK(rsc_names_check(n, r, &dropped) == 0);
			CHECK(dropped == 0);
			guessed = rsc_names_guess(n, r);
			CHECK(guessed >= 0 && sound(n) && in_range(n, r));
			printf(", %d dropped, %d guessed (resource: %d trees, %d free strings, %d free images)\n",
			       dropped, guessed, rsc_ntrees(r), rsc_nfrstr(r), rsc_nfrimg(r));
			rsc_free(r);
		}
		else
			printf("  %s: %s\n", rpath, err.msg);
	}
	rsc_names_free(n);
	free(f.b);
	free(g.b);
	CHECK(live() == 0);
}

static void test_external(void)
{
	const char	*env = getenv("RSC_TEST_EXTERNAL");
	char	dir[1024];
	const char	*p, *q;
	struct dirent	*de;
	DIR	*d;

	if (!env || !*env)
		return;
	for (p = env; *p; p = *q ? q + 1 : q)
	{
		q = strchr(p, ':');
		if (!q)
			q = p + strlen(p);
		snprintf(dir, sizeof dir, "%.*s", (int)(q - p), p);
		d = opendir(dir);
		CHECK(d != NULL);
		if (!d)
			continue;
		while ((de = readdir(d)) != NULL)
			if (has_ext(de->d_name, "dfn") || has_ext(de->d_name, "def")
			    || has_ext(de->d_name, "rsd") || has_ext(de->d_name, "hrd"))
				external_pair(dir, de->d_name);
		closedir(d);
	}
}

int main(void)
{
	rcs_rsc = load_rsc("src/atari/rcs.rsc");
	base_live = rsc_test_live_blocks();
	test_rcs_dfn();
	test_dfn();
	test_def();
	test_hrd();
	test_by_ext();
	test_add();
	test_lookup();
	test_check();
	test_renumber();
	test_guess_rcs();
	test_guess_desk();
	test_budget();
	test_limit();
	test_truncation();
	test_mutations();
	test_alloc_failures();
	test_external();
	rsc_free(rcs_rsc);
	CHECK(rsc_test_live_blocks() == 0);
	printf("rsc-test (names): %ld checks, %ld failed\n", checks, failures);
	return failures ? 1 : 0;
}
