/* TEST_RSC.C	host tests of the resource library (src/rsc): make rsc-test */
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

/* ---- helpers ------------------------------------------------------------------ */

/* The links of tree t are an AES tree (rsc_tree_preorder accepts them),
 * the root's ob_next is -1 and OF_LASTOB is on the last array element
 * only. */
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

/* The preorder of the n objects at o as a list "0 1 3 2", or the error
 * code as "E-3"; the buffer is static. */
static const char *walk_objs(const OBJECT *o, int n)
{
	static char	buf[512];
	int	order[100], k, i;
	size_t	len = 0;

	buf[0] = '\0';
	if (!o || n > 100)
		return "?";
	k = rsc_tree_preorder(o, n, order);
	if (k < 0)
	{
		snprintf(buf, sizeof buf, "E%d", k);
		return buf;
	}
	for (i = 0; i < k; i++)
		len += (size_t)snprintf(buf + len, sizeof buf - len, i ? " %d" : "%d", order[i]);
	return buf;
}

/* The preorder of tree t (see walk_objs). */
static const char *walk(struct rsc *r, int t)
{
	int	n;
	OBJECT	*o = rsc_tree_objects(r, t, &n);

	return walk_objs(o, n);
}

static short	icon_data[32], icon_mask[32];	/* a plane of 32x16 */
static short	image_data[8];

static void fill(short *w, int n, int seed)
{
	int	i;

	for (i = 0; i < n; i++)
		w[i] = (short)(seed * 1000 + i);
}

/* Builds a dialog with every kind of object, two free strings, a free image
 * and a colour icon with two versions.  Stops at the first failure and
 * returns -1; 0 when all went well. */
static int build_all(struct rsc *r)
{
	TEDINFO	te;
	ICONBLK	ib;
	BITBLK	bb;
	struct rsc_cicon	*c;
	short	col[4 * 16], cmask[16], sel[4 * 16], smask[16];
	int	t, box, o;

	memset(&te, 0, sizeof te);
	memset(&ib, 0, sizeof ib);
	memset(&bb, 0, sizeof bb);
	fill(icon_data, 32, 1);
	fill(icon_mask, 32, 2);
	fill(image_data, 8, 3);
	fill(col, 4 * 16, 4);
	fill(cmask, 16, 5);
	fill(sel, 4 * 16, 6);
	fill(smask, 16, 7);

	if ((t = rsc_tree_add(r, 1)) < 0)
		return -1;
	if (rsc_obj_set_type(r, t, 0, G_BOX) || rsc_obj_set_value(r, t, 0, 0x00021100UL)
	    || rsc_obj_set_geom(r, t, 0, 0, 0, 40, 20))
		return -1;
	/* 1 */
	if ((o = rsc_obj_add(r, t, 0, G_STRING)) < 0 || rsc_obj_set_str(r, t, o, RSC_STR_SPEC, "A string"))
		return -1;
	/* 2 */
	if ((o = rsc_obj_add(r, t, 0, G_BUTTON)) < 0 || rsc_obj_set_str(r, t, o, RSC_STR_SPEC, "OK")
	    || rsc_obj_set_flags(r, t, o, OF_SELECTABLE | OF_DEFAULT | OF_EXIT))
		return -1;
	/* 3 */
	if ((o = rsc_obj_add(r, t, 0, G_TITLE)) < 0 || rsc_obj_set_str(r, t, o, RSC_STR_SPEC, " Desk "))
		return -1;
	/* 4..7: the four text types */
	te.te_ptext = "@1234";
	te.te_ptmplt = "Name: _____";
	te.te_pvalid = "XXXXX";
	te.te_font = 3;
	te.te_just = 2;
	te.te_color = 0x1180;
	te.te_thickness = -1;
	te.te_txtlen = 6;
	te.te_tmplen = 12;
	if ((o = rsc_obj_add(r, t, 0, G_TEXT)) < 0 || rsc_obj_set_tedinfo(r, t, o, &te))
		return -1;
	if ((o = rsc_obj_add(r, t, 0, G_BOXTEXT)) < 0 || rsc_obj_set_tedinfo(r, t, o, &te))
		return -1;
	if ((o = rsc_obj_add(r, t, 0, G_FTEXT)) < 0 || rsc_obj_set_tedinfo(r, t, o, &te)
	    || rsc_obj_set_flags(r, t, o, OF_EDITABLE))
		return -1;
	if ((o = rsc_obj_add(r, t, 0, G_FBOXTEXT)) < 0 || rsc_obj_set_tedinfo(r, t, o, &te))
		return -1;
	/* 8: an icon of 32x8 (32 words a plane) */
	ib.ib_pmask = icon_mask;
	ib.ib_pdata = icon_data;
	ib.ib_ptext = "ICON";
	ib.ib_char = 0x1041;
	ib.ib_wicon = 32;
	ib.ib_hicon = 16;
	ib.ib_ytext = 16;
	ib.ib_wtext = 32;
	ib.ib_htext = 8;
	if ((o = rsc_obj_add(r, t, 0, G_ICON)) < 0 || rsc_obj_set_iconblk(r, t, o, &ib))
		return -1;
	/* 9: an image of 2 bytes x 8 lines */
	bb.bi_pdata = image_data;
	bb.bi_wb = 2;
	bb.bi_hl = 8;
	bb.bi_color = 1;
	if ((o = rsc_obj_add(r, t, 0, G_IMAGE)) < 0 || rsc_obj_set_bitblk(r, t, o, &bb))
		return -1;
	/* 10: a box with a G_BOXCHAR (11) and a G_IBOX (12) inside */
	if ((box = rsc_obj_add(r, t, 0, G_BOX)) < 0 || rsc_obj_set_value(r, t, box, 0x00ff1121UL))
		return -1;
	if ((o = rsc_obj_add(r, t, box, G_BOXCHAR)) < 0 || rsc_obj_set_value(r, t, o, 0x41001100UL))
		return -1;
	if ((o = rsc_obj_add(r, t, box, G_IBOX)) < 0)
		return -1;
	/* 13: a colour icon of 16x16 with two versions */
	ib.ib_wicon = 16;
	ib.ib_hicon = 16;
	if ((o = rsc_obj_add(r, t, 0, G_CICON)) < 0 || rsc_obj_set_cicon(r, t, o, &ib, "CICON")
	    || !(c = rsc_obj_cicon(r, t, o)))
		return -1;
	if (rsc_cicon_add_version(c, 4, col, cmask, sel, smask) != 0
	    || rsc_cicon_add_version(c, 1, col, cmask, NULL, NULL) != 1)
		return -1;
	/* free strings and images */
	if (rsc_frstr_add(r, "[1][An alert][ OK ]") != 0 || rsc_frstr_add(r, "free") != 1)
		return -1;
	if (rsc_frimg_add(r, &bb) != 0)
		return -1;
	return 0;
}

/* Every tree of r is a well-formed AES tree. */
static int all_trees_ok(struct rsc *r)
{
	int	t;

	for (t = 0; t < rsc_ntrees(r); t++)
		if (!tree_ok(r, t))
			return 0;
	return 1;
}

/* ---- the tests ------------------------------------------------------------------ */

