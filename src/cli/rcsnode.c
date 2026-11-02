/* RCSNODE.C	the node layer of rcs-cli (rcsnode.h, docs/rcs-cli.md) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rcsnode.h"

/* The longest path component looked up (longer ones are an error). */
#define COMP_MAX	255
/* The most bytes of user input quoted in a message. */
#define QUOTE_MAX	64

/* What the view knows about a tree, built on first use: parent[i] is the
 * parent of object i, -1 for the root, -2 for an object the root does not
 * reach.  state 0: not built; 1: built; -1: the links are broken (only the
 * root is reachable, and it has no children as far as the view goes). */
struct tcache
{
	int	state;
	int	nobj;
	int	*parent;
};

struct rcs_view
{
	const struct rsc	*r;
	const struct rsc_names	*n;
	int			ntrees, nstr, nimg;
	struct tcache		*tc;	/* per tree */
	char			ebuf[24];	/* the name of a colour icon entry */
};

/* ---- small helpers ------------------------------------------------------ */

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int eq_nocase(const char *a, const char *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

size_t rcs_escape(char *dst, size_t dstsize, const char *src, size_t max, int quote)
{
	static const char hex[] = "0123456789abcdef";
	size_t	k = 0, i;
	char	tmp[5];
	size_t	tl;

	if (dstsize == 0)
		return 0;
	for (i = 0; i < max && src[i]; i++)
	{
		unsigned char	c = (unsigned char)src[i];

		tl = 0;
		if (c == '\\' || (quote == '"' && c == '"'))
		{
			tmp[tl++] = '\\';
			tmp[tl++] = (char)c;
		}
		else if (c == '\n' || c == '\r' || c == '\t')
		{
			tmp[tl++] = '\\';
			tmp[tl++] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
		}
		else if (c < 0x20 || c >= 0x7f || (quote == 0 && c == ' '))
		{
			tmp[tl++] = '\\';
			tmp[tl++] = 'x';
			tmp[tl++] = hex[c >> 4];
			tmp[tl++] = hex[c & 15];
		}
		else
			tmp[tl++] = (char)c;
		/* room for this, and for "..." should more follow */
		if (k + tl + 4 > dstsize)
			break;
		memcpy(dst + k, tmp, tl);
		k += tl;
	}
	if (i < max && src[i] && k + 4 <= dstsize)
	{
		memcpy(dst + k, "...", 3);
		k += 3;
	}
	dst[k] = 0;
	return k;
}

int rcs_is_alert(const char *s)
{
	return s != NULL && s[0] == '[' && s[1] != 0 && s[2] == ']';
}

int rcs_node_eq(const struct rcs_node *a, const struct rcs_node *b)
{
	return a->kind == b->kind && a->tree == b->tree && a->obj == b->obj
	    && a->index == b->index && a->sel == b->sel;
}

static void node_set(struct rcs_node *nd, int kind, int tree, int obj, int index, int sel)
{
	nd->kind = kind;
	nd->tree = tree;
	nd->obj = obj;
	nd->index = index;
	nd->sel = sel;
}

/* ---- the view ----------------------------------------------------------- */

static void drop_caches(struct rcs_view *v)
{
	int	t;

	if (v->tc)
		for (t = 0; t < v->ntrees; t++)
			free(v->tc[t].parent);
	free(v->tc);
	v->tc = NULL;
	v->ntrees = v->nstr = v->nimg = 0;
}

static int build(struct rcs_view *v)
{
	v->ntrees = rsc_ntrees(v->r);
	v->nstr = rsc_nfrstr(v->r);
	v->nimg = rsc_nfrimg(v->r);
	v->tc = calloc((size_t)(v->ntrees > 0 ? v->ntrees : 1), sizeof *v->tc);
	if (!v->tc)
	{
		v->ntrees = 0;
		return -1;
	}
	return 0;
}

struct rcs_view *rcs_view_new(const struct rsc *r, const struct rsc_names *n)
{
	struct rcs_view	*v;

	if (!r)
		return NULL;
	v = calloc(1, sizeof *v);
	if (!v)
		return NULL;
	v->r = r;
	v->n = n;
	if (build(v) != 0)
	{
		free(v);
		return NULL;
	}
	return v;
}

void rcs_view_free(struct rcs_view *v)
{
	if (!v)
		return;
	drop_caches(v);
	free(v);
}

int rcs_view_reset(struct rcs_view *v)
{
	drop_caches(v);
	return build(v);
}

/* The cache of tree t, built on first use; NULL for a bad index or no
 * memory. */
static struct tcache *tree_cache(struct rcs_view *v, int t)
{
	struct tcache	*c;
	const OBJECT	*o;
	int		*order, nobj, k, i, p, ch, steps;

	if (t < 0 || t >= v->ntrees || !v->tc)
		return NULL;
	c = &v->tc[t];
	if (c->state != 0)
		return c;
	o = rsc_tree_objects(v->r, t, &nobj);
	if (!o)
		return NULL;
	c->parent = malloc((size_t)nobj * sizeof *c->parent);
	order = malloc((size_t)nobj * sizeof *order);
	if (!c->parent || !order)
	{
		free(c->parent);
		free(order);
		c->parent = NULL;
		return NULL;
	}
	for (i = 0; i < nobj; i++)
		c->parent[i] = -2;
	c->parent[0] = -1;
	c->nobj = nobj;
	k = rsc_tree_preorder(o, nobj, order);
	if (k == RSC_WALK_ENOMEM)
	{
		free(c->parent);
		free(order);
		c->parent = NULL;
		return NULL;
	}
	c->state = k < 1 ? -1 : 1;
	/* the walk checked the links: every chain ends at its parent */
	for (i = 0; i < k; i++)
	{
		p = order[i];
		steps = 0;
		for (ch = o[p].ob_head; ch >= 0 && ch < nobj && ch != p && steps < nobj; steps++)
		{
			c->parent[ch] = p;
			ch = o[ch].ob_next;
		}
	}
	free(order);
	return c;
}

static int reachable(struct rcs_view *v, int t, int i)
{
	struct tcache	*c = tree_cache(v, t);

	return c && i >= 0 && i < c->nobj && c->parent[i] != -2;
}

/* The colour icon of an object the root reaches, or NULL. */
static const struct rsc_cicon *obj_cicon(struct rcs_view *v, int t, int i)
{
	if (!reachable(v, t, i))
		return NULL;
	return rsc_obj_cicon(v->r, t, i);
}

/* ---- colour icon entries -------------------------------------------------- */

/* The name of entry (version, sel) of c into buf (room for 24). */
static void icon_entry_name(const struct rsc_cicon *c, int version, int sel, char *buf, size_t size)
{
	const struct rsc_cicon_version	*x, *u;
	int				w, dup = 0;

	if (version < 0)
	{
		snprintf(buf, size, "mono");
		return;
	}
	x = rsc_cicon_version(c, version);
	if (!x)
	{
		snprintf(buf, size, "?");
		return;
	}
	for (w = 0; w < version; w++)
	{
		u = rsc_cicon_version(c, w);
		if (u && u->c.num_planes == x->c.num_planes)
			dup = 1;
	}
	if (dup)
		snprintf(buf, size, "%d#%d%s", x->c.num_planes, version, sel ? ".sel" : "");
	else
		snprintf(buf, size, "%d%s", x->c.num_planes, sel ? ".sel" : "");
}

/* Parses an entry component; 1 and *version, *sel when comp names an entry
 * of c, else 0. */
static int icon_entry_find(const struct rsc_cicon *c, const char *comp, int *version, int *sel)
{
	long	planes = 0, ver = -1;
	int	nd = 0, n, w;
	const struct rsc_cicon_version	*x;

	if (eq_nocase(comp, "mono"))
	{
		*version = -1;
		*sel = 0;
		return 1;
	}
	for (; *comp >= '0' && *comp <= '9' && nd < 6; comp++, nd++)
		planes = planes * 10 + (*comp - '0');
	if (nd == 0 || nd >= 6)
		return 0;
	if (*comp == '#')
	{
		comp++;
		ver = 0;
		for (nd = 0; *comp >= '0' && *comp <= '9' && nd < 6; comp++, nd++)
			ver = ver * 10 + (*comp - '0');
		if (nd == 0 || nd >= 6)
			return 0;
	}
	*sel = 0;
	if (*comp == '.' && eq_nocase(comp, ".sel"))
		*sel = 1;
	else if (*comp)
		return 0;
	n = rsc_cicon_nversions(c);
	if (ver >= 0)
	{
		x = ver < n ? rsc_cicon_version(c, (int)ver) : NULL;
		if (!x || x->c.num_planes != planes)
			return 0;
		w = (int)ver;
	}
	else
	{
		for (w = 0; w < n; w++)
		{
			x = rsc_cicon_version(c, w);
			if (x && x->c.num_planes == planes)
				break;
		}
		if (w == n)
			return 0;
		x = rsc_cicon_version(c, w);
	}
	if (*sel && (!x || !x->c.sel_data))
		return 0;
	*version = w;
	return 1;
}

/* ---- names --------------------------------------------------------------- */

/* The display name of a tree, object, free string or image (the names
 * library's accessors), or NULL. */
static const char *entry_name(struct rcs_view *v, const struct rcs_node *nd)
{
	switch (nd->kind)
	{
	case RCS_NODE_TREE:
		return rsc_names_tree_name(v->n, nd->tree);
	case RCS_NODE_OBJ:
		return rsc_names_obj_name(v->n, nd->tree, nd->obj);
	case RCS_NODE_FRSTR:
		return rsc_names_str_name(v->n, nd->index);
	case RCS_NODE_FRIMG:
		return rsc_names_img_name(v->n, nd->index);
	default:
		return NULL;
	}
}

/* Whether name, looked up in the scope of nd, finds nd again. */
static int finds_again(struct rcs_view *v, const struct rcs_node *nd, const char *name)
{
	int	nobj;

	switch (nd->kind)
	{
	case RCS_NODE_TREE:
		return rsc_names_find_tree(v->n, name, v->ntrees) == nd->tree;
	case RCS_NODE_OBJ:
		return rsc_tree_objects(v->r, nd->tree, &nobj)
		    && rsc_names_find_obj(v->n, nd->tree, name, nobj) == nd->obj;
	case RCS_NODE_FRSTR:
		return rsc_names_find_str(v->n, name, v->nstr) == nd->index;
	case RCS_NODE_FRIMG:
		return rsc_names_find_img(v->n, name, v->nimg) == nd->index;
	default:
		return 0;
	}
}

const char *rcs_node_name(struct rcs_view *v, const struct rcs_node *nd)
{
	const struct rsc_cicon	*c;

	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
		return "/";
	case RCS_NODE_STRDIR:
		return ".strings";
	case RCS_NODE_IMGDIR:
		return ".images";
	case RCS_NODE_ICON:
		c = obj_cicon(v, nd->tree, nd->obj);
		if (!c)
			return NULL;
		icon_entry_name(c, nd->index, nd->sel, v->ebuf, sizeof v->ebuf);
		return v->ebuf;
	default:
		return entry_name(v, nd);
	}
}

