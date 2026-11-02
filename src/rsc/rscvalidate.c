/* RSCVALIDATE.C	rsc_validate (rscfile.h): the invariants the writer relies
 *			on, checked before a write so that a model edited
 *			through the public arrays is reported with a position
 *			instead of failing inside the writer
 *
 * What is checked, for every tree (objects the root does not reach are not
 * looked at, as the writer does not look at them):
 *  - the tree has 1 to RSC_MAX_OBJECTS objects, its links are those of an
 *    AES tree (rsc_tree_preorder: RSC_ELINK, RSC_ECYCLE), and OF_LASTOB is
 *    on the last object of the array and on no other (RSC_ELINK);
 *  - the coordinates have a file form: with a cell size, -127 to
 *    255 * cell + 128 (RSC_ETOOBIG);
 *  - by the kind of spec: a TEDINFO text or template of 32767 characters or
 *    more (RSC_ETOOBIG); a negative size of an ICONBLK or BITBLK
 *    (RSC_EARG); of a colour icon, its sizes against the blocks the model
 *    allocated (RSC_EARG), ib_ptext and text[12], the number of versions
 *    against the chain, whether the chain loops or has more than
 *    RSC_MAX_VERSIONS versions, the plane count of each version
 *    (RSC_ECICON) and its data size (RSC_EARG);
 *  - free strings are not NULL, free images are not NULL and have no
 *    negative size;
 *  - at most 65535 trees, free strings and free images, and a cell size
 *    that is 0, 0 or 1..RSC_MAX_CELL both.
 * What is not checked: the size of the written file (the 65535 byte limit
 * of the classic part is only found by writing).
 */
#include "rscatari.h"

/* Reports "what (where k, object i)" at offset i (k and i < 0 are left
 * out); returns -1. */
static int bad(struct rsc_error *e, int code, const char *what, const char *where, int k, int i)
{
	size_t	n = 0;

	if (!e)
		return -1;
	e->code = code;
	e->offset = i >= 0 ? (size_t)i : k >= 0 ? (size_t)k : 0;
	rsc_msg_app(e->msg, &n, what);
	if (where)
	{
		rsc_msg_app(e->msg, &n, " (");
		rsc_msg_app(e->msg, &n, where);
		if (k >= 0)
		{
			rsc_msg_app(e->msg, &n, " ");
			rsc_msg_num(e->msg, &n, (size_t)k, 0);
		}
		if (i >= 0)
		{
			rsc_msg_app(e->msg, &n, ", object ");
			rsc_msg_num(e->msg, &n, (size_t)i, 0);
		}
		rsc_msg_app(e->msg, &n, ")");
	}
	return -1;
}

/* The colour icon c of object i of tree t. */
static int check_cicon(struct rsc_error *e, const struct rsc_cicon *c, int t, int i)
{
	const ICONBLK	*ib = &c->blk.monoblk;
	const CICON	*v, *tail = NULL, *slow, *fast;
	size_t	pb;
	int	k = 0;

	if (ib->ib_wicon < 0 || ib->ib_hicon < 0)
		return bad(e, RSC_EARG, "negative colour icon size", "tree", t, i);
	slow = fast = c->blk.mainlist;
	while (fast && fast->next_res)
	{
		fast = fast->next_res->next_res;
		slow = slow->next_res;
		if (fast && fast == slow)
			return bad(e, RSC_ECICON, "colour version chain loops", "tree", t, i);
	}
	for (v = c->blk.mainlist; v; v = v->next_res)
	{
		if (++k > RSC_MAX_VERSIONS)
			return bad(e, RSC_ECICON, "too many colour versions", "tree", t, i);
		tail = v;
	}
	pb = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);
	if (pb != c->mono_bytes)
		return bad(e, RSC_EARG, "colour icon size does not match its data", "tree", t, i);
	if (k != c->nver || (c->last ? &c->last->c : NULL) != tail)
		return bad(e, RSC_EARG, "colour version count does not match the chain", "tree", t, i);
	if (ib->ib_ptext != c->text || c->text[sizeof c->text - 1] != '\0')
		return bad(e, RSC_EARG, "colour icon text is not a C string", "tree", t, i);
	for (v = c->blk.mainlist; v; v = v->next_res)
	{
		if (v->num_planes < 1 || v->num_planes > RSC_MAX_PLANES)
			return bad(e, RSC_ECICON, "bad number of colour planes", "tree", t, i);
		if (pb * (size_t)v->num_planes != ((const struct rsc_cicon_version *)v)->data_bytes)
			return bad(e, RSC_EARG, "num_planes does not match the colour data", "tree", t, i);
	}
	return 0;
}

static int check_coord(struct rsc_error *e, int v, int cell, const char *f, int t, int i)
{
	if (cell && (v < -127 || v > 255 * cell + 128))
		return bad(e, RSC_ETOOBIG, f, "tree", t, i);
	return 0;
}