static void test_new_free(void)
{
	struct rsc	*r = rsc_new();
	int	w = -1, h = -1;

	CHECK(r != NULL);
	if (!r)
		return;
	CHECK(rsc_ntrees(r) == 0);
	CHECK(rsc_nfrstr(r) == 0);
	CHECK(rsc_nfrimg(r) == 0);
	CHECK(rsc_vrsn(r) == 0);
	rsc_cell(r, &w, &h);
	CHECK(w == 0 && h == 0);
	CHECK(rsc_set_cell(r, 8, 16) == 0);
	rsc_cell(r, &w, &h);
	CHECK(w == 8 && h == 16);
	CHECK(rsc_set_cell(r, 8, 0) == -1);
	CHECK(rsc_set_cell(r, -1, 16) == -1);
	CHECK(rsc_set_cell(r, 0, 0) == 0);
	rsc_set_vrsn(r, 0x0005);
	CHECK(rsc_vrsn(r) == 0x0001);		/* the format bit is not kept */
	CHECK(rsc_tree_objects(r, 0, NULL) == NULL);
	CHECK(rsc_tree_add(r, 0) == -1);
	CHECK(rsc_tree_add(r, RSC_MAX_OBJECTS + 1) == -1);
	CHECK(rsc_tree_delete(r, 0) == -1);
	CHECK(rsc_frstr(r, 0) == NULL);
	CHECK(rsc_frimg(r, 0) == NULL);
	rsc_free(r);
	rsc_free(NULL);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_raw_tree(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	int	n, i;

	CHECK(rsc_tree_add(r, 3) == 0);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o != NULL && n == 3);
	for (i = 0; i < 3; i++)
	{
		CHECK(o[i].ob_next == -1 && o[i].ob_head == -1 && o[i].ob_tail == -1);
		CHECK(o[i].ob_type == G_IBOX && o[i].ob_spec.index == 0);
	}
	CHECK(o[2].ob_flags == OF_LASTOB && o[0].ob_flags == 0);
	/* a reader writes the links itself */
	o[0].ob_head = 1;
	o[0].ob_tail = 2;
	o[1].ob_next = 2;
	o[2].ob_next = 0;
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 2");
	CHECK(rsc_tree_add(r, 1) == 1);
	CHECK(rsc_ntrees(r) == 2);
	CHECK(rsc_tree_delete(r, 0) == 0);
	CHECK(rsc_ntrees(r) == 1);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 1);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_build_all(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	TEDINFO	*te;
	ICONBLK	*ib;
	BITBLK	*bb;
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	int	n, i;

	CHECK(build_all(r) == 0);
	CHECK(rsc_ntrees(r) == 1);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 14);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 2 3 4 5 6 7 8 9 10 11 12 13");
	CHECK(o[0].ob_type == G_BOX && rsc_obj_value(r, 0, 0) == 0x00021100UL);
	CHECK(o[0].ob_width == 40 && o[0].ob_height == 20);
	CHECK(o[10].ob_head == 11 && o[10].ob_tail == 12 && o[12].ob_next == 10);
	CHECK(rsc_obj_value(r, 0, 11) == 0x41001100UL);
	CHECK(rsc_obj_value(r, 0, 1) == 0);	/* not a value kind */
	CHECK_STR(o[1].ob_spec.free_string, "A string");
	CHECK_STR(o[2].ob_spec.free_string, "OK");
	CHECK(o[2].ob_flags == (OF_SELECTABLE | OF_DEFAULT | OF_EXIT));
	CHECK_STR(o[3].ob_spec.free_string, " Desk ");
	for (i = 4; i <= 7; i++)
	{
		te = o[i].ob_spec.tedinfo;
		CHECK(te != NULL);
		if (!te)
			continue;
		CHECK_STR(te->te_ptext, "@1234");
		CHECK_STR(te->te_ptmplt, "Name: _____");
		CHECK_STR(te->te_pvalid, "XXXXX");
		CHECK(te->te_font == 3 && te->te_just == 2 && te->te_color == 0x1180);
		CHECK(te->te_thickness == -1 && te->te_txtlen == 6 && te->te_tmplen == 12);
	}
	/* each object has its own copies */
	CHECK(o[4].ob_spec.tedinfo != o[5].ob_spec.tedinfo);
	CHECK(o[4].ob_spec.tedinfo->te_ptext != o[5].ob_spec.tedinfo->te_ptext);
	ib = o[8].ob_spec.iconblk;
	CHECK(ib && ib->ib_pmask != icon_mask && ib->ib_pdata != icon_data);
	CHECK(rsc_plane_bytes(32, 16) == 64);
	CHECK(ib && memcmp(ib->ib_pdata, icon_data, 64) == 0 && memcmp(ib->ib_pmask, icon_mask, 64) == 0);
	CHECK(ib && ib->ib_char == 0x1041 && ib->ib_wicon == 32 && ib->ib_hicon == 16);
	CHECK(ib && ib->ib_htext == 8);
	CHECK_STR(ib->ib_ptext, "ICON");
	bb = o[9].ob_spec.bitblk;
	CHECK(bb && bb->bi_pdata != image_data && memcmp(bb->bi_pdata, image_data, 16) == 0);
	CHECK(bb && bb->bi_wb == 2 && bb->bi_hl == 8 && bb->bi_color == 1);
	c = rsc_obj_cicon(r, 0, 13);
	CHECK(c != NULL && (void *)o[13].ob_spec.ciconblk == (void *)c);
	if (c)
	{
		CHECK(c->blk.monoblk.ib_ptext == c->text);
		CHECK_STR(c->text, "CICON");
		CHECK(memcmp(c->blk.monoblk.ib_pdata, icon_data, 32) == 0);
		CHECK(rsc_cicon_nversions(c) == 2);
		v = rsc_cicon_version(c, 0);
		CHECK(v && v->c.num_planes == 4 && v->c.sel_data && v->c.sel_mask);
		CHECK(v && (CICON *)c->blk.mainlist == &v->c);
		CHECK(v && v->c.col_data[63] == 4 * 1000 + 63 && v->c.sel_mask[15] == 7 * 1000 + 15);
		CHECK(v && v->file_ptr[0] == 0);
		v = rsc_cicon_version(c, 1);
		CHECK(v && v->c.num_planes == 1 && !v->c.sel_data && !v->c.sel_mask && !v->c.next_res);
		CHECK(rsc_cicon_version(c, 2) == NULL);
	}
	CHECK(rsc_obj_cicon(r, 0, 12) == NULL);
	CHECK(rsc_nfrstr(r) == 2);
	CHECK_STR(rsc_frstr(r, 0), "[1][An alert][ OK ]");
	CHECK(rsc_nfrimg(r) == 1 && rsc_frimg(r, 0)->bi_pdata != image_data);
	/* setting a spec on the wrong kind fails */
	CHECK(rsc_obj_set_tedinfo(r, 0, 1, NULL) == -1);
	CHECK(rsc_obj_set_value(r, 0, 1, 5) == -1);
	CHECK(rsc_obj_set_bitblk(r, 0, 8, NULL) == -1);
	CHECK(rsc_obj_set_iconblk(r, 0, 9, NULL) == -1);
	CHECK(rsc_obj_set_cicon(r, 0, 8, NULL, NULL) == -1);
	/* bad indices */
	CHECK(rsc_obj_add(r, 0, 14, G_BOX) == -1);
	CHECK(rsc_obj_add(r, 1, 0, G_BOX) == -1);
	CHECK(rsc_obj_set_type(r, 0, 14, G_BOX) == -1);
	CHECK(rsc_obj_set_geom(r, 0, -1, 0, 0, 0, 0) == -1);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_links(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	int	n, a, b, c, d, e;

	CHECK(rsc_tree_add(r, 1) == 0);
	a = rsc_obj_add(r, 0, 0, G_BOX);		/* 1 */
	b = rsc_obj_add(r, 0, 0, G_STRING);		/* 2 */
	c = rsc_obj_add(r, 0, a, G_STRING);		/* 3, in a */
	d = rsc_obj_add(r, 0, 0, G_TEXT);		/* 4 */
	e = rsc_obj_add(r, 0, a, G_BUTTON);		/* 5, in a */
	CHECK(a == 1 && b == 2 && c == 3 && d == 4 && e == 5);
	CHECK(rsc_obj_set_str(r, 0, b, RSC_STR_SPEC, "b") == 0);
	CHECK(rsc_obj_set_str(r, 0, c, RSC_STR_SPEC, "c") == 0);
	CHECK(rsc_obj_set_str(r, 0, e, RSC_STR_SPEC, "e") == 0);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 3 5 2 4");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[0].ob_head == 1 && o[0].ob_tail == 4);
	CHECK(o[1].ob_next == 2 && o[2].ob_next == 4 && o[4].ob_next == 0);
	CHECK(o[1].ob_head == 3 && o[1].ob_tail == 5 && o[3].ob_next == 5 && o[5].ob_next == 1);

	CHECK(rsc_obj_delete(r, 0, 0) == -1);	/* not the root */
	CHECK(rsc_obj_delete(r, 0, 6) == -1);

	/* a middle child */
	CHECK(rsc_obj_delete(r, 0, b) == 0);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 3 5 4");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 6);				/* the slot stays */
	CHECK(o[b].ob_type == G_IBOX && o[b].ob_spec.index == 0 && o[b].ob_next == -1);
	/* the last child of a nested box */
	CHECK(rsc_obj_delete(r, 0, e) == 0);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 3 4");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[1].ob_head == 3 && o[1].ob_tail == 3 && o[3].ob_next == 1);
	CHECK(o[5].ob_flags == OF_LASTOB);	/* still the last element */
	/* add again: appended, linked as the last child */
	CHECK(rsc_obj_add(r, 0, a, G_IBOX) == 6);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 3 6 4");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(!(o[5].ob_flags & OF_LASTOB) && o[6].ob_flags == OF_LASTOB);
	/* the first child of the root, with its subtree */
	CHECK(rsc_obj_delete(r, 0, a) == 0);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 4");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[0].ob_head == 4 && o[0].ob_tail == 4);
	CHECK(o[3].ob_type == G_IBOX && o[3].ob_spec.free_string == NULL && o[3].ob_next == -1);
	CHECK(o[6].ob_flags == OF_LASTOB);
	/* the only child */
	CHECK(rsc_obj_delete(r, 0, d) == 0);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0");
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[0].ob_head == -1 && o[0].ob_tail == -1);
	/* an unlinked object can be deleted too */
	CHECK(rsc_obj_delete(r, 0, b) == 0);
	/* the flags keep OF_LASTOB where the model wants it */
	CHECK(rsc_obj_set_flags(r, 0, 0, 0xffff) == 0);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[0].ob_flags == (0xffff & ~OF_LASTOB));
	CHECK(rsc_obj_set_flags(r, 0, n - 1, 0) == 0);
	CHECK(o[n - 1].ob_flags == OF_LASTOB);
	CHECK(rsc_obj_set_state(r, 0, 0, OS_SELECTED) == 0 && o[0].ob_state == OS_SELECTED);
	/* broken links make delete fail without changes */
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == 7);
	CHECK(rsc_obj_add(r, 0, 7, G_BOX) == 8);
	o = rsc_tree_objects(r, 0, &n);
	o[8].ob_head = o[8].ob_tail = 7;	/* a cycle */
	CHECK(rsc_obj_delete(r, 0, 7) == -1);
	CHECK(o[0].ob_head == 7 && o[7].ob_head == 8);
	o[8].ob_head = o[8].ob_tail = -1;
	CHECK(rsc_obj_delete(r, 0, 7) == 0);
	CHECK(tree_ok(r, 0));
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_type_change(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	TEDINFO	te, *pte;
	ICONBLK	ib;
	char	*s;
	int	n;

	memset(&te, 0, sizeof te);
	memset(&ib, 0, sizeof ib);
	CHECK(rsc_spec_kind(G_BOX) == RSC_SPEC_VALUE);
	CHECK(rsc_spec_kind(G_USERDEF) == RSC_SPEC_VALUE);
	CHECK(rsc_spec_kind(0x1200 | G_STRING) == RSC_SPEC_STRING);
	CHECK(rsc_spec_kind(G_FBOXTEXT) == RSC_SPEC_TEDINFO);
	CHECK(rsc_spec_kind(G_ICON) == RSC_SPEC_ICONBLK);
	CHECK(rsc_spec_kind(G_IMAGE) == RSC_SPEC_BITBLK);
	CHECK(rsc_spec_kind(G_CICON) == RSC_SPEC_CICON);
	CHECK(rsc_spec_kind(34) == RSC_SPEC_VALUE);

	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_STRING) == 1);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "text") == 0);
	o = rsc_tree_objects(r, 0, &n);
	s = o[1].ob_spec.free_string;
	/* same kind: kept */
	CHECK(rsc_obj_set_type(r, 0, 1, G_BUTTON) == 0);
	CHECK(o[1].ob_spec.free_string == s && o[1].ob_type == G_BUTTON);
	CHECK(rsc_obj_set_type(r, 0, 1, 0x1100 | G_TITLE) == 0);	/* extended type */
	CHECK(o[1].ob_spec.free_string == s && o[1].ob_type == (0x1100 | G_TITLE));
	/* another kind: released */
	CHECK(rsc_obj_set_type(r, 0, 1, G_TEXT) == 0);
	CHECK(o[1].ob_spec.tedinfo == NULL);
	te.te_ptext = "t";
	CHECK(rsc_obj_set_tedinfo(r, 0, 1, &te) == 0);
	pte = o[1].ob_spec.tedinfo;
	CHECK(pte && pte->te_ptmplt == NULL && pte->te_pvalid == NULL);
	CHECK(rsc_obj_set_type(r, 0, 1, G_FBOXTEXT) == 0);
	CHECK(o[1].ob_spec.tedinfo == pte);
	CHECK(rsc_obj_set_type(r, 0, 1, G_ICON) == 0);
	CHECK(o[1].ob_spec.iconblk == NULL);
	ib.ib_wicon = 17;		/* 2 words a line */
	ib.ib_hicon = 2;
	ib.ib_pdata = icon_data;
	CHECK(rsc_obj_set_iconblk(r, 0, 1, &ib) == 0);
	CHECK(o[1].ob_spec.iconblk->ib_pmask == NULL && o[1].ob_spec.iconblk->ib_pdata != NULL);
	CHECK(rsc_plane_bytes(17, 2) == 8 && rsc_plane_bytes(-1, 2) == 0);
	CHECK(rsc_obj_set_type(r, 0, 1, G_CICON) == 0);
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, NULL) == 0);
	CHECK(rsc_obj_cicon(r, 0, 1) != NULL && rsc_obj_cicon(r, 0, 1)->text[0] == '\0');
	CHECK(rsc_obj_set_type(r, 0, 1, G_IMAGE) == 0);
	CHECK(o[1].ob_spec.bitblk == NULL);
	CHECK(rsc_obj_set_type(r, 0, 1, G_BOX) == 0);
	CHECK(o[1].ob_spec.index == 0);
	CHECK(rsc_obj_set_value(r, 0, 1, 0xfffe1173UL) == 0);
	CHECK(rsc_obj_set_type(r, 0, 1, G_IBOX) == 0);		/* value kept */
	CHECK(rsc_obj_value(r, 0, 1) == 0xfffe1173UL);
	CHECK(rsc_obj_set_type(r, 0, 1, G_STRING) == 0);
	CHECK(o[1].ob_spec.free_string == NULL);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "again") == 0);
	/* deleting the tree releases the last spec */
	CHECK(rsc_tree_delete(r, 0) == 0);
	CHECK(rsc_test_live_blocks() == 2);	/* r and its (empty) tree list */
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_strings(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	TEDINFO	te;
	ICONBLK	ib;
	struct rsc_cicon	*c;
	long	live;
	int	n;

	memset(&te, 0, sizeof te);
	memset(&ib, 0, sizeof ib);
	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_STRING) == 1);
	CHECK(rsc_obj_add(r, 0, 0, G_FTEXT) == 2);
	CHECK(rsc_obj_add(r, 0, 0, G_ICON) == 3);
	CHECK(rsc_obj_add(r, 0, 0, G_CICON) == 4);
	o = rsc_tree_objects(r, 0, &n);

	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "one") == 0);
	live = rsc_test_live_blocks();
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "two") == 0);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "three") == 0);
	CHECK(rsc_test_live_blocks() == live);	/* the old ones are freed */
	CHECK_STR(o[1].ob_spec.free_string, "three");
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, NULL) == 0);
	CHECK(o[1].ob_spec.free_string == NULL && rsc_test_live_blocks() == live - 1);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_TE_TEXT, "x") == -1);	/* wrong kind */

	/* the record must exist */
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_TE_TEXT, "x") == -1);
	CHECK(rsc_obj_set_str(r, 0, 3, RSC_STR_IB_TEXT, "x") == -1);
	CHECK(rsc_obj_set_str(r, 0, 4, RSC_STR_IB_TEXT, "x") == -1);
	CHECK(rsc_obj_set_tedinfo(r, 0, 2, &te) == 0);
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_TE_TEXT, "text") == 0);
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_TE_TMPLT, "____") == 0);
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_TE_VALID, "XXXX") == 0);
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_TE_TEXT, "TEXT") == 0);
	CHECK_STR(o[2].ob_spec.tedinfo->te_ptext, "TEXT");
	CHECK_STR(o[2].ob_spec.tedinfo->te_ptmplt, "____");
	CHECK_STR(o[2].ob_spec.tedinfo->te_pvalid, "XXXX");
	CHECK(rsc_obj_set_str(r, 0, 2, RSC_STR_SPEC, "x") == -1);
	/* replacing the record frees the old one with its strings */
	live = rsc_test_live_blocks();
	te.te_ptext = "new";
	CHECK(rsc_obj_set_tedinfo(r, 0, 2, &te) == 0);
	CHECK(rsc_test_live_blocks() == live - 2);	/* 4 blocks out, 2 in */
	CHECK(rsc_obj_set_tedinfo(r, 0, 2, NULL) == 0);
	CHECK(o[2].ob_spec.tedinfo == NULL);

	CHECK(rsc_obj_set_iconblk(r, 0, 3, &ib) == 0);
	CHECK(rsc_obj_set_str(r, 0, 3, RSC_STR_IB_TEXT, "icon") == 0);
	CHECK(rsc_obj_set_str(r, 0, 3, RSC_STR_IB_TEXT, "ICON") == 0);
	CHECK_STR(o[3].ob_spec.iconblk->ib_ptext, "ICON");

	CHECK(rsc_obj_set_cicon(r, 0, 4, &ib, "1234567890123") == -1);	/* 13 characters */
	CHECK(rsc_obj_set_cicon(r, 0, 4, &ib, "123456789012") == 0);
	c = rsc_obj_cicon(r, 0, 4);
	CHECK_STR(c->text, "123456789012");
	CHECK(rsc_obj_set_str(r, 0, 4, RSC_STR_IB_TEXT, "1234567890123") == -1);
	CHECK_STR(c->text, "123456789012");
	CHECK(rsc_obj_set_str(r, 0, 4, RSC_STR_IB_TEXT, "abc") == 0);
	CHECK_STR(c->blk.monoblk.ib_ptext, "abc");
	CHECK(c->text[3] == 0 && c->text[11] == 0 && c->text[12] == 0);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_free_lists(void)
{
	struct rsc	*r = rsc_new();
	BITBLK	bb;
	short	d1[4] = { 1, 2, 3, 4 }, d2[2] = { 5, 6 };

	memset(&bb, 0, sizeof bb);
	CHECK(rsc_frstr_add(r, NULL) == -1);
	CHECK(rsc_frstr_add(r, "a") == 0);
	CHECK(rsc_frstr_add(r, "b") == 1);
	CHECK(rsc_frstr_add(r, "c") == 2);
	CHECK(rsc_frstr_set(r, 1, "B") == 0);
	CHECK(rsc_frstr_set(r, 3, "x") == -1);
	CHECK(rsc_frstr_set(r, 0, NULL) == -1);
	CHECK_STR(rsc_frstr(r, 1), "B");
	CHECK(rsc_frstr_delete(r, 0) == 0);
	CHECK(rsc_nfrstr(r) == 2);
	CHECK_STR(rsc_frstr(r, 0), "B");
	CHECK_STR(rsc_frstr(r, 1), "c");
	CHECK(rsc_frstr_delete(r, 2) == -1);

	CHECK(rsc_frimg_add(r, NULL) == -1);
	bb.bi_pdata = d1;
	bb.bi_wb = 4;
	bb.bi_hl = 2;
	CHECK(rsc_frimg_add(r, &bb) == 0);
	bb.bi_pdata = d2;
	bb.bi_wb = 2;
	bb.bi_hl = 2;
	CHECK(rsc_frimg_add(r, &bb) == 1);
	bb.bi_pdata = NULL;
	CHECK(rsc_frimg_add(r, &bb) == 2);
	CHECK(rsc_nfrimg(r) == 3);
	CHECK(rsc_frimg(r, 0)->bi_pdata[3] == 4);
	CHECK(rsc_frimg(r, 2)->bi_pdata == NULL);
	CHECK(rsc_bitblk_bytes(rsc_frimg(r, 0)) == 8);
	CHECK(rsc_frimg_delete(r, 0) == 0);
	CHECK(rsc_nfrimg(r) == 2 && rsc_frimg(r, 0)->bi_pdata[1] == 6);
	CHECK(rsc_frimg_delete(r, 5) == -1);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_cicon_versions(void)
{
	struct rsc	*r = rsc_new();
	ICONBLK	ib;
	struct rsc_cicon	*c;
	short	col[16 * 16], mask[16];
	long	live;
	int	k;

	memset(&ib, 0, sizeof ib);
	ib.ib_wicon = 16;
	ib.ib_hicon = 16;
	fill(col, 16 * 16, 1);
	fill(mask, 16, 2);
	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_CICON) == 1);
	CHECK(rsc_obj_cicon(r, 0, 1) == NULL);		/* none yet */
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, "x") == 0);
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(c != NULL && rsc_cicon_nversions(c) == 0);
	CHECK(c->blk.monoblk.ib_pdata == NULL && c->blk.monoblk.ib_pmask == NULL);
	CHECK(rsc_cicon_add_version(c, 0, col, mask, NULL, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 17, col, mask, NULL, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 1, NULL, mask, NULL, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 1, col, NULL, NULL, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 1, col, mask, col, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 1, col, mask, NULL, mask) == -1);
	CHECK(rsc_cicon_add_version(c, 1, col, mask, NULL, NULL) == 0);
	CHECK(rsc_cicon_add_version(c, 4, col, mask, col, mask) == 1);
	CHECK(rsc_cicon_add_version(c, 8, col, mask, NULL, NULL) == 2);
	CHECK(rsc_cicon_nversions(c) == 3);
	CHECK(rsc_cicon_version(c, 2)->c.col_data[8 * 16 - 1] == 1000 + 8 * 16 - 1);
	live = rsc_test_live_blocks();
	CHECK(rsc_cicon_remove_version(c, 1) == 0);
	CHECK(rsc_test_live_blocks() == live - 5);	/* the node and four planes */
	CHECK(rsc_cicon_nversions(c) == 2);
	CHECK(rsc_cicon_version(c, 0)->c.num_planes == 1);
	CHECK(rsc_cicon_version(c, 1)->c.num_planes == 8);
	CHECK(rsc_cicon_version(c, 0)->c.next_res == &rsc_cicon_version(c, 1)->c);
	CHECK(rsc_cicon_remove_version(c, 2) == -1);
	CHECK(rsc_cicon_remove_version(c, -1) == -1);
	CHECK(rsc_cicon_remove_version(c, 1) == 0);
	CHECK(rsc_cicon_version(c, 0)->c.next_res == NULL);
	CHECK(rsc_cicon_remove_version(c, 0) == 0);
	CHECK(c->blk.mainlist == NULL);
	CHECK(rsc_cicon_add_version(c, 2, col, mask, NULL, NULL) == 0);
	/* replacing the colour icon drops its versions */
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, "y") == 0);
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(rsc_cicon_nversions(c) == 0);
	CHECK(rsc_cicon_add_version(c, 2, col, mask, NULL, NULL) == 0);
	CHECK(rsc_obj_set_cicon(r, 0, 1, NULL, NULL) == 0);
	CHECK(rsc_obj_cicon(r, 0, 1) == NULL);
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, NULL) == 0);
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(rsc_cicon_add_version(c, 3, col, mask, col, mask) == 0);
	/* RSC_MAX_VERSIONS versions, not one more; the tail stays right after
	 * removing the last and the first */
	for (k = 1; k < RSC_MAX_VERSIONS; k++)
		CHECK(rsc_cicon_add_version(c, 1 + k % RSC_MAX_PLANES, col, mask, NULL, NULL) == k);
	CHECK(rsc_cicon_nversions(c) == RSC_MAX_VERSIONS);
	live = rsc_test_live_blocks();
	CHECK(rsc_cicon_add_version(c, 1, col, mask, NULL, NULL) == -1);
	CHECK(rsc_test_live_blocks() == live && rsc_cicon_nversions(c) == RSC_MAX_VERSIONS);
	CHECK(rsc_cicon_version(c, RSC_MAX_VERSIONS - 1)->c.next_res == NULL);
	CHECK(rsc_cicon_remove_version(c, RSC_MAX_VERSIONS - 1) == 0);
	CHECK(rsc_cicon_remove_version(c, 0) == 0);
	CHECK(rsc_cicon_nversions(c) == RSC_MAX_VERSIONS - 2);
	CHECK(rsc_cicon_add_version(c, 5, col, mask, NULL, NULL) == RSC_MAX_VERSIONS - 2);
	CHECK(rsc_cicon_version(c, RSC_MAX_VERSIONS - 3)->c.next_res
	      == &rsc_cicon_version(c, RSC_MAX_VERSIONS - 2)->c);
	CHECK(rsc_cicon_version(c, RSC_MAX_VERSIONS - 2)->c.num_planes == 5);
	CHECK(rsc_cicon_add_version(c, 6, col, mask, NULL, NULL) == RSC_MAX_VERSIONS - 1);
	CHECK(rsc_cicon_add_version(c, 6, col, mask, NULL, NULL) == -1);
	for (k = RSC_MAX_VERSIONS - 1; k >= 0; k--)
		CHECK(rsc_cicon_remove_version(c, k) == 0);
	CHECK(c->blk.mainlist == NULL && c->last == NULL && rsc_cicon_nversions(c) == 0);
	CHECK(rsc_cicon_add_version(c, 2, col, mask, NULL, NULL) == 0);
	CHECK(c->blk.mainlist == &c->last->c);
	rsc_free(r);					/* frees it with its versions */
	CHECK(rsc_test_live_blocks() == 0);
}