/* Whether name can be a path component that a lookup finds by name. */
static int name_usable(const char *name, int at_root)
{
	const char	*p;

	if (name[0] == '#' && name[1])
	{
		for (p = name + 1; *p >= '0' && *p <= '9'; p++)
			;
		if (*p == 0)
			return 0;	/* "#digits" is always an index */
	}
	if (strchr(name, '/'))
		return 0;
	if (at_root && (eq_nocase(name, ".strings") || eq_nocase(name, ".images")))
		return 0;
	return 1;
}

int rcs_node_component(struct rcs_view *v, const struct rcs_node *nd, char *buf, size_t size)
{
	const char	*name;
	int		idx, n;

	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
	case RCS_NODE_ICON:
		name = rcs_node_name(v, nd);
		if (!name)
			return -1;
		n = snprintf(buf, size, "%s", name);
		return n < 0 || (size_t)n >= size ? -1 : 0;
	default:
		break;
	}
	name = entry_name(v, nd);
	if (name && name_usable(name, nd->kind == RCS_NODE_TREE) && finds_again(v, nd, name))
		n = snprintf(buf, size, "%s", name);
	else
	{
		idx = nd->kind == RCS_NODE_TREE ? nd->tree : nd->kind == RCS_NODE_OBJ ? nd->obj : nd->index;
		n = snprintf(buf, size, "#%d", idx);
	}
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int rcs_node_path(struct rcs_view *v, const struct rcs_node *nd, char *buf, size_t size)
{
	char		a[RSC_NAME_MAX + 16], b[RSC_NAME_MAX + 16], c[32];
	struct rcs_node	t, o;
	int		n;

	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
		n = snprintf(buf, size, "/");
		break;
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
		n = snprintf(buf, size, "/%s", nd->kind == RCS_NODE_STRDIR ? ".strings" : ".images");
		break;
	case RCS_NODE_FRSTR:
	case RCS_NODE_FRIMG:
		if (rcs_node_component(v, nd, a, sizeof a))
			return -1;
		n = snprintf(buf, size, "/%s/%s", nd->kind == RCS_NODE_FRSTR ? ".strings" : ".images", a);
		break;
	case RCS_NODE_TREE:
		if (rcs_node_component(v, nd, a, sizeof a))
			return -1;
		n = snprintf(buf, size, "/%s", a);
		break;
	case RCS_NODE_OBJ:
	case RCS_NODE_ICON:
		node_set(&t, RCS_NODE_TREE, nd->tree, 0, 0, 0);
		node_set(&o, RCS_NODE_OBJ, nd->tree, nd->obj, 0, 0);
		if (rcs_node_component(v, &t, a, sizeof a) || rcs_node_component(v, &o, b, sizeof b))
			return -1;
		if (nd->kind == RCS_NODE_OBJ)
			n = snprintf(buf, size, "/%s/%s", a, b);
		else
		{
			if (rcs_node_component(v, nd, c, sizeof c))
				return -1;
			n = snprintf(buf, size, "/%s/%s/%s", a, b, c);
		}
		break;
	default:
		return -1;
	}
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

