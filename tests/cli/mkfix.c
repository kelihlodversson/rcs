/* MKFIX.C	the synthetic fixtures of the rcs-cli tests (tests/cli/run.sh)
 *
 *	mkfix all DIR			write every fixture into DIR (which must exist)
 *	mkfix flip IN OUT OFFSET XOR	copy IN to OUT with the byte at OFFSET xor'ed
 *
 * The resource files are built with the editing API of the library and
 * written with rsc_atari_write, then patched where a file must have what
 * the writer never writes (an object no root reaches).  The name files are
 * written byte by byte in the .DFN, .DEF and .HRD formats
 * (docs/resource-model.md, "Names").  What each file holds is listed in
 * docs/rcs-cli.md, "Tests", and below.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "rscmodel.h"
#include "rscfile.h"
#include "rscnames.h"

static const char *dir;

static void die(const char *what)
{
	fprintf(stderr, "mkfix: %s\n", what);
	exit(1);
}

static void check(int ok, const char *what)
{
	if (!ok)
		die(what);
}

static char *path(const char *name)
{
	static char	buf[4096];

	snprintf(buf, sizeof buf, "%s/%s", dir, name);
	return buf;
}

static void put_file(const char *name, const void *buf, size_t len)
{
	FILE	*f = fopen(path(name), "wb");

	check(f != NULL, name);
	check(fwrite(buf, 1, len, f) == len, name);
	check(fclose(f) == 0, name);
}

static void sub_dir(const char *name)
{
	mkdir(path(name), 0777);
}

/* Writes r to name (file form) and frees it; with patch, patch(buf, len)
 * first. */
static void write_rsc(struct rsc *r, const char *name, void (*patch)(uint8_t *, size_t))
{
	struct rsc_error	err;
	uint8_t			*buf;
	size_t			len;

	if (rsc_atari_write(r, 0, 0, &buf, &len, &err) != 0)
	{
		fprintf(stderr, "mkfix: %s: %s\n", name, err.msg);
		exit(1);
	}
	if (patch)
		patch(buf, len);
	put_file(name, buf, len);
	rsc_mfree(buf);
	rsc_free(r);
}

/* ---- name files ------------------------------------------------------------ */

struct ent
{
	int		kind;	/* enum rsc_name_kind */
	int		tkind;
	int		tree;
	int		obj;	/* OBJ: object; FRSTR, FRIMG: index */
	const char	*name;
};

static size_t ents_len(const struct ent *e)
{
	size_t	n = 0;

	while (e[n].name)
		n++;
	return n;
}

/* .DFN: entries that do not fit (an object or tree above 255) are left out. */
static void write_dfn(const char *name, const struct ent *e)
{
	uint8_t	buf[2 + 14 * 512];
	size_t	n = ents_len(e), i, k = 0, at;
	int	value, kind;

	memset(buf, 0, sizeof buf);
	for (i = 0; i < n; i++)
	{
		switch (e[i].kind)
		{
		case RSC_NAME_TREE:
			value = e[i].tree;
			kind = e[i].tkind;
			break;
		case RSC_NAME_OBJ:
			if (e[i].tree > 255 || e[i].obj > 255)
				continue;
			value = (e[i].tree << 8) | e[i].obj;
			kind = 0x100;
			break;
		case RSC_NAME_FRSTR:
			value = e[i].obj;
			kind = e[i].tkind == RSC_TKIND_ALRT ? 4 : 0x101;
			break;
		default:
			value = e[i].obj;
			kind = 0x102;
			break;
		}
		if (strlen(e[i].name) > 8)
			continue;
		at = 2 + 14 * k;
		buf[at] = (uint8_t)(value & 0xff);
		buf[at + 1] = (uint8_t)(value >> 8);
		buf[at + 2] = (uint8_t)(kind & 0xff);
		buf[at + 3] = (uint8_t)(kind >> 8);
		memcpy(buf + at + 4, e[i].name, strlen(e[i].name));
		k++;
	}
	buf[0] = (uint8_t)(k & 0xff);
	buf[1] = (uint8_t)(k >> 8);
	put_file(name, buf, 2 + 14 * k);
}

