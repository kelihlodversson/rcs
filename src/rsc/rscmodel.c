/* RSCMODEL.C	a resource file in memory: the trees, the free strings and
 *		images, and the editing API (see rscmodel.h) */
#include <stdlib.h>
#include <string.h>
#include "rscmodel.h"

struct rsc_tree
{
	OBJECT	*obj;
	int	nobj;
};

struct rsc
{
	struct rsc_tree	*tree;
	int	ntree;
	char	**frstr;
	int	nfrstr;
	BITBLK	**frimg;
	int	nfrimg;
	unsigned	vrsn;
	int	cell_w, cell_h;
};

/* ---- allocation ---------------------------------------------------------- */

#ifdef RSC_TEST
static long fail_after = -1;
static long alloc_count;
static long live;

void rsc_test_fail_alloc_after(long n)
{
	fail_after = n;
	alloc_count = 0;
}

long rsc_test_alloc_count(void)
{
	return alloc_count;
}

long rsc_test_live_blocks(void)
{
	return live;
}

static int alloc_fails(void)
{
	alloc_count++;
	if (fail_after < 0)
		return 0;
	if (fail_after == 0)
		return 1;
	fail_after--;
	return 0;
}
#define LIVE(d)		(live += (d))
#else
#define alloc_fails()	0
#define LIVE(d)		((void)0)
#endif

void *rsc_malloc(size_t n)
{
	void	*p;

	if (alloc_fails())
		return NULL;
	p = malloc(n ? n : 1);
	if (p)
		LIVE(1);
	return p;
}

void *rsc_realloc(void *p, size_t n)
{
	void	*q;

	if (alloc_fails())
		return NULL;
	q = realloc(p, n ? n : 1);
	if (q && !p)
		LIVE(1);
	return q;
}

void rsc_mfree(void *p)
{
	if (p)
	{
		LIVE(-1);
		free(p);
	}
}