/* ---- directories and children ---------------------------------------------- */

int rcs_is_dir(struct rcs_view *v, const struct rcs_node *nd)
{
	const OBJECT	*o;
	int		nobj;

	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
	case RCS_NODE_TREE:
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
		return 1;
	case RCS_NODE_OBJ:
		o = rsc_tree_objects(v->r, nd->tree, &nobj);
		if (!o || nd->obj < 0 || nd->obj >= nobj || !reachable(v, nd->tree, nd->obj))
			return 0;
		if (v->tc[nd->tree].state == 1 && o[nd->obj].ob_head >= 0)
			return 1;
		return obj_cicon(v, nd->tree, nd->obj) != NULL;
	default:
		return 0;
	}
}

/* Calls back for each child of a directory: the work of rcs_children and
 * rcs_nchildren.  out NULL counts only.  Returns the count or -1. */
static int walk_children(struct rcs_view *v, const struct rcs_node *nd, struct rcs_node *out,
			 char *msg, size_t msgsize)
{
	const struct rsc_cicon	*c;
	const OBJECT		*o;
	struct tcache		*tc;
	int			k = 0, i, n, nobj, p, ch, steps;

	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
		for (i = 0; i < v->ntrees; i++, k++)
			if (out)
				node_set(&out[k], RCS_NODE_TREE, i, 0, 0, 0);
		if (out)
		{
			node_set(&out[k], RCS_NODE_STRDIR, 0, 0, 0, 0);
			node_set(&out[k + 1], RCS_NODE_IMGDIR, 0, 0, 0, 0);
		}
		return k + 2;
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
		n = nd->kind == RCS_NODE_STRDIR ? v->nstr : v->nimg;
		for (i = 0; i < n; i++)
			if (out)
				node_set(&out[i], nd->kind == RCS_NODE_STRDIR ? RCS_NODE_FRSTR : RCS_NODE_FRIMG,
					 0, 0, i, 0);
		return n;
	case RCS_NODE_TREE:
	case RCS_NODE_OBJ:
		break;
	default:
		return 0;
	}
	p = nd->kind == RCS_NODE_TREE ? 0 : nd->obj;
	o = rsc_tree_objects(v->r, nd->tree, &nobj);
	if (!o || p < 0 || p >= nobj)
		return 0;
	tc = tree_cache(v, nd->tree);
	if (!tc)
	{
		snprintf(msg, msgsize, "out of memory");
		return -1;
	}
	if (tc->state < 0)
	{
		snprintf(msg, msgsize, "the links of tree #%d are broken", nd->tree);
		return -1;
	}
	if (tc->parent[p] == -2)
		return 0;
	if (nd->kind == RCS_NODE_OBJ)
	{
		c = rsc_obj_cicon(v->r, nd->tree, p);
		if (c)
		{
			if (out)
				node_set(&out[k], RCS_NODE_ICON, nd->tree, p, -1, 0);
			k++;
			n = rsc_cicon_nversions(c);
			for (i = 0; i < n; i++)
			{
				const struct rsc_cicon_version	*x = rsc_cicon_version(c, i);

				if (out)
					node_set(&out[k], RCS_NODE_ICON, nd->tree, p, i, 0);
				k++;
				if (x && x->c.sel_data)
				{
					if (out)
						node_set(&out[k], RCS_NODE_ICON, nd->tree, p, i, 1);
					k++;
				}
			}
		}
	}
	steps = 0;
	for (ch = o[p].ob_head; ch >= 0 && ch < nobj && ch != p && steps < nobj; steps++)
	{
		if (out)
			node_set(&out[k], RCS_NODE_OBJ, nd->tree, ch, 0, 0);
		k++;
		ch = o[ch].ob_next;
	}
	return k;
}