/* .DEF/.RSD with the trailing zero record. */
static void write_def(const char *name, const struct ent *e)
{
	uint8_t	buf[16 * 512];
	size_t	n = ents_len(e), i, k = 0, at;

	memset(buf, 0, sizeof buf);
	for (i = 0; i < n; i++)
	{
		if (strlen(e[i].name) > 8 || e[i].tree > 255 || e[i].obj > 255)
			continue;
		at = 16 * k;
		switch (e[i].kind)
		{
		case RSC_NAME_TREE:
			buf[at + 5] = (uint8_t)e[i].tree;
			buf[at + 7] = (uint8_t)e[i].tkind;
			break;
		case RSC_NAME_OBJ:
			buf[at + 4] = (uint8_t)e[i].tree;
			buf[at + 5] = (uint8_t)e[i].obj;
			buf[at + 6] = 1;
			break;
		case RSC_NAME_FRSTR:
			buf[at + 5] = (uint8_t)e[i].obj;
			buf[at + 7] = e[i].tkind == RSC_TKIND_ALRT ? 4 : 5;
			break;
		default:
			buf[at + 5] = (uint8_t)e[i].obj;
			buf[at + 7] = 6;
			break;
		}
		memcpy(buf + at + 8, e[i].name, strlen(e[i].name));
		k++;
	}
	buf[0] = (uint8_t)(k >> 8);
	buf[1] = (uint8_t)(k & 0xff);
	put_file(name, buf, 16 * (k + 1));
}

/* .HRD: trees of kind PANL and UNKN are written as DIAL (the format has no
 * others). */
static void write_hrd(const char *name, const struct ent *e)
{
	uint8_t	buf[32 * 512];
	size_t	n = ents_len(e), i, k = 8, len;
	int	type, a, b;

	memset(buf, 0, sizeof buf);
	buf[1] = 1;
	for (i = 0; i < n; i++)
	{
		a = e[i].tree;
		b = 0;
		switch (e[i].kind)
		{
		case RSC_NAME_TREE:
			type = e[i].tkind == RSC_TKIND_MENU ? 1 : 0;
			break;
		case RSC_NAME_OBJ:
			type = 5;
			b = e[i].obj;
			break;
		case RSC_NAME_FRSTR:
			type = e[i].tkind == RSC_TKIND_ALRT ? 2 : 3;
			a = e[i].obj;
			break;
		default:
			type = 4;
			a = e[i].obj;
			break;
		}
		len = strlen(e[i].name);
		buf[k] = (uint8_t)type;
		buf[k + 2] = (uint8_t)(a >> 8);
		buf[k + 3] = (uint8_t)(a & 0xff);
		buf[k + 4] = (uint8_t)(b >> 8);
		buf[k + 5] = (uint8_t)(b & 0xff);
		memcpy(buf + k + 6, e[i].name, len);
		k += 6 + len + 1;
	}
	buf[k] = 6;	/* the end */
	k += 7;
	put_file(name, buf, k);
}

/* ---- kinds.rsc: every object kind -------------------------------------------- */

#define T(x)	check((x) == 0, #x)

static int add(struct rsc *r, int t, int parent, unsigned type)
{
	int	k = rsc_obj_add(r, t, parent, type);

	check(k >= 0, "rsc_obj_add");
	return k;
}

/* file form coordinates: cells plus a remainder in the high byte */
static int fc(int cells, int rem)
{
	return (int)(short)(unsigned short)((cells & 0xff) | ((rem & 0xff) << 8));
}

static short	planes[16 * 64];