char *rsc_strdup(const char *s)
{
	size_t	n;
	char	*d;

	if (!s)
		return NULL;
	n = strlen(s) + 1;
	d = rsc_malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

/* *dst := a copy of the n bytes at src, or NULL when src is NULL.
 * Returns 0, or -1 when the allocation failed. */
static int dup_mem(void *dst, const void *src, size_t n)
{
	void	*p = NULL;

	if (src)
	{
		p = rsc_malloc(n);
		if (!p)
			return -1;
		memcpy(p, src, n);
	}
	memcpy(dst, &p, sizeof p);
	return 0;
}

/* *dst := a copy of the string s, or NULL when s is NULL. */
static int dup_str(char **dst, const char *s)
{
	*dst = NULL;
	if (s && !(*dst = rsc_strdup(s)))
		return -1;
	return 0;
}

/* ---- sizes and kinds ----------------------------------------------------- */

size_t rsc_plane_bytes(int w, int h)
{
	if (w < 0 || h < 0)
		return 0;
	return (((size_t)w + 15) / 16) * 2 * (size_t)h;
}

size_t rsc_bitblk_bytes(const BITBLK *bb)
{
	if (bb->bi_wb < 0 || bb->bi_hl < 0)
		return 0;
	return (size_t)bb->bi_wb * (size_t)bb->bi_hl;
}

enum rsc_spec_kind rsc_spec_kind(unsigned ob_type)
{
	switch (ob_type & 0xff)
	{
	case G_BUTTON:
	case G_STRING:
	case G_TITLE:
		return RSC_SPEC_STRING;
	case G_TEXT:
	case G_BOXTEXT:
	case G_FTEXT:
	case G_FBOXTEXT:
		return RSC_SPEC_TEDINFO;
	case G_ICON:
		return RSC_SPEC_ICONBLK;
	case G_IMAGE:
		return RSC_SPEC_BITBLK;
	case G_CICON:
		return RSC_SPEC_CICON;
	default:
		return RSC_SPEC_VALUE;
	}
}

/* The kind of spec of object o: that of its type, but a plain value when
 * OF_INDIRECT is set (the model never owns what an indirect spec points to). */
enum rsc_spec_kind rsc_obj_spec_kind(const OBJECT *o)
{
	return o->ob_flags & OF_INDIRECT ? RSC_SPEC_VALUE : rsc_spec_kind(o->ob_type);
}

/* ---- records: free and copy ---------------------------------------------- */

static void free_tedinfo(TEDINFO *te)
{
	if (!te)
		return;
	rsc_mfree(te->te_ptext);
	rsc_mfree(te->te_ptmplt);
	rsc_mfree(te->te_pvalid);
	rsc_mfree(te);
}

static void free_iconblk(ICONBLK *ib)
{
	if (!ib)
		return;
	rsc_mfree(ib->ib_pmask);
	rsc_mfree(ib->ib_pdata);
	rsc_mfree(ib->ib_ptext);
	rsc_mfree(ib);
}

static void free_bitblk(BITBLK *bb)
{
	if (!bb)
		return;
	rsc_mfree(bb->bi_pdata);
	rsc_mfree(bb);
}

static void free_version(struct rsc_cicon_version *v)
{
	rsc_mfree(v->c.col_data);
	rsc_mfree(v->c.col_mask);
	rsc_mfree(v->c.sel_data);
	rsc_mfree(v->c.sel_mask);
	rsc_mfree(v);
}

static void free_cicon(struct rsc_cicon *c)
{
	CICON	*p, *next;

	if (!c)
		return;
	for (p = c->blk.mainlist; p; p = next)
	{
		next = p->next_res;
		free_version((struct rsc_cicon_version *)p);
	}
	rsc_mfree(c->blk.monoblk.ib_pmask);
	rsc_mfree(c->blk.monoblk.ib_pdata);
	rsc_mfree(c);
}

/* Releases what the spec of o points to, by the kind of its type, and
 * empties the spec.  The one place that knows what an object owns. */
static void spec_release(OBJECT *o)
{
	switch (rsc_obj_spec_kind(o))
	{
	case RSC_SPEC_STRING:
		rsc_mfree(o->ob_spec.free_string);
		break;
	case RSC_SPEC_TEDINFO:
		free_tedinfo(o->ob_spec.tedinfo);
		break;
	case RSC_SPEC_ICONBLK:
		free_iconblk(o->ob_spec.iconblk);
		break;
	case RSC_SPEC_BITBLK:
		free_bitblk(o->ob_spec.bitblk);
		break;
	case RSC_SPEC_CICON:
		free_cicon((struct rsc_cicon *)o->ob_spec.ciconblk);
		break;
	case RSC_SPEC_VALUE:
		break;
	}
	memset(&o->ob_spec, 0, sizeof o->ob_spec);
}

static TEDINFO *copy_tedinfo(const TEDINFO *src)
{
	TEDINFO	*te = rsc_malloc(sizeof *te);

	if (!te)
		return NULL;
	*te = *src;
	te->te_ptext = te->te_ptmplt = te->te_pvalid = NULL;
	if (dup_str(&te->te_ptext, src->te_ptext) || dup_str(&te->te_ptmplt, src->te_ptmplt)
	    || dup_str(&te->te_pvalid, src->te_pvalid))
	{
		free_tedinfo(te);
		return NULL;
	}
	return te;
}

static ICONBLK *copy_iconblk(const ICONBLK *src)
{
	ICONBLK	*ib = rsc_malloc(sizeof *ib);
	size_t	n = rsc_plane_bytes(src->ib_wicon, src->ib_hicon);

	if (!ib)
		return NULL;
	*ib = *src;
	ib->ib_pmask = ib->ib_pdata = NULL;
	ib->ib_ptext = NULL;
	if (dup_mem(&ib->ib_pmask, src->ib_pmask, n) || dup_mem(&ib->ib_pdata, src->ib_pdata, n)
	    || dup_str(&ib->ib_ptext, src->ib_ptext))
	{
		free_iconblk(ib);
		return NULL;
	}
	return ib;
}

static BITBLK *copy_bitblk(const BITBLK *src)
{
	BITBLK	*bb = rsc_malloc(sizeof *bb);

	if (!bb)
		return NULL;
	*bb = *src;
	bb->bi_pdata = NULL;
	if (dup_mem(&bb->bi_pdata, src->bi_pdata, rsc_bitblk_bytes(src)))
	{
		free_bitblk(bb);
		return NULL;
	}
	return bb;
}

/* Copies text (at most 12 characters, NULL for none) into c->text. */
static int set_cicon_text(struct rsc_cicon *c, const char *text)
{
	size_t	n = text ? strlen(text) : 0;

	if (n > sizeof c->text - 1)
		return -1;
	memset(c->text, 0, sizeof c->text);
	if (n)
		memcpy(c->text, text, n);
	return 0;
}

static struct rsc_cicon *new_cicon(const ICONBLK *mono, const char *text)
{
	struct rsc_cicon	*c;
	ICONBLK	*ib;
	size_t	n = rsc_plane_bytes(mono->ib_wicon, mono->ib_hicon);

	if (text && strlen(text) > sizeof c->text - 1)
		return NULL;
	c = rsc_malloc(sizeof *c);
	if (!c)
		return NULL;
	memset(c, 0, sizeof *c);
	ib = &c->blk.monoblk;
	*ib = *mono;
	ib->ib_pmask = ib->ib_pdata = NULL;
	ib->ib_ptext = c->text;
	c->blk.mainlist = NULL;
	if (dup_mem(&ib->ib_pmask, mono->ib_pmask, n) || dup_mem(&ib->ib_pdata, mono->ib_pdata, n))
	{
		free_cicon(c);
		return NULL;
	}
	set_cicon_text(c, text);
	return c;
}

/* ---- the resource -------------------------------------------------------- */

struct rsc *rsc_new(void)
{
	struct rsc	*r = rsc_malloc(sizeof *r);

	if (r)
		memset(r, 0, sizeof *r);
	return r;
}

static void free_tree(struct rsc_tree *tr)
{
	int	i;

	for (i = 0; i < tr->nobj; i++)
		spec_release(&tr->obj[i]);
	rsc_mfree(tr->obj);
	tr->obj = NULL;
	tr->nobj = 0;
}

void rsc_free(struct rsc *r)
{
	int	k;

	if (!r)
		return;
	for (k = 0; k < r->ntree; k++)
		free_tree(&r->tree[k]);
	rsc_mfree(r->tree);
	for (k = 0; k < r->nfrstr; k++)
		rsc_mfree(r->frstr[k]);
	rsc_mfree(r->frstr);
	for (k = 0; k < r->nfrimg; k++)
		free_bitblk(r->frimg[k]);
	rsc_mfree(r->frimg);
	rsc_mfree(r);
}

unsigned rsc_vrsn(const struct rsc *r)
{
	return r->vrsn;
}

void rsc_set_vrsn(struct rsc *r, unsigned vrsn)
{
	r->vrsn = vrsn & 0xffffU & ~(unsigned)RSC_VRSN_FORMAT_BITS;
}

void rsc_cell(const struct rsc *r, int *cell_w, int *cell_h)
{
	if (cell_w)
		*cell_w = r->cell_w;
	if (cell_h)
		*cell_h = r->cell_h;
}

int rsc_set_cell(struct rsc *r, int cell_w, int cell_h)
{
	if (cell_w < 0 || cell_h < 0 || (cell_w == 0) != (cell_h == 0))
		return -1;
	r->cell_w = cell_w;
	r->cell_h = cell_h;
	return 0;
}

/* ---- trees ---------------------------------------------------------------- */

int rsc_ntrees(const struct rsc *r)
{
	return r->ntree;
}

static void init_object(OBJECT *o)
{
	memset(o, 0, sizeof *o);
	o->ob_next = o->ob_head = o->ob_tail = -1;
	o->ob_type = G_IBOX;
}

int rsc_tree_add(struct rsc *r, int nobj)
{
	OBJECT	*obj;
	struct rsc_tree	*nt;
	int	i;

	if (nobj < 1 || nobj > RSC_MAX_OBJECTS)
		return -1;
	obj = rsc_malloc((size_t)nobj * sizeof *obj);
	if (!obj)
		return -1;
	nt = rsc_realloc(r->tree, ((size_t)r->ntree + 1) * sizeof *nt);
	if (!nt)
	{
		rsc_mfree(obj);
		return -1;
	}
	r->tree = nt;
	for (i = 0; i < nobj; i++)
		init_object(&obj[i]);
	obj[nobj - 1].ob_flags = OF_LASTOB;
	nt[r->ntree].obj = obj;
	nt[r->ntree].nobj = nobj;
	return r->ntree++;
}

int rsc_tree_delete(struct rsc *r, int t)
{
	if (t < 0 || t >= r->ntree)
		return -1;
	free_tree(&r->tree[t]);
	memmove(&r->tree[t], &r->tree[t + 1], (size_t)(r->ntree - t - 1) * sizeof *r->tree);
	r->ntree--;
	return 0;
}

OBJECT *rsc_tree_objects(const struct rsc *r, int t, int *nobj)
{
	if (t < 0 || t >= r->ntree)
		return NULL;
	if (nobj)
		*nobj = r->tree[t].nobj;
	return r->tree[t].obj;
}

/* ---- objects -------------------------------------------------------------- */

static struct rsc_tree *tree_at(const struct rsc *r, int t)
{
	return t >= 0 && t < r->ntree ? &r->tree[t] : NULL;
}

static OBJECT *obj_at(const struct rsc *r, int t, int i)
{
	struct rsc_tree	*tr = tree_at(r, t);

	return tr && i >= 0 && i < tr->nobj ? &tr->obj[i] : NULL;
}

int rsc_obj_add(struct rsc *r, int t, int parent, unsigned type)
{
	struct rsc_tree	*tr = tree_at(r, t);
	OBJECT	*obj, *p, *o;
	int	k;

	if (!tr || parent < 0 || parent >= tr->nobj || tr->nobj >= RSC_MAX_OBJECTS)
		return -1;
	p = &tr->obj[parent];
	if (p->ob_tail < -1 || p->ob_tail >= tr->nobj || (p->ob_head == -1) != (p->ob_tail == -1))
		return -1;		/* broken links: do not write through them */
	obj = rsc_realloc(tr->obj, ((size_t)tr->nobj + 1) * sizeof *obj);
	if (!obj)
		return -1;
	tr->obj = obj;
	k = tr->nobj++;
	o = &obj[k];
	init_object(o);
	o->ob_type = (unsigned short)type;
	o->ob_flags = OF_LASTOB;
	obj[k - 1].ob_flags &= ~OF_LASTOB;
	p = &obj[parent];
	o->ob_next = (short)parent;
	if (p->ob_tail == -1)
		p->ob_head = (short)k;
	else
		obj[p->ob_tail].ob_next = (short)k;
	p->ob_tail = (short)k;
	return k;
}

/* The parent of object i (not the root), found the AES way: follow ob_next
 * until an object whose ob_tail is where we came from; -1 when i is
 * unlinked (ob_next -1), -2 when the links do not lead to a parent. */
static int find_parent(const OBJECT *o, int n, int i)
{
	int	j = i, nx, steps;

	if (o[i].ob_next == -1)
		return -1;
	for (steps = 0; steps < n; steps++)
	{
		nx = o[j].ob_next;
		if (nx < 0 || nx >= n)
			return -2;
		if (o[nx].ob_tail == j)
			return nx;
		j = nx;
	}
	return -2;
}

/* The preorder walk of the subtree of s (s's own ob_next is not looked at)
 * into order[]; parent[] (n ints) is scratch, left holding the parent of
 * every visited object and -2 for the others.  Returns the number of
 * objects visited or an enum rsc_walk_error.  Each object is entered once
 * and left once, so it ends within 2n steps; it reads only o[0..n-1]. */
static int walk_tree(const OBJECT *o, int n, int s, int *order, int *parent)
{
	int	cnt = 0, cur = s, h, tl, nx, i;

	for (i = 0; i < n; i++)
		parent[i] = -2;
	parent[s] = -1;
	order[cnt++] = s;
	for (;;)
	{
		/* entering cur */
		h = o[cur].ob_head;
		tl = o[cur].ob_tail;
		if (h < -1 || h >= n || tl < -1 || tl >= n)
			return RSC_WALK_ERANGE;
		if ((h == -1) != (tl == -1))
			return RSC_WALK_ECHAIN;
		if (h != -1)
		{
			if (parent[h] != -2)
				return RSC_WALK_ESHARED;
			parent[h] = cur;
			order[cnt++] = cur = h;
			continue;
		}
		/* leaving cur: on to its next sibling, or back up */
		for (;;)
		{
			if (cur == s)
				return cnt;
			nx = o[cur].ob_next;
			if (nx < -1 || nx >= n)
				return RSC_WALK_ERANGE;
			if (nx == -1)
				return RSC_WALK_ECHAIN;	/* a child must lead on */
			if (nx == parent[cur])
			{
				if (o[nx].ob_tail != cur)
					return RSC_WALK_ECHAIN;	/* back at the parent before its ob_tail */
				cur = nx;
				continue;
			}
			if (o[parent[cur]].ob_tail == cur)
				return RSC_WALK_ECHAIN;	/* the parent's ob_tail is not its last child */
			if (parent[nx] != -2)
				return RSC_WALK_ESHARED;
			parent[nx] = parent[cur];
			order[cnt++] = cur = nx;
			break;
		}
	}
}

int rsc_tree_preorder(const OBJECT *objs, int n, int *order)
{
	int	*scratch, cnt;

	if (!objs || n < 1 || n > RSC_MAX_OBJECTS)
		return RSC_WALK_EARG;
	scratch = rsc_malloc((size_t)n * 2 * sizeof *scratch);
	if (!scratch)
		return RSC_WALK_ENOMEM;
	cnt = walk_tree(objs, n, 0, order ? order : scratch + n, scratch);
	rsc_mfree(scratch);
	return cnt;
}

int rsc_obj_delete(struct rsc *r, int t, int i)
{
	struct rsc_tree	*tr = tree_at(r, t);
	OBJECT	*o;
	int	*scratch, *order, *parents;
	int	n, parent, prev = -1, k, cnt, steps;

	if (!tr || i <= 0 || i >= tr->nobj)
		return -1;
	o = tr->obj;
	n = tr->nobj;
	parent = find_parent(o, n, i);
	if (parent == -2)
		return -1;
	if (parent >= 0 && o[parent].ob_head != i)
	{
		/* the sibling before i */
		k = o[parent].ob_head;
		for (steps = 0; steps < n && k >= 0 && k < n && o[k].ob_next != i; steps++)
			k = o[k].ob_next;
		if (k < 0 || k >= n || o[k].ob_next != i)
			return -1;
		prev = k;
	}
	scratch = rsc_malloc((size_t)n * 2 * sizeof *scratch);
	if (!scratch)
		return -1;
	order = scratch;
	parents = scratch + n;
	cnt = walk_tree(o, n, i, order, parents);
	if (cnt < 0 || (parent >= 0 && parents[parent] != -2))
	{
		rsc_mfree(scratch);
		return -1;
	}
	/* nothing fails from here on */
	if (parent >= 0)
	{
		if (prev < 0)		/* i is the first child */
		{
			if (o[parent].ob_tail == i)
				o[parent].ob_head = o[parent].ob_tail = -1;
			else
				o[parent].ob_head = o[i].ob_next;
		}
		else
		{
			o[prev].ob_next = o[i].ob_next;
			if (o[parent].ob_tail == i)
				o[parent].ob_tail = (short)prev;
		}
	}
	for (k = 0; k < cnt; k++)
	{
		OBJECT	*d = &o[order[k]];

		spec_release(d);
		d->ob_next = d->ob_head = d->ob_tail = -1;
		d->ob_type = G_IBOX;
		d->ob_flags &= OF_LASTOB;
		d->ob_state = 0;
	}
	rsc_mfree(scratch);
	return 0;
}

/* link l of an object remapped through newidx (-1 for none) */
static short remap(int l, int n, const int *newidx)
{
	return (short)(l < 0 || l >= n ? -1 : newidx[l]);
}

int rsc_tree_compact(struct rsc *r, int t, int *map)
{
	struct rsc_tree	*tr = tree_at(r, t);
	OBJECT	*o, *no;
	int	*scratch, *newidx, *parents;
	int	n, cnt, k, m = 0;

	if (!tr)
		return RSC_WALK_EARG;
	o = tr->obj;
	n = tr->nobj;
	scratch = rsc_malloc((size_t)n * 2 * sizeof *scratch);
	if (!scratch)
		return RSC_WALK_ENOMEM;
	newidx = scratch;
	parents = scratch + n;
	cnt = walk_tree(o, n, 0, newidx, parents);
	if (cnt < 0)
	{
		rsc_mfree(scratch);
		return cnt;
	}
	no = rsc_malloc((size_t)cnt * sizeof *no);
	if (!no)
	{
		rsc_mfree(scratch);
		return RSC_WALK_ENOMEM;
	}
	/* nothing fails from here on: the reachable objects keep their order */
	for (k = 0; k < n; k++)
		newidx[k] = parents[k] != -2 ? m++ : -1;
	for (k = 0; k < n; k++)
	{
		if (newidx[k] < 0)
		{
			spec_release(&o[k]);
			continue;
		}
		no[newidx[k]] = o[k];
		no[newidx[k]].ob_next = remap(o[k].ob_next, n, newidx);
		no[newidx[k]].ob_head = remap(o[k].ob_head, n, newidx);
		no[newidx[k]].ob_tail = remap(o[k].ob_tail, n, newidx);
		no[newidx[k]].ob_flags &= ~OF_LASTOB;
	}
	no[0].ob_next = -1;
	no[cnt - 1].ob_flags |= OF_LASTOB;
	if (map)
		memcpy(map, newidx, (size_t)n * sizeof *map);
	rsc_mfree(o);
	tr->obj = no;
	tr->nobj = cnt;
	rsc_mfree(scratch);
	return cnt;
}

int rsc_obj_set_type(struct rsc *r, int t, int i, unsigned type)
{
	OBJECT	*o = obj_at(r, t, i);

	if (!o)
		return -1;
	if (!(o->ob_flags & OF_INDIRECT) && rsc_spec_kind(o->ob_type) != rsc_spec_kind(type))
		spec_release(o);
	o->ob_type = (unsigned short)type;
	return 0;
}

int rsc_obj_set_flags(struct rsc *r, int t, int i, unsigned flags)
{
	OBJECT	*o = obj_at(r, t, i);

	if (!o)
		return -1;
	if ((flags ^ o->ob_flags) & OF_INDIRECT && rsc_spec_kind(o->ob_type) != RSC_SPEC_VALUE)
		spec_release(o);	/* the kind changes: owned pointer <-> opaque value */
	o->ob_flags = (unsigned short)((flags & ~(unsigned)OF_LASTOB) | (o->ob_flags & OF_LASTOB));
	return 0;
}

int rsc_obj_set_state(struct rsc *r, int t, int i, unsigned state)
{
	OBJECT	*o = obj_at(r, t, i);

	if (!o)
		return -1;
	o->ob_state = (unsigned short)state;
	return 0;
}

int rsc_obj_set_geom(struct rsc *r, int t, int i, int x, int y, int w, int h)
{
	OBJECT	*o = obj_at(r, t, i);

	if (!o)
		return -1;
	o->ob_x = (short)x;
	o->ob_y = (short)y;
	o->ob_width = (short)w;
	o->ob_height = (short)h;
	return 0;
}

/* Object i when its type has spec kind k, else NULL. */
static OBJECT *obj_kind(const struct rsc *r, int t, int i, enum rsc_spec_kind k)
{
	OBJECT	*o = obj_at(r, t, i);

	return o && rsc_obj_spec_kind(o) == k ? o : NULL;
}

int rsc_obj_set_value(struct rsc *r, int t, int i, uint32_t value)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_VALUE);

	if (!o)
		return -1;
	memset(&o->ob_spec, 0, sizeof o->ob_spec);
	o->ob_spec.index = (long)value;
	return 0;
}