/* The number of child objects of object p of tree t (its colour icon
 * entries not counted); 0 when the root does not reach it, -1 for broken
 * links or no memory. */
static int count_objs(struct rcs_view *v, int t, int p)
{
	struct rcs_node	nd;
	char		msg[8];
	int		n, k, i;
	const struct rsc_cicon	*c;
	const struct rsc_cicon_version	*x;

	node_set(&nd, RCS_NODE_OBJ, t, p, 0, 0);
	n = walk_children(v, &nd, NULL, msg, sizeof msg);
	if (n <= 0)
		return n;
	c = obj_cicon(v, t, p);
	if (c)
	{
		k = rsc_cicon_nversions(c);
		n--;
		for (i = 0; i < k; i++)
		{
			x = rsc_cicon_version(c, i);
			n -= x && x->c.sel_data ? 2 : 1;
		}
	}
	return n;
}

int rcs_nchildren(struct rcs_view *v, const struct rcs_node *dir)
{
	char	msg[8];

	return walk_children(v, dir, NULL, msg, sizeof msg);
}

int rcs_children(struct rcs_view *v, const struct rcs_node *dir, struct rcs_node **kids,
		 int *count, char *msg, size_t msgsize)
{
	struct rcs_node	*a;
	int		n, m;

	*kids = NULL;
	*count = 0;
	n = walk_children(v, dir, NULL, msg, msgsize);
	if (n <= 0)
		return n;
	a = malloc((size_t)n * sizeof *a);
	if (!a)
	{
		snprintf(msg, msgsize, "out of memory");
		return -1;
	}
	m = walk_children(v, dir, a, msg, msgsize);
	if (m != n)
	{
		free(a);
		if (m >= 0)
			snprintf(msg, msgsize, "internal error listing children");
		return -1;
	}
	*kids = a;
	*count = n;
	return 0;
}