static void ted(struct rsc *r, int t, int i, const char *text, const char *tmplt, const char *valid,
		int font, int just, int color, int thick)
{
	TEDINFO	te;

	memset(&te, 0, sizeof te);
	te.te_ptext = (char *)text;
	te.te_ptmplt = (char *)tmplt;
	te.te_pvalid = (char *)valid;
	te.te_font = (short)font;
	te.te_just = (short)just;
	te.te_color = (short)color;
	te.te_thickness = (short)thick;
	T(rsc_obj_set_tedinfo(r, t, i, &te));
}

/* The objects of tree 0, "DIALOG", by index (rsc_obj_add numbers them in
 * this order, and the writer keeps it since it is the preorder). */
static void tree_dialog(struct rsc *r)
{
	ICONBLK	ib;
	BITBLK	bb;
	int	t, k;
	struct rsc_cicon	*c;

	t = rsc_tree_add(r, 1);
	check(t == 0, "tree 0");
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	T(rsc_obj_set_value(r, t, 0, 0x00021100));		/* frame 2, colours 1 1 */
	T(rsc_obj_set_geom(r, t, 0, 0, 0, fc(40, 0), fc(20, 0)));
	k = add(r, t, 0, G_TEXT);					/* 1 */
	ted(r, t, k, "Name: \"x\"\\", "____", "XXXX", 3, 2, 0x1180, -1);
	T(rsc_obj_set_geom(r, t, k, fc(3, 4), fc(3, -2), fc(10, 0), fc(1, 0)));
	k = add(r, t, 0, G_BOXTEXT);					/* 2 */
	ted(r, t, k, "Box text", "", "", 5, 0, 0x1072, 1);
	k = add(r, t, 0, G_IMAGE);					/* 3 */
	memset(&bb, 0, sizeof bb);
	bb.bi_pdata = planes;
	bb.bi_wb = 2;
	bb.bi_hl = 8;
	bb.bi_color = 1;
	T(rsc_obj_set_bitblk(r, t, k, &bb));
	k = add(r, t, 0, G_PROGDEF);					/* 4 */
	T(rsc_obj_set_value(r, t, k, 0x12345678));
	k = add(r, t, 0, G_IBOX);					/* 5: a box with two buttons */
	T(rsc_obj_set_value(r, t, k, 0));
	k = add(r, t, 5, G_BUTTON);					/* 6 */
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "OK"));
	T(rsc_obj_set_flags(r, t, k, OF_SELECTABLE | OF_DEFAULT | OF_EXIT));
	k = add(r, t, 5, G_BUTTON);					/* 7 */
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "Cancel"));
	T(rsc_obj_set_flags(r, t, k, OF_SELECTABLE | OF_EXIT));
	T(rsc_obj_set_state(r, t, k, OS_SELECTED));
	k = add(r, t, 0, G_BOXCHAR);					/* 8 */
	T(rsc_obj_set_value(r, t, k, 0x41FF1172));
	k = add(r, t, 0, G_STRING);					/* 9 */
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "Tab\there\001 \351 end"));
	k = add(r, t, 0, G_FTEXT);					/* 10 */
	ted(r, t, k, "12", "Age: __", "99", 3, 0, 0x1100, 0);
	k = add(r, t, 0, G_FBOXTEXT);					/* 11 */
	ted(r, t, k, "abc", "Edit: ___", "XXX", 3, 1, 0x1181, 2);
	T(rsc_obj_set_flags(r, t, k, OF_EDITABLE));
	k = add(r, t, 0, G_ICON);					/* 12 */
	memset(&ib, 0, sizeof ib);
	ib.ib_pmask = planes;
	ib.ib_pdata = planes;
	ib.ib_ptext = "ICON";
	ib.ib_char = 0x1049;
	ib.ib_xchar = 6;
	ib.ib_ychar = 12;
	ib.ib_wicon = 32;
	ib.ib_hicon = 32;
	ib.ib_ytext = 32;
	ib.ib_wtext = 72;
	ib.ib_htext = 8;
	T(rsc_obj_set_iconblk(r, t, k, &ib));
	k = add(r, t, 0, G_TITLE);					/* 13 */
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, " File "));
	k = add(r, t, 0, G_CICON);					/* 14 */
	ib.ib_wicon = 16;
	ib.ib_hicon = 16;
	ib.ib_ptext = NULL;
	T(rsc_obj_set_cicon(r, t, k, &ib, "CIC"));
	c = rsc_obj_cicon(r, t, k);
	check(c != NULL, "cicon");
	check(rsc_cicon_add_version(c, 4, planes, planes, planes, planes) == 0, "version 0");
	check(rsc_cicon_add_version(c, 8, planes, planes, NULL, NULL) == 1, "version 1");
	check(rsc_cicon_add_version(c, 4, planes, planes, planes, planes) == 2, "version 2");
	k = add(r, t, 0, 0x50);						/* 15: an unknown type */
	T(rsc_obj_set_value(r, t, k, 0xDEADBEEF));
	k = add(r, t, 0, 0x1100 | G_BUTTON);				/* 16: extended type, odd bits */
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "Ext"));
	T(rsc_obj_set_flags(r, t, k, 0x0600 | 0x0800 | 0x2000));
	T(rsc_obj_set_state(r, t, k, 0x00C0 | OS_CROSSED | OS_CHECKED | OS_DISABLED | OS_OUTLINED
			    | OS_SHADOWED));
	k = add(r, t, 0, G_STRING);					/* 17: INDIRECT */
	T(rsc_obj_set_flags(r, t, k, OF_INDIRECT | OF_HIDETREE | OF_RBUTTON | OF_TOUCHEXIT));
	T(rsc_obj_set_value(r, t, k, 0x42));
	k = add(r, t, 0, G_BOX);					/* 18 */
	T(rsc_obj_set_flags(r, t, k, 0x0200));
	T(rsc_obj_set_value(r, t, k, 0x00FF1100));
	k = add(r, t, 0, G_BOX);					/* 19 */
	T(rsc_obj_set_flags(r, t, k, 0x0400));
	T(rsc_obj_set_value(r, t, k, 0x00011100));
	T(rsc_obj_set_geom(r, t, k, fc(255, 128), fc(0, 129), fc(1, 1), fc(0, 255)));
}