uint32_t rsc_obj_value(const struct rsc *r, int t, int i)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_VALUE);

	return o ? (uint32_t)o->ob_spec.index : 0;
}

int rsc_obj_get_value(const struct rsc *r, int t, int i, uint32_t *out)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_VALUE);

	if (!o)
		return -1;
	*out = (uint32_t)o->ob_spec.index;
	return 0;
}

int rsc_obj_set_str(struct rsc *r, int t, int i, enum rsc_str_field f, const char *s)
{
	OBJECT	*o = obj_at(r, t, i);
	enum rsc_spec_kind	k;
	char	**slot = NULL, *copy;
	TEDINFO	*te;

	if (!o)
		return -1;
	k = rsc_obj_spec_kind(o);
	te = k == RSC_SPEC_TEDINFO ? o->ob_spec.tedinfo : NULL;
	switch (f)
	{
	case RSC_STR_SPEC:
		if (k == RSC_SPEC_STRING)
			slot = &o->ob_spec.free_string;
		break;
	case RSC_STR_TE_TEXT:
		if (te)
			slot = &te->te_ptext;
		break;
	case RSC_STR_TE_TMPLT:
		if (te)
			slot = &te->te_ptmplt;
		break;
	case RSC_STR_TE_VALID:
		if (te)
			slot = &te->te_pvalid;
		break;
	case RSC_STR_IB_TEXT:
		if (k == RSC_SPEC_ICONBLK && o->ob_spec.iconblk)
			slot = &o->ob_spec.iconblk->ib_ptext;
		else if (k == RSC_SPEC_CICON && o->ob_spec.ciconblk)
			return set_cicon_text((struct rsc_cicon *)o->ob_spec.ciconblk, s);
		break;
	}
	if (!slot || dup_str(&copy, s))
		return -1;
	rsc_mfree(*slot);
	*slot = copy;
	return 0;
}