/* ---- resolving ------------------------------------------------------------ */

/* The path as typed up to (not including) position end, without a
 * trailing '/' (but "/" for the root), escaped, into buf. */
static void typed_prefix(const char *path, size_t end, char *buf, size_t size)
{
	char	tmp[RCS_PATH_MAX];
	size_t	n = end;

	while (n > 1 && path[n - 1] == '/')
		n--;
	if (n == 0)
	{
		snprintf(buf, size, "/");
		return;
	}
	if (n >= sizeof tmp)
		n = sizeof tmp - 1;
	memcpy(tmp, path, n);
	tmp[n] = 0;
	rcs_escape(buf, size, tmp, (size_t)-1, 1);
}

static int fail_find(int rc, const char *comp, const char *path, size_t at, char *msg, size_t msgsize)
{
	char	q[QUOTE_MAX * 4 + 8], p[RCS_PATH_MAX];

	rcs_escape(q, sizeof q, comp, QUOTE_MAX, 1);
	typed_prefix(path, at, p, sizeof p);
	if (rc == RSC_NAMES_AMBIGUOUS)
		snprintf(msg, msgsize, "ambiguous name '%s' in %s (use #N)", q, p);
	else
		snprintf(msg, msgsize, "not found: '%s' in %s", q, p);
	return -1;
}

static int bad_path(const char *path, const char *why, char *msg, size_t msgsize)
{
	char	q[QUOTE_MAX * 4 + 8];

	rcs_escape(q, sizeof q, path, QUOTE_MAX, 1);
	snprintf(msg, msgsize, "bad path '%s': %s", q, why);
	return -1;
}