/* tree 1: menu-shaped (root, bar, active, titles; screen, a drop-down) */
static void tree_menu(struct rsc *r)
{
	int	t = rsc_tree_add(r, 1), bar, act, scr, drop, k;

	check(t == 1, "tree 1");
	T(rsc_obj_set_type(r, t, 0, G_IBOX));
	bar = add(r, t, 0, G_BOX);
	act = add(r, t, bar, G_IBOX);
	k = add(r, t, act, G_TITLE);
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, " Desk "));
	k = add(r, t, act, G_TITLE);
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, " File "));
	scr = add(r, t, 0, G_IBOX);
	drop = add(r, t, scr, G_BOX);
	k = add(r, t, drop, G_STRING);
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "  About...  "));
	k = add(r, t, drop, G_STRING);
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "  Quit  "));
}

static void kinds(void)
{
	struct rsc	*r = rsc_new();
	BITBLK		bb;
	int		t, i, j, k, box;
	char		longs[101];

	check(r != NULL, "rsc_new");
	for (i = 0; i < (int)(sizeof planes / sizeof planes[0]); i++)
		planes[i] = (short)(i * 0x0101 + 0x1234);
	tree_dialog(r);
	tree_menu(r);
	/* tree 2: a panel (no exit flags, a G_BOX root) */
	t = rsc_tree_add(r, 1);
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	k = add(r, t, 0, G_STRING);
	T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, "Panel"));
	/* tree 3: a lone string: kind UNKN */
	t = rsc_tree_add(r, 1);
	T(rsc_obj_set_type(r, t, 0, G_STRING));
	T(rsc_obj_set_str(r, t, 0, RSC_STR_SPEC, "lonely"));
	/* tree 4: 321 objects, 20 boxes of 15 strings */
	t = rsc_tree_add(r, 1);
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	for (i = 0; i < 20; i++)
	{
		box = add(r, t, 0, G_BOX);
		for (j = 0; j < 15; j++)
		{
			char	s[16];

			snprintf(s, sizeof s, "item %d", i * 15 + j);
			k = add(r, t, box, G_STRING);
			T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, s));
		}
	}
	/* tree 5: named ".images" in the name files */
	t = rsc_tree_add(r, 1);
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	add(r, t, 0, G_BOX);
	/* tree 6: root and A, B, C; the patch unlinks B */
	t = rsc_tree_add(r, 1);
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	for (i = 0; i < 3; i++)
	{
		k = add(r, t, 0, G_STRING);
		T(rsc_obj_set_str(r, t, k, RSC_STR_SPEC, i == 0 ? "A" : i == 1 ? "B" : "C"));
	}
	/* free strings */
	check(rsc_frstr_add(r, "Hello, world") == 0, "frstr 0");
	check(rsc_frstr_add(r, "[1][Delete this?|Really][ OK |Cancel]") == 1, "frstr 1");
	check(rsc_frstr_add(r, "[2][Question][Yes|No]") == 2, "frstr 2");
	check(rsc_frstr_add(r, "Say \"hi\"\\ \033[0m \344 \233") == 3, "frstr 3");
	for (i = 0; i < 100; i++)
		longs[i] = (char)('a' + i % 26);
	longs[100] = 0;
	check(rsc_frstr_add(r, longs) == 4, "frstr 4");
	/* free images */
	memset(&bb, 0, sizeof bb);
	bb.bi_pdata = planes;
	bb.bi_wb = 2;
	bb.bi_hl = 8;
	bb.bi_color = 1;
	check(rsc_frimg_add(r, &bb) == 0, "frimg 0");
	bb.bi_wb = 3;
	bb.bi_hl = 4;
	bb.bi_x = 1;
	bb.bi_y = 2;
	bb.bi_color = 3;
	check(rsc_frimg_add(r, &bb) == 1, "frimg 1");
	write_rsc(r, "kinds.rsc", NULL);
}