int rsc_obj_set_tedinfo(struct rsc *r, int t, int i, const TEDINFO *src)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_TEDINFO);
	TEDINFO	*te = NULL;

	if (!o || (src && !(te = copy_tedinfo(src))))
		return -1;
	spec_release(o);
	o->ob_spec.tedinfo = te;
	return 0;
}

int rsc_obj_set_iconblk(struct rsc *r, int t, int i, const ICONBLK *src)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_ICONBLK);
	ICONBLK	*ib = NULL;

	if (!o || (src && !(ib = copy_iconblk(src))))
		return -1;
	spec_release(o);
	o->ob_spec.iconblk = ib;
	return 0;
}

int rsc_obj_set_bitblk(struct rsc *r, int t, int i, const BITBLK *src)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_BITBLK);
	BITBLK	*bb = NULL;

	if (!o || (src && !(bb = copy_bitblk(src))))
		return -1;
	spec_release(o);
	o->ob_spec.bitblk = bb;
	return 0;
}

int rsc_obj_set_cicon(struct rsc *r, int t, int i, const ICONBLK *mono, const char *text)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_CICON);
	struct rsc_cicon	*c = NULL;

	if (!o || (mono && !(c = new_cicon(mono, text))))
		return -1;
	spec_release(o);
	o->ob_spec.ciconblk = c ? &c->blk : NULL;
	return 0;
}