/* Fails the allocations of build_all and of single calls one by one: each
 * failure must leave a consistent model and leak nothing. */
static void test_alloc_failures(void)
{
	struct rsc	*r;
	OBJECT	*o;
	long	n, total;
	int	built = 0, ok = 1, cnt;

	/* how many allocations a full build makes */
	rsc_test_fail_alloc_after(-1);
	r = rsc_new();
	CHECK(build_all(r) == 0);
	rsc_free(r);
	total = rsc_test_alloc_count();
	CHECK(total > 30);
	CHECK(rsc_test_live_blocks() == 0);

	for (n = 0; n <= total; n++)
	{
		rsc_test_fail_alloc_after(n);
		r = rsc_new();
		if (!r)
		{
			ok &= n == 0;
			continue;
		}
		if (build_all(r) == 0)
			built++;
		rsc_test_fail_alloc_after(-1);
		ok &= all_trees_ok(r);
		rsc_free(r);
		ok &= rsc_test_live_blocks() == 0;
	}
	CHECK(ok);
	CHECK(built == 1);			/* only when nothing failed */

	/* a failed call changes nothing */
	r = rsc_new();
	CHECK(build_all(r) == 0);
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == -1);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "new") == -1);
	CHECK(rsc_obj_delete(r, 0, 10) == -1);
	CHECK(rsc_tree_add(r, 1) == -1);
	CHECK(rsc_frstr_add(r, "x") == -1);
	CHECK(rsc_frstr_set(r, 0, "x") == -1);
	CHECK(rsc_frimg_add(r, rsc_frimg(r, 0)) == -1);
	CHECK(rsc_obj_set_type(r, 0, 1, G_BUTTON) == 0);	/* allocates nothing */
	rsc_test_fail_alloc_after(-1);
	o = rsc_tree_objects(r, 0, &cnt);
	CHECK(cnt == 14 && rsc_ntrees(r) == 1 && rsc_nfrstr(r) == 2 && rsc_nfrimg(r) == 1);
	CHECK_STR(o[1].ob_spec.free_string, "A string");
	CHECK_STR(rsc_frstr(r, 0), "[1][An alert][ OK ]");
	CHECK_STR(walk(r, 0), "0 1 2 3 4 5 6 7 8 9 10 11 12 13");
	/* a record half copied when the memory runs out is not kept */
	for (n = 0; n < 4; n++)
	{
		TEDINFO	te = *o[4].ob_spec.tedinfo;
		TEDINFO	*old = o[4].ob_spec.tedinfo;

		rsc_test_fail_alloc_after(n);
		CHECK(rsc_obj_set_tedinfo(r, 0, 4, &te) == -1);
		rsc_test_fail_alloc_after(-1);
		CHECK(o[4].ob_spec.tedinfo == old);
	}
	for (n = 0; n < 5; n++)
	{
		struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 13);
		short	words[4 * 16] = { 0 };

		rsc_test_fail_alloc_after(n);
		CHECK(rsc_cicon_add_version(c, 4, words, words, words, words) == -1);
		rsc_test_fail_alloc_after(-1);
		CHECK(rsc_cicon_nversions(c) == 2);
	}
	CHECK(all_trees_ok(r));
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