static unsigned be16(const uint8_t *p)
{
	return (unsigned)p[0] << 8 | p[1];
}

/* kinds-lost.rsc: the last tree's first object skips the second, which no
 * root reaches then (it keeps its links and spec in the file). */
static void unlink_b(uint8_t *buf, size_t len)
{
	unsigned	obj = be16(buf + 2), nobs = be16(buf + 20);
	size_t		at = obj + (size_t)(nobs - 3) * 24;

	check(at + 2 <= len, "unlink_b");
	buf[at] = 0;
	buf[at + 1] = 3;
}

static void kinds_lost(void)
{
	FILE		*f = fopen(path("kinds.rsc"), "rb");
	uint8_t		buf[65536];
	size_t		len;

	check(f != NULL, "kinds.rsc");
	len = fread(buf, 1, sizeof buf, f);
	fclose(f);
	unlink_b(buf, len);
	put_file("kinds-lost.rsc", buf, len);
}

static const struct ent kinds_names[] =
{
	{ RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "DIALOG" },
	{ RSC_NAME_TREE, RSC_TKIND_MENU, 1, 0, "MAINMENU" },
	{ RSC_NAME_TREE, RSC_TKIND_PANL, 4, 0, "BIG" },
	{ RSC_NAME_TREE, RSC_TKIND_UNKN, 5, 0, ".images" },
	{ RSC_NAME_TREE, RSC_TKIND_DIAL, 6, 0, "UNREACH" },
	{ RSC_NAME_OBJ, 0, 0, 0, "ROOT" },
	{ RSC_NAME_OBJ, 0, 0, 1, "Title" },
	{ RSC_NAME_OBJ, 0, 0, 2, "TITLE" },
	{ RSC_NAME_OBJ, 0, 0, 3, "DUP" },
	{ RSC_NAME_OBJ, 0, 0, 4, "DUP" },
	{ RSC_NAME_OBJ, 0, 0, 5, "BUTTONS" },
	{ RSC_NAME_OBJ, 0, 0, 6, "OK" },
	{ RSC_NAME_OBJ, 0, 0, 6, "OKAY" },
	{ RSC_NAME_OBJ, 0, 0, 7, "CANCEL" },
	{ RSC_NAME_OBJ, 0, 0, 8, "BOXCHAR" },
	{ RSC_NAME_OBJ, 0, 0, 9, "#3" },
	{ RSC_NAME_OBJ, 0, 0, 10, "F\233\344" },
	{ RSC_NAME_OBJ, 0, 0, 11, "EDIT" },
	{ RSC_NAME_OBJ, 0, 0, 12, "ICON" },
	{ RSC_NAME_OBJ, 0, 0, 13, "dup" },
	{ RSC_NAME_OBJ, 0, 0, 14, "CICON" },
	{ RSC_NAME_OBJ, 0, 6, 1, "A" },
	{ RSC_NAME_OBJ, 0, 6, 2, "LOST" },
	{ RSC_NAME_OBJ, 0, 6, 3, "C" },
	{ RSC_NAME_OBJ, 0, 1, 3, "DESK" },
	{ RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, 0, "HELLO" },
	{ RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, 2, "ASK" },
	{ RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 0, "LOGO" },
	{ 0, 0, 0, 0, NULL }
};