struct rsc_cicon *rsc_obj_cicon(const struct rsc *r, int t, int i)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_CICON);

	return o ? (struct rsc_cicon *)o->ob_spec.ciconblk : NULL;
}

/* ---- resizing image data ------------------------------------------------- */

/* *data (oh rows of orow bytes, may be NULL) := a block of nh rows of nrow
 * bytes holding the top left of the old one, zero-filled; NULL stays NULL.
 * The new block goes to *out; *data is not touched.  Returns 0 or -1. */
static int resize_plane(const void *data, size_t orow, size_t oh, size_t nrow, size_t nh, void *out)
{
	unsigned char	*p = NULL;
	size_t	y, w = orow < nrow ? orow : nrow;

	if (data)
	{
		p = rsc_malloc(nrow * nh);
		if (!p)
			return -1;
		memset(p, 0, nrow * nh);
		for (y = 0; y < oh && y < nh; y++)
			memcpy(p + y * nrow, (const unsigned char *)data + y * orow, w);
	}
	memcpy(out, &p, sizeof p);
	return 0;
}

static size_t icon_row(int w)
{
	return rsc_plane_bytes(w, 1);
}

static int resize_bitblk(BITBLK *bb, int wb, int hl)
{
	short	*nd;

	if (!bb || wb < 0 || hl < 0 || wb > 32767 || hl > 32767)
		return -1;
	if (resize_plane(bb->bi_pdata, bb->bi_wb < 0 ? 0 : (size_t)bb->bi_wb,
			 bb->bi_hl < 0 ? 0 : (size_t)bb->bi_hl, (size_t)wb, (size_t)hl, &nd))
		return -1;
	rsc_mfree(bb->bi_pdata);
	bb->bi_pdata = nd;
	bb->bi_wb = (short)wb;
	bb->bi_hl = (short)hl;
	return 0;
}