/* n objects linked by hand: all links -1, then the edits of the test */
static void unlinked(OBJECT *o, int n)
{
	int	i;

	memset(o, 0, sizeof *o * (size_t)n);
	for (i = 0; i < n; i++)
		o[i].ob_next = o[i].ob_head = o[i].ob_tail = -1;
}

/* 0 { 1 { 3 4 } 2 }, 5 unreachable */
static void sample(OBJECT *o)
{
	unlinked(o, 6);
	o[0].ob_head = 1;
	o[0].ob_tail = 2;
	o[1].ob_next = 2;
	o[1].ob_head = 3;
	o[1].ob_tail = 4;
	o[3].ob_next = 4;
	o[4].ob_next = 1;
	o[2].ob_next = 0;
	o[5].ob_next = 77;	/* not looked at: unreachable */
}

static void test_preorder(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	o[6];
	int	order[6];

	CHECK(build_all(r) == 0);
	CHECK_STR(walk(r, 0), "0 1 2 3 4 5 6 7 8 9 10 11 12 13");
	rsc_free(r);

	sample(o);
	CHECK_STR(walk_objs(o, 6), "0 1 3 4 2");
	CHECK(rsc_tree_preorder(o, 6, NULL) == 5);
	o[0].ob_next = 1234;		/* the root's ob_next is ignored */
	CHECK_STR(walk_objs(o, 6), "0 1 3 4 2");
	CHECK(rsc_tree_preorder(o, 0, order) == RSC_WALK_EARG);
	CHECK(rsc_tree_preorder(NULL, 6, order) == RSC_WALK_EARG);
	unlinked(o, 1);
	o[0].ob_next = 3;
	CHECK(rsc_tree_preorder(o, 1, order) == 1 && order[0] == 0);	/* the root alone */
	o[0].ob_head = 1;
	CHECK(rsc_tree_preorder(o, 1, order) == RSC_WALK_ERANGE);	/* head past n */

	/* out of range */
	sample(o);
	o[1].ob_head = 6;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ERANGE);
	sample(o);
	o[1].ob_head = -2;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ERANGE);
	sample(o);
	o[3].ob_next = 32000;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ERANGE);
	sample(o);
	o[1].ob_tail = 99;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ERANGE);
	sample(o);
	o[2].ob_tail = 6;		/* a leaf with a bad ob_tail */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ERANGE);
	/* a cycle: a child's head is the root */
	sample(o);
	o[3].ob_head = o[3].ob_tail = 0;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ESHARED);
	/* a sibling chain looping on itself */
	sample(o);
	o[4].ob_next = 3;
	o[1].ob_tail = 5;		/* so that no ob_tail is in the loop */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ESHARED);
	/* one object under two parents: 3 is also the only child of 2 */
	sample(o);
	o[2].ob_head = o[2].ob_tail = 3;
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ESHARED);
	/* malformed chains */
	sample(o);
	o[1].ob_tail = 3;		/* ob_tail is not the last child */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ECHAIN);
	sample(o);
	o[4].ob_next = -1;		/* the chain does not return to the parent */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ECHAIN);
	sample(o);
	o[3].ob_next = 1;		/* back at the parent before its ob_tail */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ECHAIN);
	sample(o);
	o[4].ob_next = 0;		/* the chain returns to another object */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ECHAIN);
	sample(o);
	o[1].ob_tail = -1;		/* head without tail */
	CHECK(rsc_tree_preorder(o, 6, order) == RSC_WALK_ECHAIN);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_compact(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	char	*s5;
	TEDINFO	*te4;
	int	map[14], n, k;

	CHECK(build_all(r) == 0);
	o = rsc_tree_objects(r, 0, &n);
	te4 = o[4].ob_spec.tedinfo;
	s5 = o[5].ob_spec.tedinfo->te_ptext;
	CHECK(rsc_obj_delete(r, 0, 2) == 0);
	CHECK(rsc_obj_delete(r, 0, 10) == 0);	/* 10 with 11 and 12 */
	CHECK(rsc_obj_delete(r, 0, 13) == 0);	/* the last array element */
	/* failures leave the tree as it is */
	for (k = 0; k < 2; k++)
	{
		rsc_test_fail_alloc_after(k);
		CHECK(rsc_tree_compact(r, 0, map) == RSC_WALK_ENOMEM);
		rsc_test_fail_alloc_after(-1);
		o = rsc_tree_objects(r, 0, &n);
		CHECK(n == 14 && o[4].ob_spec.tedinfo == te4);
		CHECK_STR(walk(r, 0), "0 1 3 4 5 6 7 8 9");
	}
	CHECK(rsc_tree_compact(r, 5, NULL) == RSC_WALK_EARG);
	CHECK(rsc_tree_compact(r, 0, map) == 9);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 9);
	CHECK(tree_ok(r, 0));
	CHECK_STR(walk(r, 0), "0 1 2 3 4 5 6 7 8");
	CHECK(map[0] == 0 && map[1] == 1 && map[2] == -1 && map[3] == 2 && map[9] == 8);
	CHECK(map[10] == -1 && map[11] == -1 && map[12] == -1 && map[13] == -1);
	CHECK(o[map[4]].ob_spec.tedinfo == te4);		/* the records move along */
	CHECK(o[map[5]].ob_spec.tedinfo->te_ptext == s5);
	CHECK(o[8].ob_flags == OF_LASTOB && o[8].ob_type == G_IMAGE);
	CHECK(o[0].ob_head == 1 && o[0].ob_tail == 8 && o[8].ob_next == 0);
	/* the freed slots can be used again */
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == 9);
	CHECK(tree_ok(r, 0));
	/* compacting a compact tree changes nothing */
	CHECK(rsc_tree_compact(r, 0, NULL) == 10);
	CHECK_STR(walk(r, 0), "0 1 2 3 4 5 6 7 8 9");
	/* broken links: an error, the tree unchanged */
	o = rsc_tree_objects(r, 0, &n);
	o[3].ob_head = o[3].ob_tail = 0;
	CHECK(rsc_tree_compact(r, 0, NULL) == RSC_WALK_ESHARED);
	CHECK(rsc_tree_objects(r, 0, &n) == o && n == 10);
	o[3].ob_head = o[3].ob_tail = -1;
	/* unreachable objects of a file: the root's ob_next is reset too */
	o[0].ob_next = 5;
	CHECK(rsc_tree_compact(r, 0, NULL) == 10);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o[0].ob_next == -1);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_resize(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	ICONBLK	ib;
	BITBLK	bb;
	short	data[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };	/* 32x2: 2 words a row */
	short	*p;
	int	n, k;
	long	live;

	memset(&ib, 0, sizeof ib);
	memset(&bb, 0, sizeof bb);
	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_ICON) == 1);
	CHECK(rsc_obj_add(r, 0, 0, G_IMAGE) == 2);
	CHECK(rsc_obj_resize_icon(r, 0, 1, 16, 16) == -1);	/* no record */
	ib.ib_wicon = 32;
	ib.ib_hicon = 2;
	ib.ib_pdata = data;
	CHECK(rsc_obj_set_iconblk(r, 0, 1, &ib) == 0);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(rsc_obj_resize_icon(r, 0, 1, -1, 2) == -1);
	CHECK(rsc_obj_resize_icon(r, 0, 2, 16, 2) == -1);	/* not an icon */
	/* wider and taller: kept top left, zero elsewhere; NULL mask stays NULL */
	CHECK(rsc_obj_resize_icon(r, 0, 1, 48, 3) == 0);
	p = o[1].ob_spec.iconblk->ib_pdata;
	CHECK(o[1].ob_spec.iconblk->ib_wicon == 48 && o[1].ob_spec.iconblk->ib_hicon == 3);
	CHECK(o[1].ob_spec.iconblk->ib_pmask == NULL);
	CHECK(p[0] == 0x1111 && p[1] == 0x2222 && p[2] == 0 && p[3] == 0x3333 && p[4] == 0x4444);
	CHECK(p[5] == 0 && p[6] == 0 && p[7] == 0 && p[8] == 0);
	/* narrower */
	CHECK(rsc_obj_resize_icon(r, 0, 1, 1, 2) == 0);
	p = o[1].ob_spec.iconblk->ib_pdata;
	CHECK(p[0] == 0x1111 && p[1] == 0x3333);
	/* failures change nothing: an icon with mask and data, two allocations */
	ib.ib_wicon = 32;
	ib.ib_hicon = 2;
	ib.ib_pmask = ib.ib_pdata = data;
	CHECK(rsc_obj_set_iconblk(r, 0, 1, &ib) == 0);
	for (k = 0; k < 2; k++)
	{
		ICONBLK	*old = o[1].ob_spec.iconblk;
		short	*od = old->ib_pdata, *om = old->ib_pmask;

		live = rsc_test_live_blocks();
		rsc_test_fail_alloc_after(k);
		CHECK(rsc_obj_resize_icon(r, 0, 1, 16, 8) == -1);
		rsc_test_fail_alloc_after(-1);
		CHECK(old->ib_pdata == od && old->ib_pmask == om);
		CHECK(old->ib_wicon == 32 && old->ib_hicon == 2);
		CHECK(rsc_test_live_blocks() == live);
	}
	CHECK(rsc_obj_resize_icon(r, 0, 1, 16, 8) == 0);
	p = o[1].ob_spec.iconblk->ib_pmask;
	CHECK(p[0] == 0x1111 && p[1] == 0x3333 && p[2] == 0);
	/* BITBLK and free image */
	bb.bi_pdata = data;
	bb.bi_wb = 4;
	bb.bi_hl = 2;
	CHECK(rsc_obj_set_bitblk(r, 0, 2, &bb) == 0);
	CHECK(rsc_obj_resize_bitblk(r, 0, 2, 2, 3) == 0);
	p = o[2].ob_spec.bitblk->bi_pdata;
	CHECK(o[2].ob_spec.bitblk->bi_wb == 2 && o[2].ob_spec.bitblk->bi_hl == 3);
	CHECK(p[0] == 0x1111 && p[1] == 0x3333 && p[2] == 0);
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_obj_resize_bitblk(r, 0, 2, 8, 8) == -1);
	rsc_test_fail_alloc_after(-1);
	CHECK(o[2].ob_spec.bitblk->bi_pdata == p && o[2].ob_spec.bitblk->bi_wb == 2);
	CHECK(rsc_obj_resize_bitblk(r, 0, 2, 2, 40000) == -1);
	CHECK(rsc_frimg_add(r, &bb) == 0);
	CHECK(rsc_frimg_resize(r, 0, 6, 1) == 0);
	p = rsc_frimg(r, 0)->bi_pdata;
	CHECK(p[0] == 0x1111 && p[1] == 0x2222 && p[2] == 0);
	CHECK(rsc_frimg_resize(r, 1, 6, 1) == -1);
	bb.bi_pdata = NULL;
	CHECK(rsc_frimg_add(r, &bb) == 1);
	CHECK(rsc_frimg_resize(r, 1, 6, 6) == 0 && rsc_frimg(r, 1)->bi_pdata == NULL);
	CHECK(rsc_bitblk_bytes(rsc_frimg(r, 1)) == 36);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_indirect(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o;
	TEDINFO	te;
	uint32_t	v = 1;
	int	n;

	memset(&te, 0, sizeof te);
	te.te_ptext = "x";
	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_TEXT) == 1);
	CHECK(rsc_obj_set_tedinfo(r, 0, 1, &te) == 0);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(rsc_obj_spec_kind(&o[1]) == RSC_SPEC_TEDINFO);
	/* setting OF_INDIRECT releases the TEDINFO: the spec is a value now */
	CHECK(rsc_obj_set_flags(r, 0, 1, OF_INDIRECT | OF_EDITABLE) == 0);
	CHECK(rsc_obj_spec_kind(&o[1]) == RSC_SPEC_VALUE && o[1].ob_spec.index == 0);
	CHECK(rsc_obj_set_tedinfo(r, 0, 1, &te) == -1);
	CHECK(rsc_obj_set_value(r, 0, 1, 0x12345678UL) == 0);
	CHECK(rsc_obj_get_value(r, 0, 1, &v) == 0 && v == 0x12345678UL);
	/* other flags and type changes keep the value */
	CHECK(rsc_obj_set_flags(r, 0, 1, OF_INDIRECT) == 0);
	CHECK(rsc_obj_set_type(r, 0, 1, G_STRING) == 0);
	CHECK(rsc_obj_value(r, 0, 1) == 0x12345678UL);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "no") == -1);
	/* clearing it empties the spec: the value is no pointer */
	CHECK(rsc_obj_set_flags(r, 0, 1, 0) == 0);
	CHECK(o[1].ob_spec.free_string == NULL);
	CHECK(rsc_obj_get_value(r, 0, 1, &v) == -1);
	CHECK(rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "yes") == 0);
	/* a value-kind type is not affected by the flag */
	CHECK(rsc_obj_set_type(r, 0, 1, G_BOX) == 0 && rsc_obj_set_value(r, 0, 1, 7) == 0);
	CHECK(rsc_obj_set_flags(r, 0, 1, OF_INDIRECT) == 0 && rsc_obj_value(r, 0, 1) == 7);
	CHECK(rsc_obj_set_flags(r, 0, 1, 0) == 0 && rsc_obj_value(r, 0, 1) == 7);
	CHECK(rsc_obj_get_value(r, 0, 2, &v) == -1);
	/* an indirect object is freed without touching its value */
	CHECK(rsc_obj_add(r, 0, 0, G_ICON) == 2);
	CHECK(rsc_obj_set_flags(r, 0, 2, OF_INDIRECT) == 0);
	CHECK(rsc_obj_set_value(r, 0, 2, 0xdeadbeefUL) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_CICON) == 3);
	CHECK(rsc_obj_set_flags(r, 0, 3, OF_INDIRECT) == 0);
	CHECK(rsc_obj_set_value(r, 0, 3, 0x1UL) == 0);
	CHECK(rsc_obj_cicon(r, 0, 3) == NULL);
	CHECK(rsc_obj_delete(r, 0, 3) == 0);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_planes(void)
{
	struct rsc	*r = rsc_new();
	ICONBLK	ib;
	struct rsc_cicon	*c;
	short	col[RSC_MAX_PLANES * 16], mask[16];

	memset(&ib, 0, sizeof ib);
	memset(col, 0, sizeof col);
	memset(mask, 0, sizeof mask);
	ib.ib_wicon = ib.ib_hicon = 16;
	CHECK(rsc_tree_add(r, 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_CICON) == 1);
	CHECK(rsc_obj_set_cicon(r, 0, 1, &ib, NULL) == 0);
	c = rsc_obj_cicon(r, 0, 1);
	CHECK(RSC_MAX_PLANES == 16);
	CHECK(rsc_cicon_add_version(c, 0, col, mask, NULL, NULL) == -1);
	CHECK(rsc_cicon_add_version(c, 1, col, mask, NULL, NULL) == 0);
	CHECK(rsc_cicon_add_version(c, 16, col, mask, col, mask) == 1);
	CHECK(rsc_cicon_add_version(c, 17, col, mask, NULL, NULL) == -1);
	CHECK(rsc_cicon_nversions(c) == 2);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

/* ---- failure injection for calls that replace something ------------------------ */

/* The ops run on a build_all resource. */
static int op_iconblk(struct rsc *r)
{
	OBJECT	*o = rsc_tree_objects(r, 0, NULL);
	ICONBLK	ib = *o[8].ob_spec.iconblk;

	return rsc_obj_set_iconblk(r, 0, 8, &ib);
}

static int op_bitblk(struct rsc *r)
{
	OBJECT	*o = rsc_tree_objects(r, 0, NULL);
	BITBLK	bb = *o[9].ob_spec.bitblk;

	return rsc_obj_set_bitblk(r, 0, 9, &bb);
}

static int op_cicon(struct rsc *r)
{
	return rsc_obj_set_cicon(r, 0, 13, &rsc_obj_cicon(r, 0, 13)->blk.monoblk, "new");
}

static int op_str(struct rsc *r)
{
	return rsc_obj_set_str(r, 0, 1, RSC_STR_SPEC, "replaced");
}

static int op_te_str(struct rsc *r)
{
	return rsc_obj_set_str(r, 0, 4, RSC_STR_TE_VALID, "replaced");
}

static int op_ib_str(struct rsc *r)
{
	return rsc_obj_set_str(r, 0, 8, RSC_STR_IB_TEXT, "replaced");
}

static int op_frstr(struct rsc *r)
{
	return rsc_frstr_set(r, 1, "replaced");
}

static int op_version(struct rsc *r)
{
	static short	words[16 * 16];

	return rsc_cicon_add_version(rsc_obj_cicon(r, 0, 13), 16, words, words, words, words) < 0 ? -1 : 0;
}

static int op_tedinfo(struct rsc *r)
{
	OBJECT	*o = rsc_tree_objects(r, 0, NULL);
	TEDINFO	te = *o[4].ob_spec.tedinfo;

	return rsc_obj_set_tedinfo(r, 0, 4, &te);
}

/* A fingerprint of everything op_* may change. */
static unsigned long state(struct rsc *r)
{
	OBJECT	*o = rsc_tree_objects(r, 0, NULL);
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 13);
	unsigned long	h = 0;

	h = h * 31 + (unsigned long)(uintptr_t)o[1].ob_spec.free_string;
	h = h * 31 + (unsigned long)(uintptr_t)o[4].ob_spec.tedinfo;
	h = h * 31 + (unsigned long)(uintptr_t)o[4].ob_spec.tedinfo->te_pvalid;
	h = h * 31 + (unsigned long)(uintptr_t)o[8].ob_spec.iconblk;
	h = h * 31 + (unsigned long)(uintptr_t)o[8].ob_spec.iconblk->ib_ptext;
	h = h * 31 + (unsigned long)(uintptr_t)o[9].ob_spec.bitblk;
	h = h * 31 + (unsigned long)(uintptr_t)c;
	h = h * 31 + (unsigned long)rsc_cicon_nversions(c);
	h = h * 31 + (unsigned long)(uintptr_t)rsc_frstr(r, 1);
	return h;
}