/* the .HRD adds names the other formats cannot hold */
static const struct ent kinds_hrd_extra[] =
{
	{ RSC_NAME_OBJ, 0, 4, 320, "ITEM300" },
	{ RSC_NAME_OBJ, 0, 0, 18, "MY BOX\344" },
	{ RSC_NAME_OBJ, 0, 0, 19, "A_LONG_NAME_16CH" },
	{ 0, 0, 0, 0, NULL }
};

/* names that do not match kinds.rsc: rsc_names_check drops 3 */
static const struct ent drop_names[] =
{
	{ RSC_NAME_TREE, RSC_TKIND_DIAL, 0, 0, "DIALOG" },
	{ RSC_NAME_TREE, RSC_TKIND_DIAL, 99, 0, "NOSUCH" },
	{ RSC_NAME_OBJ, 0, 0, 200, "NOOBJ" },
	{ RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, 7, "NOIMG" },
	{ 0, 0, 0, 0, NULL }
};

static void kinds_names_files(void)
{
	struct ent	all[64];
	size_t		n = ents_len(kinds_names), m = ents_len(kinds_hrd_extra);

	write_dfn("kinds.dfn", kinds_names);
	write_dfn("kinds-lost.dfn", kinds_names);
	write_def("kinds.def", kinds_names);
	memcpy(all, kinds_names, n * sizeof all[0]);
	memcpy(all + n, kinds_hrd_extra, (m + 1) * sizeof all[0]);
	write_hrd("kinds.hrd", all);
	write_dfn("drop.dfn", drop_names);
	put_file("bad.dfn", "\001\000\000", 3);			/* 1 record, 1 byte of it */
	put_file("bad.hrd", "\000\001\000\000\000\000\000\000\005\000\000\000\000\001AB", 16);
	put_file("names.txt", "DIALOG\n", 7);
}

/* ---- the name file discovery ---------------------------------------------------- */

static void small(const char *name)
{
	struct rsc	*r = rsc_new();
	int		t;

	check(r != NULL, "rsc_new");
	t = rsc_tree_add(r, 1);
	check(t == 0, "small");
	T(rsc_obj_set_type(r, t, 0, G_BOX));
	write_rsc(r, name, NULL);
}