/* One step from cur by component comp (at offset at of path). */
static int step(struct rcs_view *v, struct rcs_node *cur, const char *comp, const char *path,
		size_t at, char *msg, size_t msgsize)
{
	const struct rsc_cicon	*c;
	int			k, nobj, version, sel;

	switch (cur->kind)
	{
	case RCS_NODE_ROOT:
		if (eq_nocase(comp, ".strings"))
		{
			node_set(cur, RCS_NODE_STRDIR, 0, 0, 0, 0);
			return 0;
		}
		if (eq_nocase(comp, ".images"))
		{
			node_set(cur, RCS_NODE_IMGDIR, 0, 0, 0, 0);
			return 0;
		}
		k = rsc_names_find_tree(v->n, comp, v->ntrees);
		if (k < 0)
			return fail_find(k, comp, path, at, msg, msgsize);
		node_set(cur, RCS_NODE_TREE, k, 0, 0, 0);
		return 0;
	case RCS_NODE_TREE:
		if (!rsc_tree_objects(v->r, cur->tree, &nobj))
			return fail_find(RSC_NAMES_NOTFOUND, comp, path, at, msg, msgsize);
		k = rsc_names_find_obj(v->n, cur->tree, comp, nobj);
		if (k < 0)
			return fail_find(k, comp, path, at, msg, msgsize);
		node_set(cur, RCS_NODE_OBJ, cur->tree, k, 0, 0);
		return 0;
	case RCS_NODE_OBJ:
		break;
	case RCS_NODE_STRDIR:
		k = rsc_names_find_str(v->n, comp, v->nstr);
		if (k < 0)
			return fail_find(k, comp, path, at, msg, msgsize);
		node_set(cur, RCS_NODE_FRSTR, 0, 0, k, 0);
		return 0;
	case RCS_NODE_IMGDIR:
		k = rsc_names_find_img(v->n, comp, v->nimg);
		if (k < 0)
			return fail_find(k, comp, path, at, msg, msgsize);
		node_set(cur, RCS_NODE_FRIMG, 0, 0, k, 0);
		return 0;
	default:
		goto notdir;
	}
	/* below an object */
	if (!rcs_is_dir(v, cur))
		goto notdir;
	c = obj_cicon(v, cur->tree, cur->obj);
	if (c && icon_entry_find(c, comp, &version, &sel))
	{
		node_set(cur, RCS_NODE_ICON, cur->tree, cur->obj, version, sel);
		return 0;
	}
	if (!rsc_tree_objects(v->r, cur->tree, &nobj))
		return fail_find(RSC_NAMES_NOTFOUND, comp, path, at, msg, msgsize);
	k = rsc_names_find_obj(v->n, cur->tree, comp, nobj);
	if (k < 0)
		return fail_find(k, comp, path, at, msg, msgsize);
	if (!reachable(v, cur->tree, k) || v->tc[cur->tree].parent[k] != cur->obj)
		return fail_find(RSC_NAMES_NOTFOUND, comp, path, at, msg, msgsize);
	node_set(cur, RCS_NODE_OBJ, cur->tree, k, 0, 0);
	return 0;
notdir:
	{
		char	p[RCS_PATH_MAX];

		typed_prefix(path, at, p, sizeof p);
		snprintf(msg, msgsize, "not a directory: %s", p);
	}
	return -1;
}

int rcs_resolve(struct rcs_view *v, const char *path, struct rcs_node *out,
		char *msg, size_t msgsize)
{
	struct rcs_node	cur;
	char		comp[COMP_MAX + 1];
	size_t		pos, end;

	msg[0] = 0;
	node_set(&cur, RCS_NODE_ROOT, 0, 0, 0, 0);
	if (path[0] != '/')
		return bad_path(path, "paths start with /", msg, msgsize);
	if (strlen(path) >= RCS_PATH_MAX)
		return bad_path(path, "too long", msg, msgsize);
	/* the syntax first, so that "/x//y" is a bad path whatever x is */
	for (pos = 1; path[pos]; pos++)
		if (path[pos] == '/' && path[pos - 1] == '/')
			return bad_path(path, "empty component (//)", msg, msgsize);
	pos = 1;
	while (path[pos])
	{
		for (end = pos; path[end] && path[end] != '/'; end++)
			;
		if (end == pos)
			return bad_path(path, "empty component (//)", msg, msgsize);
		if (end - pos > COMP_MAX)
			return bad_path(path, "component too long", msg, msgsize);
		memcpy(comp, path + pos, end - pos);
		comp[end - pos] = 0;
		if (step(v, &cur, comp, path, pos, msg, msgsize))
			return -1;
		pos = end;
		if (path[pos] == '/')
			pos++;
	}
	*out = cur;
	return 0;
}