/* Fails the n-th allocation of op for n = 0, 1, ... until op succeeds:
 * every failure must leave the model and the live block count as they
 * were.  Returns the number of failures seen, -1 on a broken promise. */
static int fail_each(int (*op)(struct rsc *))
{
	struct rsc	*r = rsc_new();
	unsigned long	before;
	long	live, n;
	int	fails = 0, rc;

	if (!r || build_all(r))
		return -1;
	for (n = 0; n < 100; n++)
	{
		before = state(r);
		live = rsc_test_live_blocks();
		rsc_test_fail_alloc_after(n);
		rc = op(r);
		rsc_test_fail_alloc_after(-1);
		if (rc == 0)
			break;
		if (state(r) != before || rsc_test_live_blocks() != live || !all_trees_ok(r))
		{
			fails = -1;
			break;
		}
		fails++;
	}
	rsc_free(r);
	return n == 100 ? -1 : fails;
}

static void test_replace_failures(void)
{
	CHECK(fail_each(op_iconblk) == 4);	/* record, mask, data, text */
	CHECK(fail_each(op_bitblk) == 2);
	CHECK(fail_each(op_cicon) == 3);
	CHECK(fail_each(op_str) == 1);
	CHECK(fail_each(op_te_str) == 1);
	CHECK(fail_each(op_ib_str) == 1);
	CHECK(fail_each(op_frstr) == 1);
	CHECK(fail_each(op_version) == 5);
	CHECK(fail_each(op_tedinfo) == 4);
	CHECK(rsc_test_live_blocks() == 0);
}