static void names_of(const char *file, const char *tree0, int fmt)
{
	struct ent	e[2];

	memset(e, 0, sizeof e);
	e[0].kind = RSC_NAME_TREE;
	e[0].tkind = RSC_TKIND_DIAL;
	e[0].name = tree0;
	if (fmt == 0)
		write_dfn(file, e);
	else if (fmt == 1)
		write_def(file, e);
	else
		write_hrd(file, e);
}

static void discovery(void)
{
	sub_dir("disc");
	sub_dir("disc/d1");
	small("disc/d1/x.rsc");
	names_of("disc/d1/x.dfn", "DFNLOW", 0);
	sub_dir("disc/d2");
	small("disc/d2/x.rsc");
	names_of("disc/d2/x.DFN", "DFNUP", 0);
	sub_dir("disc/d3");				/* .DFN before .def */
	small("disc/d3/x.rsc");
	names_of("disc/d3/x.DFN", "DFNUP", 0);
	names_of("disc/d3/x.def", "DEFLOW", 1);
	sub_dir("disc/d4");				/* .DEF before .rsd */
	small("disc/d4/x.rsc");
	names_of("disc/d4/x.DEF", "DEFUP", 1);
	names_of("disc/d4/x.rsd", "RSDLOW", 1);
	sub_dir("disc/d5");				/* .RSD before .hrd */
	small("disc/d5/x.rsc");
	names_of("disc/d5/x.RSD", "RSDUP", 1);
	names_of("disc/d5/x.hrd", "HRDLOW", 2);
	sub_dir("disc/d6");
	small("disc/d6/x.rsc");
	names_of("disc/d6/x.HRD", "HRDUP", 2);
	sub_dir("disc/d7");
	small("disc/d7/x.rsc");
	names_of("disc/d7/x.hrd", "HRDLOW", 2);
	sub_dir("disc/d8");				/* no extension */
	small("disc/d8/noext");
	names_of("disc/d8/noext.dfn", "DFNLOW", 0);
	sub_dir("disc/d9");				/* a bad .def is used and fails */
	small("disc/d9/x.rsc");
	put_file("disc/d9/x.def", "\000\002\000\000", 4);
	names_of("disc/d9/x.hrd", "HRDLOW", 2);
	sub_dir("disc/d10");				/* the .RSC base name keeps its case */
	small("disc/d10/Mixed.RSC");
	names_of("disc/d10/Mixed.dfn", "MIXED", 0);
	sub_dir("disc/d11");				/* a directory named like a name file is skipped */
	small("disc/d11/x.rsc");
	sub_dir("disc/d11/x.dfn");
	names_of("disc/d11/x.def", "DEFLOW", 1);
}

/* ---- big trees ------------------------------------------------------------------ */

/* n objects: chain (each the only child of the previous) or flat (all
 * children of the root), as G_IBOX objects.  The classic format cannot
 * hold more than about 2700 objects below its 64 KB offsets, and the
 * writer refuses more, but the object table of a file may run past 64 KB
 * when its offset is below: what a hostile file can do.  So these files
 * are written byte by byte: the header, the tree index at 36, the objects
 * from 40. */
static void put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)(v & 0xff);
}