/* ---- describing ----------------------------------------------------------- */

int rcs_describe(struct rcs_view *v, const struct rcs_node *nd, struct rcs_desc *d)
{
	const struct rsc_cicon_version	*x;
	const OBJECT			*o;
	struct tcache			*tc;
	int				nobj, g = 0;

	memset(d, 0, sizeof *d);
	d->kind = nd->kind;
	d->tkind = -1;
	d->parent = -1;
	d->version = -1;
	switch (nd->kind)
	{
	case RCS_NODE_ROOT:
		d->ntrees = v->ntrees;
		d->nfrstr = v->nstr;
		d->nfrimg = v->nimg;
		rsc_cell(v->r, &d->cell_w, &d->cell_h);
		d->vrsn = rsc_vrsn(v->r);
		break;
	case RCS_NODE_STRDIR:
		d->count = v->nstr;
		break;
	case RCS_NODE_IMGDIR:
		d->count = v->nimg;
		break;
	case RCS_NODE_TREE:
		if (!rsc_tree_objects(v->r, nd->tree, &nobj))
			return -1;
		d->tree = nd->tree;
		d->nobj = nobj;
		d->tkind = rsc_names_tree_kind(v->n, nd->tree, &g);
		d->tkind_guessed = d->tkind >= 0 && g;
		d->nchildren = count_objs(v, nd->tree, 0);
		break;
	case RCS_NODE_OBJ:
	case RCS_NODE_ICON:
		o = rsc_tree_objects(v->r, nd->tree, &nobj);
		if (!o || nd->obj < 0 || nd->obj >= nobj)
			return -1;
		tc = tree_cache(v, nd->tree);
		d->tree = nd->tree;
		d->obj = nd->obj;
		d->nobj = nobj;
		d->o = &o[nd->obj];
		d->reachable = tc && tc->parent[nd->obj] != -2;
		d->parent = tc && tc->parent[nd->obj] >= 0 ? tc->parent[nd->obj] : -1;
		d->spec_kind = (int)rsc_obj_spec_kind(d->o);
		switch (d->spec_kind)
		{
		case RSC_SPEC_VALUE:
			rsc_obj_get_value(v->r, nd->tree, nd->obj, &d->value);
			break;
		case RSC_SPEC_STRING:
			d->str = d->o->ob_spec.free_string;
			break;
		case RSC_SPEC_TEDINFO:
			d->ted = d->o->ob_spec.tedinfo;
			break;
		case RSC_SPEC_ICONBLK:
			d->icon = d->o->ob_spec.iconblk;
			break;
		case RSC_SPEC_BITBLK:
			d->bit = d->o->ob_spec.bitblk;
			break;
		case RSC_SPEC_CICON:
			d->cicon = rsc_obj_cicon(v->r, nd->tree, nd->obj);
			break;
		}
		if (nd->kind == RCS_NODE_OBJ)
		{
			d->nchildren = count_objs(v, nd->tree, nd->obj);
			break;
		}
		/* a colour icon entry */
		if (!d->cicon || !d->reachable)
			return -1;
		d->version = nd->index;
		d->sel = nd->sel;
		d->w = d->cicon->blk.monoblk.ib_wicon;
		d->h = d->cicon->blk.monoblk.ib_hicon;
		d->mask_bytes = rsc_plane_bytes(d->w, d->h);
		if (nd->index < 0)
		{
			d->planes = 1;
			d->data_bytes = d->mask_bytes;
			break;
		}
		x = rsc_cicon_version(d->cicon, nd->index);
		if (!x || (nd->sel && !x->c.sel_data))
			return -1;
		d->planes = x->c.num_planes;
		d->has_sel = x->c.sel_data != NULL;
		d->data_bytes = d->mask_bytes * (size_t)(d->planes > 0 ? d->planes : 0);
		break;
	case RCS_NODE_FRSTR:
		d->index = nd->index;
		d->text = rsc_frstr(v->r, nd->index);
		if (!d->text)
			return -1;
		d->alert = rcs_is_alert(d->text);
		break;
	case RCS_NODE_FRIMG:
		d->index = nd->index;
		d->bit = rsc_frimg(v->r, nd->index);
		if (!d->bit)
			return -1;
		break;
	default:
		return -1;
	}
	d->name = rcs_node_name(v, nd);
	return 0;
}