static void test_limits_and_broken_links(void)
{
	struct rsc	*r = rsc_new();
	OBJECT	*o, *old;
	int	n;

	/* RSC_MAX_OBJECTS through rsc_obj_add */
	CHECK(rsc_tree_add(r, RSC_MAX_OBJECTS - 1) == 0);
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == RSC_MAX_OBJECTS - 1);
	old = rsc_tree_objects(r, 0, &n);
	CHECK(n == RSC_MAX_OBJECTS && old[0].ob_head == RSC_MAX_OBJECTS - 1);
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == -1);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(o == old && n == RSC_MAX_OBJECTS && o[0].ob_tail == RSC_MAX_OBJECTS - 1);
	CHECK(o[n - 1].ob_flags == OF_LASTOB && o[n - 1].ob_next == 0);
	CHECK(rsc_tree_add(r, RSC_MAX_OBJECTS) == 1);
	CHECK(rsc_tree_delete(r, 1) == 0);
	CHECK(rsc_tree_delete(r, 0) == 0);

	/* hand-linked trees with links out of range */
	CHECK(rsc_tree_add(r, 3) == 0);
	o = rsc_tree_objects(r, 0, &n);
	o[0].ob_head = 1;
	o[0].ob_tail = 7;
	o[1].ob_next = 2;
	o[2].ob_next = 0;
	CHECK(rsc_obj_add(r, 0, 0, G_BOX) == -1);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 3 && o[0].ob_tail == 7 && o[2].ob_next == 0);
	o[0].ob_tail = 2;
	CHECK(tree_ok(r, 0));
	o[1].ob_head = 9;		/* head without tail, and out of range */
	CHECK(rsc_obj_add(r, 0, 1, G_BOX) == -1);
	CHECK(rsc_obj_delete(r, 0, 1) == -1);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 3 && o[0].ob_head == 1 && o[1].ob_next == 2 && o[1].ob_head == 9);
	o[1].ob_tail = 9;
	CHECK(rsc_obj_add(r, 0, 1, G_BOX) == -1);
	CHECK(rsc_obj_delete(r, 0, 1) == -1);
	CHECK(rsc_tree_compact(r, 0, NULL) == RSC_WALK_ERANGE);
	o = rsc_tree_objects(r, 0, &n);
	CHECK(n == 3 && o[0].ob_head == 1 && o[1].ob_head == 9 && o[1].ob_tail == 9);
	o[2].ob_next = 40;		/* a broken chain: the parent of 2 cannot be found */
	o[1].ob_head = o[1].ob_tail = -1;
	CHECK(rsc_obj_delete(r, 0, 1) == -1);
	CHECK(o[0].ob_head == 1 && o[1].ob_next == 2);
	rsc_free(r);
	CHECK(rsc_test_live_blocks() == 0);
}

int main(void)
{
	test_new_free();
	test_raw_tree();
	test_build_all();
	test_links();
	test_type_change();
	test_strings();
	test_free_lists();
	test_cicon_versions();
	test_alloc_failures();
	test_preorder();
	test_compact();
	test_resize();
	test_indirect();
	test_planes();
	test_replace_failures();
	test_limits_and_broken_links();
	printf("rsc-test: %ld checks, %ld failed\n", checks, failures);
	return failures ? 1 : 0;
}