static void big(const char *name, int n, int chain)
{
	size_t	len = 40 + (size_t)n * 24;
	uint8_t	*buf = calloc(1, len);
	int	i, next, head, tail;

	check(buf != NULL, "big");
	put16(buf + 2, 40);		/* rsh_object */
	put16(buf + 4, 40);		/* rsh_tedinfo, iconblk, bitblk: empty */
	put16(buf + 6, 40);
	put16(buf + 8, 40);
	put16(buf + 10, 36);		/* rsh_frstr, string, imdata, frimg: empty */
	put16(buf + 12, 36);
	put16(buf + 14, 36);
	put16(buf + 16, 36);
	put16(buf + 18, 36);		/* rsh_trindex */
	put16(buf + 20, (unsigned)n);	/* rsh_nobs */
	put16(buf + 22, 1);		/* rsh_ntree */
	put16(buf + 34, 0xffff);	/* rsh_rssize: not trusted */
	buf[39] = 40;			/* the root at offset 40 */
	for (i = 0; i < n; i++)
	{
		uint8_t	*o = buf + 40 + (size_t)i * 24;

		if (chain)
		{
			next = i == 0 ? -1 : i - 1;
			head = tail = i + 1 < n ? i + 1 : -1;
		}
		else
		{
			next = i == 0 ? -1 : i + 1 < n ? i + 1 : 0;
			head = i == 0 && n > 1 ? 1 : -1;
			tail = i == 0 && n > 1 ? n - 1 : -1;
		}
		put16(o, (unsigned)next & 0xffff);
		put16(o + 2, (unsigned)head & 0xffff);
		put16(o + 4, (unsigned)tail & 0xffff);
		put16(o + 6, G_IBOX);
		put16(o + 8, i + 1 == n ? OF_LASTOB : 0);
		put16(o + 16, (unsigned)(i & 0x7f));
		put16(o + 20, 1);
		put16(o + 22, 1);
	}
	put_file(name, buf, len);
	free(buf);
}

/* huge.hrd for huge.rsc (big(), 20000 objects): the most entries a list
 * holds, 65535: the tree, then names for the objects in turn ("N<obj>",
 * then "N<obj>_<k>" for the later rounds). */
static void huge_names(void)
{
	size_t	cap = 8 + 65535 * 24 + 8, k = 8;
	uint8_t	*buf = calloc(1, cap);
	char	nm[24];
	long	i;
	int	obj, len;

	check(buf != NULL, "huge");
	buf[1] = 1;
	memcpy(buf + k + 6, "HUGE", 5);		/* type 0 DIAL, tree 0 */
	k += 6 + 5;
	for (i = 0; i < 65534; i++)
	{
		obj = (int)(i % 20000);
		if (i < 20000)
			len = snprintf(nm, sizeof nm, "N%d", obj);
		else
			len = snprintf(nm, sizeof nm, "N%d_%ld", obj, i / 20000);
		buf[k] = 5;
		buf[k + 4] = (uint8_t)(obj >> 8);
		buf[k + 5] = (uint8_t)(obj & 0xff);
		memcpy(buf + k + 6, nm, (size_t)len + 1);
		k += 6 + (size_t)len + 1;
	}
	buf[k] = 6;
	k += 7;
	put_file("huge.hrd", buf, k);
	free(buf);
}

/* ---- flip ----------------------------------------------------------------------- */

static int flip(const char *in, const char *out, long off, long x)
{
	static uint8_t	buf[1 << 20];
	FILE		*f = fopen(in, "rb");
	size_t		len;

	check(f != NULL, in);
	len = fread(buf, 1, sizeof buf, f);
	fclose(f);
	if (off >= 0 && (size_t)off < len)
		buf[off] ^= (uint8_t)x;
	f = fopen(out, "wb");
	check(f != NULL, out);
	check(fwrite(buf, 1, len, f) == len, out);
	check(fclose(f) == 0, out);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 6 && strcmp(argv[1], "flip") == 0)
		return flip(argv[2], argv[3], strtol(argv[4], NULL, 0), strtol(argv[5], NULL, 0));
	if (argc != 3 || strcmp(argv[1], "all") != 0)
	{
		fprintf(stderr, "usage: mkfix all DIR | mkfix flip IN OUT OFFSET XOR\n");
		return 2;
	}
	dir = argv[2];
	kinds();
	kinds_lost();
	kinds_names_files();
	discovery();
	big("deep.rsc", RSC_MAX_OBJECTS, 1);
	big("flat.rsc", 5000, 0);
	big("huge.rsc", 20000, 0);
	huge_names();
	put_file("empty.rsc", "", 0);
	return 0;
}