int rsc_obj_resize_icon(struct rsc *r, int t, int i, int w, int h)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_ICONBLK);
	ICONBLK	*ib = o ? o->ob_spec.iconblk : NULL;
	short	*nm, *nd;
	size_t	oh;

	if (!ib || w < 0 || h < 0 || w > 32767 || h > 32767)
		return -1;
	oh = ib->ib_hicon < 0 ? 0 : (size_t)ib->ib_hicon;
	if (resize_plane(ib->ib_pmask, icon_row(ib->ib_wicon), oh, icon_row(w), (size_t)h, &nm))
		return -1;
	if (resize_plane(ib->ib_pdata, icon_row(ib->ib_wicon), oh, icon_row(w), (size_t)h, &nd))
	{
		rsc_mfree(nm);
		return -1;
	}
	rsc_mfree(ib->ib_pmask);
	rsc_mfree(ib->ib_pdata);
	ib->ib_pmask = nm;
	ib->ib_pdata = nd;
	ib->ib_wicon = (short)w;
	ib->ib_hicon = (short)h;
	return 0;
}

int rsc_obj_resize_bitblk(struct rsc *r, int t, int i, int wb, int hl)
{
	OBJECT	*o = obj_kind(r, t, i, RSC_SPEC_BITBLK);

	return o ? resize_bitblk(o->ob_spec.bitblk, wb, hl) : -1;
}