/* Object o, number i of tree t (a cell size of cell_w, cell_h). */
static int check_object(struct rsc_error *e, const struct rsc *r, const OBJECT *o, int t, int i)
{
	int	cw, ch;
	const TEDINFO	*te;
	const ICONBLK	*ib;
	const BITBLK	*bb;

	rsc_cell(r, &cw, &ch);
	if (check_coord(e, o->ob_x, cw, "ob_x out of range", t, i)
	    || check_coord(e, o->ob_y, ch, "ob_y out of range", t, i)
	    || check_coord(e, o->ob_width, cw, "ob_width out of range", t, i)
	    || check_coord(e, o->ob_height, ch, "ob_height out of range", t, i))
		return -1;
	switch (rsc_obj_spec_kind(o))
	{
	case RSC_SPEC_TEDINFO:
		te = o->ob_spec.tedinfo;
		if (te && te->te_ptext && strlen(te->te_ptext) >= 32767)
			return bad(e, RSC_ETOOBIG, "TEDINFO text too long", "tree", t, i);
		if (te && te->te_ptmplt && strlen(te->te_ptmplt) >= 32767)
			return bad(e, RSC_ETOOBIG, "TEDINFO template too long", "tree", t, i);
		break;
	case RSC_SPEC_ICONBLK:
		ib = o->ob_spec.iconblk;
		if (ib && (ib->ib_wicon < 0 || ib->ib_hicon < 0))
			return bad(e, RSC_EARG, "negative icon size", "tree", t, i);
		break;
	case RSC_SPEC_BITBLK:
		bb = o->ob_spec.bitblk;
		if (bb && (bb->bi_wb < 0 || bb->bi_hl < 0))
			return bad(e, RSC_EARG, "negative image size", "tree", t, i);
		break;
	case RSC_SPEC_CICON:
		if (o->ob_spec.ciconblk)
			return check_cicon(e, (const struct rsc_cicon *)(const void *)o->ob_spec.ciconblk,
					   t, i);
		break;
	default:
		break;
	}
	return 0;
}

static int check_tree(struct rsc_error *e, const struct rsc *r, int t)
{
	const OBJECT	*o;
	int	*order, n, cnt, k;

	o = rsc_tree_objects(r, t, &n);
	if (!o || n < 1 || n > RSC_MAX_OBJECTS)
		return bad(e, RSC_ELINK, "empty tree", "tree", t, -1);
	for (k = 0; k < n; k++)
		if (!(o[k].ob_flags & OF_LASTOB) != (k != n - 1))
			return bad(e, RSC_ELINK, "OF_LASTOB is not on the last object only", "tree", t, k);
	order = rsc_malloc((size_t)n * sizeof *order);
	if (!order)
		return bad(e, RSC_ENOMEM, "out of memory", NULL, -1, -1);
	cnt = rsc_tree_preorder(o, n, order);
	switch (cnt)
	{
	case RSC_WALK_ENOMEM:
		rsc_mfree(order);
		return bad(e, RSC_ENOMEM, "out of memory", NULL, -1, -1);
	case RSC_WALK_ESHARED:
		rsc_mfree(order);
		return bad(e, RSC_ECYCLE, "tree links form a cycle or share an object", "tree", t, -1);
	case RSC_WALK_ECHAIN:
		rsc_mfree(order);
		return bad(e, RSC_ELINK, "malformed sibling chain", "tree", t, -1);
	default:
		if (cnt < 1)
		{
			rsc_mfree(order);
			return bad(e, RSC_ELINK, "tree link out of range", "tree", t, -1);
		}
	}
	for (k = 0; k < cnt; k++)
		if (check_object(e, r, &o[order[k]], t, order[k]))
		{
			rsc_mfree(order);
			return -1;
		}
	rsc_mfree(order);
	return 0;
}

int rsc_validate(const struct rsc *r, struct rsc_error *err)
{
	const BITBLK	*bb;
	int	cw, ch, t, k;

	if (err)
	{
		err->code = RSC_OK;
		err->offset = 0;
		err->msg[0] = '\0';
	}
	if (!r)
		return bad(err, RSC_EARG, "no resource", NULL, -1, -1);
	rsc_cell(r, &cw, &ch);
	if (cw < 0 || ch < 0 || cw > RSC_MAX_CELL || ch > RSC_MAX_CELL || (cw == 0) != (ch == 0))
		return bad(err, RSC_EARG, "bad cell size", NULL, -1, -1);
	if (rsc_ntrees(r) > 65535)
		return bad(err, RSC_ETOOBIG, "too many trees", NULL, -1, -1);
	if (rsc_nfrstr(r) > 65535)
		return bad(err, RSC_ETOOBIG, "too many free strings", NULL, -1, -1);
	if (rsc_nfrimg(r) > 65535)
		return bad(err, RSC_ETOOBIG, "too many free images", NULL, -1, -1);
	for (t = 0; t < rsc_ntrees(r); t++)
		if (check_tree(err, r, t))
			return -1;
	for (k = 0; k < rsc_nfrstr(r); k++)
		if (!rsc_frstr(r, k))
			return bad(err, RSC_EARG, "free string is NULL", "free string", k, -1);
	for (k = 0; k < rsc_nfrimg(r); k++)
	{
		bb = rsc_frimg(r, k);
		if (!bb)
			return bad(err, RSC_EARG, "free image is NULL", "free image", k, -1);
		if (bb->bi_wb < 0 || bb->bi_hl < 0)
			return bad(err, RSC_EARG, "negative image size", "free image", k, -1);
	}
	return 0;
}