int rsc_frimg_resize(struct rsc *r, int k, int wb, int hl)
{
	return resize_bitblk(rsc_frimg(r, k), wb, hl);
}

/* ---- colour versions ------------------------------------------------------ */

int rsc_cicon_nversions(const struct rsc_cicon *c)
{
	const CICON	*p;
	int	n = 0;

	for (p = c->blk.mainlist; p; p = p->next_res)
		n++;
	return n;
}

struct rsc_cicon_version *rsc_cicon_version(const struct rsc_cicon *c, int v)
{
	CICON	*p;

	if (v < 0)
		return NULL;
	for (p = c->blk.mainlist; p && v > 0; p = p->next_res)
		v--;
	return (struct rsc_cicon_version *)p;
}

int rsc_cicon_add_version(struct rsc_cicon *c, int planes,
			  const short *col_data, const short *col_mask,
			  const short *sel_data, const short *sel_mask)
{
	struct rsc_cicon_version	*v;
	CICON	**tail;
	size_t	pb = rsc_plane_bytes(c->blk.monoblk.ib_wicon, c->blk.monoblk.ib_hicon);
	int	n = 0;

	if (planes < 1 || planes > RSC_MAX_PLANES || !col_data || !col_mask || !sel_data != !sel_mask)
		return -1;
	v = rsc_malloc(sizeof *v);
	if (!v)
		return -1;
	memset(v, 0, sizeof *v);
	v->c.num_planes = (short)planes;
	v->c.next_res = NULL;
	if (dup_mem(&v->c.col_data, col_data, pb * (size_t)planes)
	    || dup_mem(&v->c.col_mask, col_mask, pb)
	    || dup_mem(&v->c.sel_data, sel_data, pb * (size_t)planes)
	    || dup_mem(&v->c.sel_mask, sel_mask, pb))
	{
		free_version(v);
		return -1;
	}
	for (tail = &c->blk.mainlist; *tail; tail = &(*tail)->next_res)
		n++;
	*tail = &v->c;
	return n;
}

int rsc_cicon_remove_version(struct rsc_cicon *c, int v)
{
	CICON	**pp, *p;

	if (v < 0)
		return -1;
	for (pp = &c->blk.mainlist; *pp && v > 0; pp = &(*pp)->next_res)
		v--;
	p = *pp;
	if (!p)
		return -1;
	*pp = p->next_res;
	free_version((struct rsc_cicon_version *)p);
	return 0;
}

/* ---- free strings and free images ----------------------------------------- */

int rsc_nfrstr(const struct rsc *r)
{
	return r->nfrstr;
}

const char *rsc_frstr(const struct rsc *r, int k)
{
	return k >= 0 && k < r->nfrstr ? r->frstr[k] : NULL;
}

int rsc_frstr_add(struct rsc *r, const char *s)
{
	char	*copy, **list;

	if (!s || !(copy = rsc_strdup(s)))
		return -1;
	list = rsc_realloc(r->frstr, ((size_t)r->nfrstr + 1) * sizeof *list);
	if (!list)
	{
		rsc_mfree(copy);
		return -1;
	}
	r->frstr = list;
	list[r->nfrstr] = copy;
	return r->nfrstr++;
}

int rsc_frstr_set(struct rsc *r, int k, const char *s)
{
	char	*copy;

	if (k < 0 || k >= r->nfrstr || !s || !(copy = rsc_strdup(s)))
		return -1;
	rsc_mfree(r->frstr[k]);
	r->frstr[k] = copy;
	return 0;
}

int rsc_frstr_delete(struct rsc *r, int k)
{
	if (k < 0 || k >= r->nfrstr)
		return -1;
	rsc_mfree(r->frstr[k]);
	memmove(&r->frstr[k], &r->frstr[k + 1], (size_t)(r->nfrstr - k - 1) * sizeof *r->frstr);
	r->nfrstr--;
	return 0;
}

int rsc_nfrimg(const struct rsc *r)
{
	return r->nfrimg;
}

BITBLK *rsc_frimg(const struct rsc *r, int k)
{
	return k >= 0 && k < r->nfrimg ? r->frimg[k] : NULL;
}

int rsc_frimg_add(struct rsc *r, const BITBLK *bb)
{
	BITBLK	*copy, **list;

	if (!bb || !(copy = copy_bitblk(bb)))
		return -1;
	list = rsc_realloc(r->frimg, ((size_t)r->nfrimg + 1) * sizeof *list);
	if (!list)
	{
		free_bitblk(copy);
		return -1;
	}
	r->frimg = list;
	list[r->nfrimg] = copy;
	return r->nfrimg++;
}

int rsc_frimg_delete(struct rsc *r, int k)
{
	if (k < 0 || k >= r->nfrimg)
		return -1;
	free_bitblk(r->frimg[k]);
	memmove(&r->frimg[k], &r->frimg[k + 1], (size_t)(r->nfrimg - k - 1) * sizeof *r->frimg);
	r->nfrimg--;
	return 0;
}
