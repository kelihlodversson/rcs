/* RCSCMD.C	the commands of rcs-cli: ls, tree, stat, cat, convert, and their
 *		output formats (docs/rcs-cli.md says them exactly)
 *
 * Every byte that comes from the resource or the names goes through
 * rcs_escape (or out_text for cat), so the output is printable ASCII and
 * a name takes a bounded number of characters.  The walks of ls -R and
 * tree keep their own stack: no recursion on the C stack.
 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rscfile.h"
#include "rcscmd.h"

/* Room for an escaped name (RSC_NAME_MAX bytes, 4 characters each). */
#define NAME_BUF	(RSC_NAME_MAX * 4 + 8)
/* The bytes of a free string ls -l and tree show. */
#define TEXT_SHOW	60
/* The levels tree indents; deeper lines start with their depth. */
#define TREE_INDENT	32

void rcs_error(const char *fmt, ...)
{
	va_list	ap;

	fflush(stdout);
	fputs("rcs-cli: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* "object" or "objects" for n */
static const char *plural(int n, const char *one, const char *many)
{
	return n == 1 ? one : many;
}

void rcs_note(const char *fmt, ...)
{
	va_list	ap;

	fflush(stdout);
	fputs("rcs-cli: note: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ---- the output helpers ------------------------------------------------------ */

/* An escaped name for a column (space as \x20), or "-" for none. */
static const char *col_name(char *buf, const char *name)
{
	if (!name)
		return "-";
	rcs_escape(buf, NAME_BUF, name, RSC_NAME_MAX, 0);
	return buf;
}

/* Writes s escaped (quote as for rcs_escape) to stdout, in pieces. */
static void out_text(const char *s, int quote)
{
	char	buf[64];
	size_t	i;

	for (i = 0; s[i]; i++)
	{
		rcs_escape(buf, sizeof buf, s + i, 1, quote | RCS_ESC_PLAIN);
		fputs(buf, stdout);
	}
}

/* An escaped user path for a message. */
static const char *arg_path(char *buf, size_t size, const char *path)
{
	rcs_escape(buf, size, path, 128, 1);
	return buf;
}

/* A coordinate: in file form the cells, then the pixel remainder of the
 * high byte (above 128 it counts as negative, as rsrc_obfix has it), as
 * "3", "3+4" or "3-2"; in pixels the value. */
static const char *coord(char *buf, size_t size, int v, int pixels)
{
	unsigned	w = (unsigned)v & 0xffff, lo = w & 0xff, hi = w >> 8;
	int		rem = hi > 128 ? (int)hi - 256 : (int)hi;

	if (pixels)
		snprintf(buf, size, "%d", v);
	else if (rem == 0)
		snprintf(buf, size, "%u", lo);
	else
		snprintf(buf, size, "%u%c%d", lo, rem > 0 ? '+' : '-', rem > 0 ? rem : -rem);
	return buf;
}

static const char *const type_names[] =
{
	"G_BOX", "G_TEXT", "G_BOXTEXT", "G_IMAGE", "G_PROGDEF", "G_IBOX", "G_BUTTON",
	"G_BOXCHAR", "G_STRING", "G_FTEXT", "G_FBOXTEXT", "G_ICON", "G_TITLE", "G_CICON"
};

/* The type as the AES name of the low byte or 0xNN, with ":N" for a
 * non-zero extended type (the high byte). */
static const char *type_str(char *buf, size_t size, unsigned type)
{
	unsigned	lo = type & 0xff, hi = (type >> 8) & 0xff;
	char		t[16];

	if (lo >= G_BOX && lo <= G_CICON)
		snprintf(t, sizeof t, "%s", type_names[lo - G_BOX]);
	else
		snprintf(t, sizeof t, "0x%02X", lo);
	if (hi)
		snprintf(buf, size, "%s:%u", t, hi);
	else
		snprintf(buf, size, "%s", t);
	return buf;
}

static const char *const flag_names[] =
{
	"SELECTABLE", "DEFAULT", "EXIT", "EDITABLE", "RBUTTON", "LASTOB", "TOUCHEXIT",
	"HIDETREE", "INDIRECT"
};

static const char *const state_names[] =
{
	"SELECTED", "CROSSED", "CHECKED", "DISABLED", "OUTLINED", "SHADOWED"
};

static void add_word(char *buf, size_t size, const char *w)
{
	size_t	n = strlen(buf);

	snprintf(buf + n, size - n, "%s%s", n ? "," : "", w);
}

/* The flags as a comma list in bit order, then FL3DIND, FL3DBAK or FL3DACT
 * for bits 9 and 10, then the other bits as one 0xNNNN; "-" for none. */
static const char *flags_str(char *buf, size_t size, unsigned flags)
{
	unsigned	i;
	char		h[8];

	buf[0] = 0;
	for (i = 0; i < 9; i++)
		if (flags & (1u << i))
			add_word(buf, size, flag_names[i]);
	switch (flags & 0x600)
	{
	case 0x200:
		add_word(buf, size, "FL3DIND");
		break;
	case 0x400:
		add_word(buf, size, "FL3DBAK");
		break;
	case 0x600:
		add_word(buf, size, "FL3DACT");
		break;
	}
	if (flags & 0xf800)
	{
		snprintf(h, sizeof h, "0x%04X", flags & 0xf800);
		add_word(buf, size, h);
	}
	if (!buf[0])
		snprintf(buf, size, "-");
	return buf;
}

/* The state likewise: the six AES names, then the other bits as 0xNNNN. */
static const char *state_str(char *buf, size_t size, unsigned state)
{
	unsigned	i;
	char		h[8];

	buf[0] = 0;
	for (i = 0; i < 6; i++)
		if (state & (1u << i))
			add_word(buf, size, state_names[i]);
	if (state & 0xffc0)
	{
		snprintf(h, sizeof h, "0x%04X", state & 0xffc0);
		add_word(buf, size, h);
	}
	if (!buf[0])
		snprintf(buf, size, "-");
	return buf;
}

static const char *kind_names[] = { "UNKN", "PANL", "MENU", "DIAL", "ALRT" };

/* The kind of a tree with "~" when guessed; UNKN when it is not known. */
static const char *kind_str(char *buf, size_t size, const struct rcs_desc *d)
{
	if (d->tkind < 0 || d->tkind > 4)
		snprintf(buf, size, "UNKN");
	else
		snprintf(buf, size, "%s%s", kind_names[d->tkind], d->tkind_guessed ? "~" : "");
	return buf;
}

/* A character as 'c', '\'' , '\\' or '\xNN'. */
static const char *char_str(char *buf, size_t size, unsigned c)
{
	c &= 0xff;
	if (c == '\'' || c == '\\')
		snprintf(buf, size, "'\\%c'", (int)c);
	else if (c >= 0x20 && c < 0x7f)
		snprintf(buf, size, "'%c'", (int)c);
	else
		snprintf(buf, size, "'\\x%02x'", c);
	return buf;
}

/* "#N" of an index. */
static const char *idx_str(char *buf, size_t size, int i)
{
	snprintf(buf, size, "#%d", i);
	return buf;
}

/* The line of a node in ls -l (pad 1) and tree (pad 0, no geometry). */
static void node_line(struct rcs_ctx *c, const struct rcs_node *nd, int pad)
{
	struct rcs_desc	d;
	char		nb[NAME_BUF], ib[16], tb[16], fb[160], sb[120];
	char		g[4][16], tx[TEXT_SHOW * 4 + 8], kb[8];
	int		cw, ch, len;
	const OBJECT	*o;
	const char	*nm;

	if (rcs_describe(c->v, nd, &d) != 0)
	{
		printf("?\n");
		return;
	}
	switch (d.kind)
	{
	case RCS_NODE_ROOT:
		printf("/\n");
		break;
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
		printf(pad ? "%-6s %-12s %-5s %d\n" : "%s %s %s %d\n", "-", d.name, "DIR", d.count);
		break;
	case RCS_NODE_TREE:
		printf(pad ? "%-6s %-12s %-5s %d\n" : "%s %s %s %d\n", idx_str(ib, sizeof ib, d.tree),
		       col_name(nb, d.name), kind_str(kb, sizeof kb, &d), d.nobj);
		break;
	case RCS_NODE_OBJ:
		o = d.o;
		idx_str(ib, sizeof ib, d.obj);
		nm = col_name(nb, d.name);
		type_str(tb, sizeof tb, o->ob_type);
		flags_str(fb, sizeof fb, o->ob_flags);
		state_str(sb, sizeof sb, o->ob_state);
		if (!pad)
		{
			printf("%s %s %s %s %s\n", ib, nm, tb, fb, sb);
			break;
		}
		rsc_cell(c->r, &cw, &ch);
		coord(g[0], sizeof g[0], o->ob_x, cw != 0);
		coord(g[1], sizeof g[1], o->ob_y, cw != 0);
		coord(g[2], sizeof g[2], o->ob_width, cw != 0);
		coord(g[3], sizeof g[3], o->ob_height, cw != 0);
		printf("%-6s %-12s %-10s %s %s %s,%s,%s,%s\n", ib, nm, tb, fb, sb, g[0], g[1], g[2], g[3]);
		break;
	case RCS_NODE_FRSTR:
		len = (int)strlen(d.text);
		rcs_escape(tx, sizeof tx, d.text, TEXT_SHOW, '"' | RCS_ESC_PLAIN);
		printf(pad ? "%-6s %-12s %5d %-5s \"%s\"%s\n" : "%s %s %d %s \"%s\"%s\n",
		       idx_str(ib, sizeof ib, d.index), col_name(nb, d.name), len,
		       d.alert ? "ALERT" : "-", tx, len > TEXT_SHOW ? "..." : "");
		break;
	case RCS_NODE_FRIMG:
		printf(pad ? "%-6s %-12s %dx%d\n" : "%s %s %dx%d\n", idx_str(ib, sizeof ib, d.index),
		       col_name(nb, d.name), d.bit->bi_wb * 8, d.bit->bi_hl);
		break;
	case RCS_NODE_ICON:
		printf(pad ? "%-8s %2d %s\n" : "%s %d %s\n", d.name, d.planes, d.sel ? "yes" : "no");
		break;
	}
}

/* ---- ls -------------------------------------------------------------------- */

/* The short line of a node: its component. */
static void short_line(struct rcs_ctx *c, const struct rcs_node *nd)
{
	char	comp[RSC_NAME_MAX + 32], buf[NAME_BUF + 32];

	if (rcs_node_component(c->v, nd, comp, sizeof comp) != 0)
		snprintf(comp, sizeof comp, "?");
	rcs_escape(buf, sizeof buf, comp, sizeof comp, 0);
	printf("%s\n", buf);
}

static void list_line(struct rcs_ctx *c, const struct rcs_node *nd, int lng)
{
	if (lng)
		node_line(c, nd, 1);
	else
		short_line(c, nd);
}

/* The header of a section of ls -R: the canonical path and ':'. */
static void section_header(struct rcs_ctx *c, const struct rcs_node *nd, int first)
{
	char	p[RCS_PATH_MAX], e[RCS_PATH_MAX * 4];

	if (rcs_node_path(c->v, nd, p, sizeof p) != 0)
		snprintf(p, sizeof p, "?");
	rcs_escape(e, sizeof e, p, sizeof p, 0);
	printf("%s%s:\n", first ? "" : "\n", e);
}

/* A level of the walks: the children of a node and the next one to visit. */
struct frame
{
	struct rcs_node	*kids;
	int		n;
	int		pos;
};

struct stack
{
	struct frame	*f;
	int		n, cap;
};

static int push(struct stack *s, struct rcs_node *kids, int n)
{
	struct frame	*nf;
	int		cap;

	if (s->n == s->cap)
	{
		cap = s->cap ? s->cap * 2 : 64;
		nf = realloc(s->f, (size_t)cap * sizeof *nf);
		if (!nf)
			return -1;
		s->f = nf;
		s->cap = cap;
	}
	s->f[s->n].kids = kids;
	s->f[s->n].n = n;
	s->f[s->n].pos = 0;
	s->n++;
	return 0;
}

static void stack_free(struct stack *s)
{
	while (s->n > 0)
		free(s->f[--s->n].kids);
	free(s->f);
	s->f = NULL;
	s->cap = 0;
}

static int children(struct rcs_ctx *c, const struct rcs_node *nd, struct rcs_node **kids, int *n,
		    const char *cmd)
{
	char	msg[RCS_MSG_MAX];

	if (rcs_children(c->v, nd, kids, n, msg, sizeof msg) != 0)
	{
		rcs_error("%s: %s", cmd, msg);
		return -1;
	}
	return 0;
}

static int do_ls(struct rcs_ctx *c, const struct rcs_node *start, int lng, int rec)
{
	struct stack	s = { NULL, 0, 0 };
	struct rcs_node	*kids, nd;
	int		n, i, first = 1;

	if (!rcs_is_dir(c->v, start))
	{
		list_line(c, start, lng);
		return 0;
	}
	if (children(c, start, &kids, &n, "ls"))
		return 1;
	if (rec)
		section_header(c, start, 1);
	for (i = 0; i < n; i++)
		list_line(c, &kids[i], lng);
	if (!rec)
	{
		free(kids);
		return 0;
	}
	first = 0;
	if (push(&s, kids, n))
	{
		free(kids);
		goto nomem;
	}
	/* the directories in preorder, each listed when it is reached */
	while (s.n > 0)
	{
		struct frame	*f = &s.f[s.n - 1];

		if (f->pos == f->n)
		{
			free(f->kids);
			s.n--;
			continue;
		}
		nd = f->kids[f->pos++];
		if (!rcs_is_dir(c->v, &nd))
			continue;
		if (children(c, &nd, &kids, &n, "ls"))
		{
			stack_free(&s);
			return 1;
		}
		section_header(c, &nd, first);
		for (i = 0; i < n; i++)
			list_line(c, &kids[i], lng);
		if (push(&s, kids, n))
		{
			free(kids);
			goto nomem;
		}
	}
	stack_free(&s);
	return 0;
nomem:
	stack_free(&s);
	rcs_error("ls: out of memory");
	return 1;
}

/* ---- tree ------------------------------------------------------------------ */

static int do_tree(struct rcs_ctx *c, const struct rcs_node *start)
{
	struct stack	s = { NULL, 0, 0 };
	struct rcs_node	*kids, nd;
	char		p[RCS_PATH_MAX], e[RCS_PATH_MAX * 4];
	int		n, i, depth;

	if (rcs_node_path(c->v, start, p, sizeof p) != 0)
		snprintf(p, sizeof p, "?");
	rcs_escape(e, sizeof e, p, sizeof p, 0);
	printf("%s\n", e);
	if (!rcs_is_dir(c->v, start))
		return 0;
	if (children(c, start, &kids, &n, "tree"))
		return 1;
	if (push(&s, kids, n))
	{
		free(kids);
		goto nomem;
	}
	while (s.n > 0)
	{
		struct frame	*f = &s.f[s.n - 1];
		int		last;

		if (f->pos == f->n)
		{
			free(f->kids);
			s.n--;
			continue;
		}
		nd = f->kids[f->pos++];
		last = f->pos == f->n;
		depth = s.n;	/* 1 for the children of start */
		if (depth > TREE_INDENT)
			printf("[%d] ", depth);
		else
			for (i = 0; i < depth - 1; i++)
				fputs(s.f[i].pos < s.f[i].n ? "|   " : "    ", stdout);
		fputs(last ? "`-- " : "|-- ", stdout);
		node_line(c, &nd, 0);
		if (!rcs_is_dir(c->v, &nd))
			continue;
		if (children(c, &nd, &kids, &n, "tree"))
		{
			stack_free(&s);
			return 1;
		}
		if (push(&s, kids, n))
		{
			free(kids);
			goto nomem;
		}
	}
	stack_free(&s);
	return 0;
nomem:
	stack_free(&s);
	rcs_error("tree: out of memory");
	return 1;
}

/* ---- stat ------------------------------------------------------------------ */

static void kv(const char *key, const char *fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 2, 3)))
#endif
	;

static void kv(const char *key, const char *fmt, ...)
{
	va_list	ap;

	printf("%s: ", key);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

/* key: "text" (escaped), or key: - for NULL. */
static void kv_text(const char *key, const char *s)
{
	printf("%s: ", key);
	if (s)
	{
		putchar('"');
		out_text(s, '"');
		putchar('"');
	}
	else
		putchar('-');
	putchar('\n');
}

static void kv_name(const char *key, const char *name)
{
	char	nb[NAME_BUF];

	kv(key, "%s", col_name(nb, name));
}

/* The colour word of a box spec (low 16 bits) or a TEDINFO, decoded. */
static void colour_word(unsigned w)
{
	kv("frame-color", "%u", (w >> 12) & 15);
	kv("text-color", "%u", (w >> 8) & 15);
	kv("text-mode", "%s", (w & 0x80) ? "opaque" : "transparent");
	kv("fill-pattern", "%u", (w >> 4) & 7);
	kv("fill-color", "%u", w & 15);
}

static void icon_fields(const ICONBLK *ib, const char *text)
{
	char	cb[16];

	kv_text("text", text);
	kv("char", "%s", char_str(cb, sizeof cb, (unsigned)ib->ib_char));
	kv("fg-color", "%u", ((unsigned)ib->ib_char >> 12) & 15);
	kv("bg-color", "%u", ((unsigned)ib->ib_char >> 8) & 15);
	kv("char-pos", "%d,%d", ib->ib_xchar, ib->ib_ychar);
	kv("icon", "%d,%d,%d,%d", ib->ib_xicon, ib->ib_yicon, ib->ib_wicon, ib->ib_hicon);
	kv("text-box", "%d,%d,%d,%d", ib->ib_xtext, ib->ib_ytext, ib->ib_wtext, ib->ib_htext);
	kv("data-bytes", "%lu", (unsigned long)rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon));
	kv("mask-bytes", "%lu", (unsigned long)rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon));
}

static void bitblk_fields(const BITBLK *bb)
{
	kv("wb", "%d", bb->bi_wb);
	kv("hl", "%d", bb->bi_hl);
	kv("bi-x", "%d", bb->bi_x);
	kv("bi-y", "%d", bb->bi_y);
	kv("color", "%d", bb->bi_color);
	kv("data-bytes", "%lu", (unsigned long)rsc_bitblk_bytes(bb));
}

static void stat_spec(const struct rcs_desc *d)
{
	unsigned	type = d->o->ob_type & 0xff;
	char		cb[16];
	int		i, n;

	switch (d->spec_kind)
	{
	case RSC_SPEC_VALUE:
		kv("spec", "0x%08lX", (unsigned long)d->value);
		if ((type == G_BOX || type == G_IBOX || type == G_BOXCHAR) && !(d->o->ob_flags & OF_INDIRECT))
		{
			if (type == G_BOXCHAR)
				kv("char", "%s", char_str(cb, sizeof cb, (unsigned)(d->value >> 24)));
			kv("frame", "%d", (int)(signed char)(unsigned char)((d->value >> 16) & 0xff));
			colour_word((unsigned)(d->value & 0xffff));
		}
		break;
	case RSC_SPEC_STRING:
		kv_text("text", d->str);
		break;
	case RSC_SPEC_TEDINFO:
		if (!d->ted)
		{
			kv("tedinfo", "-");
			break;
		}
		kv_text("text", d->ted->te_ptext);
		kv_text("template", d->ted->te_ptmplt);
		kv_text("validation", d->ted->te_pvalid);
		kv("font", "%d%s", d->ted->te_font, d->ted->te_font == 3 ? " (IBM)"
		   : d->ted->te_font == 5 ? " (SMALL)" : "");
		kv("fontid", "%d", d->ted->te_fontid);
		kv("fontsize", "%d", d->ted->te_fontsize);
		kv("just", "%d%s", d->ted->te_just, d->ted->te_just == 0 ? " (left)"
		   : d->ted->te_just == 1 ? " (right)" : d->ted->te_just == 2 ? " (center)" : "");
		kv("color", "0x%04X", (unsigned)d->ted->te_color & 0xffff);
		colour_word((unsigned)d->ted->te_color & 0xffff);
		kv("thickness", "%d", d->ted->te_thickness);
		kv("txtlen", "%d", d->ted->te_txtlen);
		kv("tmplen", "%d", d->ted->te_tmplen);
		break;
	case RSC_SPEC_ICONBLK:
		if (!d->icon)
		{
			kv("iconblk", "-");
			break;
		}
		icon_fields(d->icon, d->icon->ib_ptext);
		break;
	case RSC_SPEC_BITBLK:
		if (!d->bit)
		{
			kv("bitblk", "-");
			break;
		}
		bitblk_fields(d->bit);
		break;
	case RSC_SPEC_CICON:
		if (!d->cicon)
		{
			kv("cicon", "-");
			break;
		}
		icon_fields(&d->cicon->blk.monoblk, d->cicon->text);
		n = rsc_cicon_nversions(d->cicon);
		kv("versions", "%d", n);
		for (i = 0; i < n; i++)
		{
			const struct rsc_cicon_version	*x = rsc_cicon_version(d->cicon, i);
			char				key[24];

			if (!x)
				continue;
			snprintf(key, sizeof key, "version-%d", i);
			kv(key, "planes=%d selected=%s", x->c.num_planes, x->c.sel_data ? "yes" : "no");
		}
		break;
	}
}

static int do_stat(struct rcs_ctx *c, const struct rcs_node *nd)
{
	struct rcs_desc	d;
	char		p[RCS_PATH_MAX], e[RCS_PATH_MAX * 4], b[160], kb[8];
	int		cw, ch;

	if (rcs_describe(c->v, nd, &d) != 0)
	{
		rcs_error("stat: no such node");
		return 1;
	}
	if (rcs_node_path(c->v, nd, p, sizeof p) != 0)
		snprintf(p, sizeof p, "?");
	rcs_escape(e, sizeof e, p, sizeof p, 0);
	kv("path", "%s", e);
	rsc_cell(c->r, &cw, &ch);
	switch (d.kind)
	{
	case RCS_NODE_ROOT:
		kv("trees", "%d", d.ntrees);
		kv("strings", "%d", d.nfrstr);
		kv("images", "%d", d.nfrimg);
		if (d.cell_w)
			kv("cell", "%dx%d", d.cell_w, d.cell_h);
		else
			kv("cell", "file");
		kv("vrsn", "0x%04X", d.vrsn);
		break;
	case RCS_NODE_STRDIR:
	case RCS_NODE_IMGDIR:
		kv("entries", "%d", d.count);
		break;
	case RCS_NODE_TREE:
		kv("kind", "%s", d.tkind >= 0 && d.tkind <= 4 ? kind_names[d.tkind] : "UNKN");
		kv("kind-source", "%s", d.tkind < 0 ? "none" : d.tkind_guessed ? "guessed" : "file");
		kv_name("name", d.name);
		kv("index", "%d", d.tree);
		kv("objects", "%d", d.nobj);
		kv("root-children", "%d", d.nchildren);
		(void)kb;
		break;
	case RCS_NODE_OBJ:
		kv("tree", "%d", d.tree);
		kv("index", "%d", d.obj);
		kv_name("name", d.name);
		kv("reachable", "%s", d.reachable ? "yes" : "no");
		if (d.parent >= 0)
			kv("parent", "%d", d.parent);
		else
			kv("parent", "-");
		kv("children", "%d", d.nchildren);
		kv("next", "%d", d.o->ob_next);
		kv("head", "%d", d.o->ob_head);
		kv("tail", "%d", d.o->ob_tail);
		kv("type", "0x%04X %s", (unsigned)d.o->ob_type, type_str(b, sizeof b, d.o->ob_type));
		kv("flags", "0x%04X %s", (unsigned)d.o->ob_flags, flags_str(b, sizeof b, d.o->ob_flags));
		kv("state", "0x%04X %s", (unsigned)d.o->ob_state, state_str(b, sizeof b, d.o->ob_state));
		kv("x", "%s", coord(b, sizeof b, d.o->ob_x, cw != 0));
		kv("y", "%s", coord(b, sizeof b, d.o->ob_y, cw != 0));
		kv("w", "%s", coord(b, sizeof b, d.o->ob_width, cw != 0));
		kv("h", "%s", coord(b, sizeof b, d.o->ob_height, cw != 0));
		stat_spec(&d);
		break;
	case RCS_NODE_FRSTR:
		kv("index", "%d", d.index);
		kv_name("name", d.name);
		kv("length", "%lu", (unsigned long)strlen(d.text));
		kv_text("text", d.text);
		kv("alert", "%s", d.alert ? "yes" : "no");
		break;
	case RCS_NODE_FRIMG:
		kv("index", "%d", d.index);
		kv_name("name", d.name);
		bitblk_fields(d.bit);
		break;
	case RCS_NODE_ICON:
		kv("tree", "%d", d.tree);
		kv("object", "%d", d.obj);
		kv("entry", "%s", d.name);
		if (d.version >= 0)
			kv("version", "%d", d.version);
		else
			kv("version", "-");
		kv("planes", "%d", d.planes);
		kv("selected", "%s", d.sel ? "yes" : "no");
		kv("has-selected", "%s", d.has_sel ? "yes" : "no");
		kv("w", "%d", d.w);
		kv("h", "%d", d.h);
		kv("data-bytes", "%lu", (unsigned long)d.data_bytes);
		kv("mask-bytes", "%lu", (unsigned long)d.mask_bytes);
		break;
	}
	return 0;
}

/* ---- cat ------------------------------------------------------------------- */

static void text_line(const char *s)
{
	if (s)
		out_text(s, 1);
	putchar('\n');
}

static int do_cat(struct rcs_ctx *c, const struct rcs_node *nd, int verbose, const char *path)
{
	struct rcs_desc	d;
	char		pb[600];

	if (rcs_describe(c->v, nd, &d) != 0)
	{
		rcs_error("cat: no such node");
		return 1;
	}
	if (d.kind == RCS_NODE_ICON)
	{
		rcs_error("cat: %s: not supported yet", arg_path(pb, sizeof pb, path));
		return 1;
	}
	if (d.kind == RCS_NODE_FRSTR)
	{
		text_line(d.text);
		return 0;
	}
	if (d.kind == RCS_NODE_OBJ)
		switch (d.spec_kind)
		{
		case RSC_SPEC_STRING:
			if (!d.str)
				break;
			text_line(d.str);
			return 0;
		case RSC_SPEC_TEDINFO:
			if (!d.ted)
				break;
			text_line(d.ted->te_ptext);
			if (verbose)
			{
				text_line(d.ted->te_ptmplt);
				text_line(d.ted->te_pvalid);
			}
			return 0;
		case RSC_SPEC_ICONBLK:
			if (!d.icon)
				break;
			text_line(d.icon->ib_ptext);
			return 0;
		case RSC_SPEC_CICON:
			if (!d.cicon)
				break;
			text_line(d.cicon->text);
			return 0;
		default:
			break;
		}
	rcs_error("cat: %s: no text", arg_path(pb, sizeof pb, path));
	return 1;
}

/* ---- convert ----------------------------------------------------------------- */

static int ends_with_nocase(const char *s, const char *suf)
{
	size_t	n = strlen(s), m = strlen(suf), i;

	if (n < m)
		return 0;
	for (i = 0; i < m; i++)
	{
		int	a = (unsigned char)s[n - m + i], b = (unsigned char)suf[i];

		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (a != b)
			return 0;
	}
	return 1;
}

int rcs_write_file(const char *path, const void *buf, size_t len, int keep_mode, char *why, size_t size)
{
	struct stat	st;
	char		*tmp, real[PATH_MAX];
	FILE		*f;
	int		fd, bad, e;
	mode_t		mask, mode;

	/* a symbolic link stays: the file it points to is replaced */
	if (realpath(path, real) != NULL)
		path = real;
	tmp = malloc(strlen(path) + sizeof ".XXXXXX");
	if (!tmp)
	{
		snprintf(why, size, "out of memory");
		return -1;
	}
	strcpy(tmp, path);
	strcat(tmp, ".XXXXXX");
	fd = mkstemp(tmp);
	f = fd < 0 ? NULL : fdopen(fd, "wb");
	if (!f)
	{
		e = errno;
		if (fd >= 0)
		{
			close(fd);
			remove(tmp);
		}
		snprintf(why, size, "%s", strerror(e));
		free(tmp);
		return -1;
	}
	/* mkstemp makes the file private: give it the mode of the file it
	 * replaces, or the one fopen would have */
	mask = umask(0);
	umask(mask);
	mode = 0666 & ~mask;
	if (keep_mode && stat(path, &st) == 0 && S_ISREG(st.st_mode))
		mode = st.st_mode & 07777;
	bad = fchmod(fd, mode) != 0;
	if (!bad)
		bad = fwrite(buf, 1, len, f) != len;
	if (fclose(f) != 0)
		bad = 1;
	if (!bad)
		bad = rename(tmp, path) != 0;
	if (bad)
	{
		e = errno;
		remove(tmp);
		snprintf(why, size, "%s", strerror(e));
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}

/* Encodes the resource first, then writes it to a temporary file next to
 * OUT and renames that over OUT once it is written and closed; on any
 * failure the temporary file is removed and an existing OUT is untouched. */
static int do_convert(struct rcs_ctx *c, int argc, char **argv)
{
	struct rsc_error	err;
	uint8_t			*buf = NULL;
	size_t			len = 0;
	const char		*out;
	char			pb[600], why[200];
	int			cw, ch;

	if (argc != 3 || strcmp(argv[1], "-o") != 0 || argv[2][0] == 0)
	{
		rcs_error("convert: usage: convert -o OUT.rsc");
		return 1;
	}
	out = argv[2];
	if (!ends_with_nocase(out, ".rsc"))
	{
		rcs_error("convert: %s: output format not supported yet", arg_path(pb, sizeof pb, out));
		return 1;
	}
	rsc_cell(c->r, &cw, &ch);
	if (rsc_atari_write(c->r, cw, ch, &buf, &len, &err) != 0)
	{
		rcs_error("convert: %s", err.msg);
		return 1;
	}
	if (rcs_write_file(out, buf, len, 0, why, sizeof why) != 0)
	{
		rsc_mfree(buf);
		rcs_error("convert: %s: cannot write: %s", arg_path(pb, sizeof pb, out), why);
		return 1;
	}
	rsc_mfree(buf);
	return 0;
}

/* ---- the dispatcher ------------------------------------------------------------ */

/* Parses the single-letter options of cmd in argv[1..]: letters in allowed
 * set bits of *set (bit = position in allowed).  Returns the index of the
 * first operand, or -1 after an error line. */
static int options(const char *cmd, int argc, char **argv, const char *allowed, unsigned *set)
{
	int		i;
	const char	*p, *q;
	char		ob[64];

	*set = 0;
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
	{
		if (strcmp(argv[i], "--") == 0)
			return i + 1;
		for (p = argv[i] + 1; *p; p++)
		{
			q = strchr(allowed, *p);
			if (!q || *p == 0)
			{
				rcs_error("%s: unknown option '%s'", cmd, arg_path(ob, sizeof ob, argv[i]));
				return -1;
			}
			*set |= 1u << (q - allowed);
		}
	}
	return i;
}

/* ---- editing: files, targets, names ------------------------------------------- */

static int lower_c(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* Equal, the ASCII letters compared without case. */
static int eq_ci(const char *a, const char *b)
{
	while (*a && lower_c((unsigned char)*a) == lower_c((unsigned char)*b))
	{
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

/* The longest FILE of a FILE:/PATH argument. */
#define FILE_BUF	1024
/* The longest component of a new name looked at. */
#define COMP_BUF	256

static int same_file(const char *a, const char *b)
{
	struct stat	sa, sb;

	return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void main_src(struct rcs_ctx *c, struct rcs_src *s)
{
	s->r = c->r;
	s->n = c->n;
	s->v = c->v;
	s->file = NULL;
	s->is_main = 1;
}

void rcs_ctx_free_others(struct rcs_ctx *c)
{
	int	i;

	for (i = 0; i < c->nother; i++)
	{
		rcs_view_free(c->other[i].v);
		rsc_names_free(c->other[i].n);
		rsc_free(c->other[i].r);
		free(c->other[i].file);
	}
	c->nother = 0;
}

/* The resource an argument is in: "/PATH" (or anything without ":/") is in
 * the main file, "FILE:/PATH" in FILE (the main file again when FILE is it,
 * else a second file, opened once).  *path is the path part.  0, or -1 after
 * an error line. */
static int arg_side(struct rcs_ctx *c, const char *cmd, const char *arg, struct rcs_src *s,
		    const char **path)
{
	const char	*p;
	char		file[FILE_BUF], eb[300];
	size_t		len;
	int		i;

	/* FILE must end in .rsc and be a regular file that exists: otherwise
	 * the argument is a path of the main file ("/a.rsc:/b") */
	for (p = strstr(arg, ":/"); p; p = strstr(p + 1, ":/"))
		if ((size_t)(p - arg) >= 4 && (size_t)(p - arg) < sizeof file
		    && lower_c((unsigned char)p[-4]) == '.' && lower_c((unsigned char)p[-3]) == 'r'
		    && lower_c((unsigned char)p[-2]) == 's' && lower_c((unsigned char)p[-1]) == 'c')
		{
			struct stat	fst;

			memcpy(file, arg, (size_t)(p - arg));
			file[p - arg] = 0;
			if (stat(file, &fst) == 0 && S_ISREG(fst.st_mode))
				break;
		}
	if (!p)
	{
		main_src(c, s);
		*path = arg;
		return 0;
	}
	len = (size_t)(p - arg);
	if (len == 0 || len >= sizeof file)
	{
		rcs_error("%s: bad path '%s': %s", cmd, arg_path(eb, sizeof eb, arg),
			  len ? "file name too long" : "no file name before :/");
		return -1;
	}
	memcpy(file, arg, len);
	file[len] = 0;
	*path = p + 1;
	if (c->file && same_file(c->file, file))
	{
		main_src(c, s);
		return 0;
	}
	for (i = 0; i < c->nother; i++)
		if (strcmp(c->other[i].file, file) == 0)
		{
			*s = c->other[i];
			return 0;
		}
	if (c->nother >= RCS_OTHER_MAX)
	{
		rcs_error("%s: too many files (at most %d second files)", cmd, RCS_OTHER_MAX);
		return -1;
	}
	if (!c->load || c->load(c, file, &c->other[c->nother]) != 0)
		return -1;
	c->other[c->nother].file = malloc(len + 1);
	c->other[c->nother].is_main = 0;
	if (!c->other[c->nother].file)
	{
		rcs_view_free(c->other[c->nother].v);
		rsc_names_free(c->other[c->nother].n);
		rsc_free(c->other[c->nother].r);
		rcs_error("out of memory");
		return -1;
	}
	strcpy(c->other[c->nother].file, file);
	*s = c->other[c->nother++];
	return 0;
}

static int side_resolve(const char *cmd, struct rcs_src *s, const char *path, struct rcs_node *nd)
{
	char	msg[RCS_MSG_MAX];

	if (rcs_resolve(s->v, path, nd, msg, sizeof msg) != 0)
	{
		rcs_error("%s: %s", cmd, msg);
		return -1;
	}
	return 0;
}

/* The destination of cp and mv: an existing node, or a new name below an
 * existing parent. */
struct target
{
	int		exists;
	struct rcs_node	node;		/* the node, or the parent of the new name */
	char		name[COMP_BUF];	/* the new name */
	char		arg[200];	/* the argument, escaped, for messages */
};

static int dst_target(struct rcs_ctx *c, const char *cmd, const char *arg, struct target *t)
{
	struct rcs_src	s;
	struct rcs_node	parent, found;
	const char	*path, *slash, *last;
	char		msg[RCS_MSG_MAX], msg2[RCS_MSG_MAX], par[RCS_PATH_MAX];
	size_t		n, i;
	int		k;

	arg_path(t->arg, sizeof t->arg, arg);
	if (arg_side(c, cmd, arg, &s, &path) != 0)
		return -1;
	if (!s.is_main)
	{
		rcs_error("%s: %s: a second file is read-only (it is never written)", cmd, t->arg);
		return -1;
	}
	t->exists = 0;
	if (rcs_resolve(c->v, path, &t->node, msg, sizeof msg) == 0)
	{
		t->exists = 1;
		return 0;
	}
	n = strlen(path);
	while (n > 1 && path[n - 1] == '/')
		n--;
	slash = NULL;
	for (i = 0; i < n; i++)
		if (path[i] == '/')
			slash = path + i;
	if (path[0] != '/' || !slash || strncmp(msg, "bad path", 8) == 0 || n >= RCS_PATH_MAX
	    || (size_t)(path + n - slash - 1) >= sizeof t->name)
		goto fail;
	last = slash + 1;
	if (slash == path)
		strcpy(par, "/");
	else
	{
		memcpy(par, path, (size_t)(slash - path));
		par[slash - path] = 0;
	}
	if (rcs_resolve(c->v, par, &parent, msg2, sizeof msg2) != 0)
		goto fail;
	memcpy(t->name, last, (size_t)(path + n - last));
	t->name[path + n - last] = 0;
	k = rcs_child_by_name(c->v, &parent, t->name, &found);
	if (k == -2)
		goto fail;
	if (k == 0)
	{
		t->exists = 1;
		t->node = found;
		return 0;
	}
	t->node = parent;
	return 0;
fail:
	rcs_error("%s: %s", cmd, msg);
	return -1;
}

/* The scratch name of a node for a message. */
static const char *node_path_str(struct rcs_src *s, const struct rcs_node *nd, char *buf, size_t size)
{
	char	raw[RCS_PATH_MAX];

	if (rcs_node_path(s->v, nd, raw, sizeof raw) != 0)
		snprintf(raw, sizeof raw, "?");
	rcs_escape(buf, size, raw, (size_t)-1, 1);
	return buf;
}

/* Whether a name can be given: for a new name from the command line, with
 * an error line; at_root for a tree. */
static int new_name_ok(const char *cmd, const char *name, int at_root)
{
	char	why[100], eb[300];

	if (rcs_check_new_name(name, at_root, why, sizeof why) != 0)
	{
		rcs_error("%s: '%s': %s", cmd, arg_path(eb, sizeof eb, name), why);
		return -1;
	}
	return 0;
}

/* Whether name is free in its scope (kind as rcs_name_taken; skip: the node
 * that may have it); with an error line when it is not. */
static int name_free(struct rcs_ctx *c, const char *cmd, int kind, int tree, const char *name, int skip)
{
	char	eb[300];
	int	k = rcs_name_taken(c->n, kind, tree, name, skip);

	if (k < 0)
		return 0;
	rcs_error("%s: the name '%s' is used by %s #%d already%s", cmd, arg_path(eb, sizeof eb, name),
		  kind == RSC_NAME_TREE ? "tree" : kind == RSC_NAME_OBJ ? "object" :
		  kind == RSC_NAME_FRSTR ? "free string" : "free image", k,
		  kind == RSC_NAME_OBJ ? " in that tree (give the new object another name)" : "");
	return -1;
}

/* The tkind of a free string entry: ALRT for the shape of an alert. */
static int str_tkind(const char *text)
{
	return rcs_is_alert(text) ? RSC_TKIND_ALRT : RSC_TKIND_UNKN;
}

/* Removes object i of tree t with its subtree, drops the unreachable
 * objects of the tree and follows with the names.  *first is the lowest old
 * index that got a new index (-1: none) and *moved how many did.  0, or -1
 * (nothing is changed when the deletion itself fails). */
static int delete_obj(struct rcs_ctx *c, int t, int i, int *first, int *moved)
{
	int	*map, nobj, k;

	*first = -1;
	*moved = 0;
	if (!rsc_tree_objects(c->r, t, &nobj))
		return -1;
	map = malloc((size_t)nobj * sizeof *map);
	if (!map)
		return -1;
	if (rsc_obj_delete(c->r, t, i) != 0)
	{
		free(map);
		return -1;
	}
	if (rsc_tree_compact(c->r, t, map) < 0 || rsc_names_renumber(c->n, t, map, nobj) != 0)
	{
		free(map);
		return -1;
	}
	for (k = 0; k < nobj; k++)
		if (map[k] >= 0 && map[k] != k)
		{
			if (*first < 0)
				*first = k;
			(*moved)++;
		}
	free(map);
	return 0;
}

/* The names helper for renaming: removes the entries of a node and adds
 * the entry name (guessed 0). */
static int rename_entry(struct rcs_ctx *c, const struct rcs_node *nd, const char *name)
{
	struct rsc_name	e;
	int		tkind = 0, kind, a = 0, b = 0;

	switch (nd->kind)
	{
	case RCS_NODE_TREE:
		kind = RSC_NAME_TREE;
		a = nd->tree;
		tkind = rsc_names_tree_kind(c->n, nd->tree, NULL);
		if (tkind < 0)
			tkind = RSC_TKIND_UNKN;
		break;
	case RCS_NODE_OBJ:
		kind = RSC_NAME_OBJ;
		a = nd->tree;
		b = nd->obj;
		break;
	case RCS_NODE_FRSTR:
		kind = RSC_NAME_FRSTR;
		b = nd->index;
		tkind = str_tkind(rsc_frstr(c->r, nd->index));
		break;
	case RCS_NODE_FRIMG:
		kind = RSC_NAME_FRIMG;
		b = nd->index;
		break;
	default:
		return -1;
	}
	(void)e;
	rsc_names_remove(c->n, kind, a, b);
	return rsc_names_add(c->n, kind, tkind, a, b, name, 0);
}

/* Renames a node (tree, object, free string or image) after the checks. */
static int rename_node(struct rcs_ctx *c, const char *cmd, const struct rcs_node *nd, const char *name)
{
	int	kind, tree = 0, skip;

	switch (nd->kind)
	{
	case RCS_NODE_TREE:
		kind = RSC_NAME_TREE;
		skip = nd->tree;
		break;
	case RCS_NODE_OBJ:
		kind = RSC_NAME_OBJ;
		tree = nd->tree;
		skip = nd->obj;
		break;
	case RCS_NODE_FRSTR:
		kind = RSC_NAME_FRSTR;
		skip = nd->index;
		break;
	case RCS_NODE_FRIMG:
		kind = RSC_NAME_FRIMG;
		skip = nd->index;
		break;
	default:
		rcs_error("%s: only trees, objects, free strings and free images have names", cmd);
		return -1;
	}
	if (new_name_ok(cmd, name, nd->kind == RCS_NODE_TREE) != 0 || name_free(c, cmd, kind, tree, name, skip) != 0)
		return -1;
	if (rename_entry(c, nd, name) != 0)
	{
		rcs_error("%s: cannot add the name (the name list is full)", cmd);
		return -1;
	}
	return 0;
}

/* Copies the first name of src node nd in S into the list as a new entry
 * unless it is only a guess; *name gets it (borrowed copy in buf) or NULL. */
static const char *carried_name(struct rcs_src *S, const struct rcs_node *nd, char *buf)
{
	struct rsc_name	e;

	if (rcs_node_entry(S->v, nd, &e) != 0 || e.guessed)
		return NULL;
	strcpy(buf, e.name);
	return buf;
}

/* Where an object goes: the tree, the parent object and the new name (NULL:
 * keeps its name). */
static int obj_place(struct rcs_ctx *c, const char *cmd, const struct target *t, int *dt, int *dparent,
		     const char **newname)
{
	struct rcs_desc	d;
	const char	*what = "an object can only go into a tree or an object";

	*newname = NULL;
	if (t->node.kind == RCS_NODE_TREE)
	{
		*dt = t->node.tree;
		*dparent = 0;
		if (!t->exists)
			*newname = t->name;
		return 0;
	}
	if (t->node.kind != RCS_NODE_OBJ)
		goto bad;
	if (rcs_describe(c->v, &t->node, &d) != 0)
		goto bad;
	if (!d.reachable)
	{
		rcs_error("%s: %s: not reachable from the root of its tree", cmd, t->arg);
		return -1;
	}
	if (t->exists && d.nchildren <= 0)
	{
		rcs_error("%s: %s: exists and is not a directory (it has no children; to put something inside, "
			  "give a name: %s/NAME)", cmd, t->arg, t->arg);
		return -1;
	}
	*dt = t->node.tree;
	*dparent = t->node.obj;
	if (!t->exists)
		*newname = t->name;
	return 0;
bad:
	rcs_error("%s: %s: %s", cmd, t->arg, what);
	return -1;
}

/* Copies the subtree of object sn of S below dparent of tree dt of c, as its
 * last child, with the names: the root gets rootname (NULL: none), the
 * descendants theirs unless the tree dt has them already: that is an error
 * with mode 1, else (mode 0) they are left out and counted in *dropped; mode
 * 2 does no names at all (the caller does).  Returns the
 * index of the copy, or -1 after an error line (nothing changed).  *map
 * (malloc'd, *nmap entries) is for the caller to free. */
static int copy_subtree(struct rcs_ctx *c, const char *cmd, struct rcs_src *S, const struct rcs_node *sn,
			int dt, int dparent, const char *rootname, int mode, int **mapp, int *nmapp,
			int *dropped)
{
	struct rsc_names	*tmp;
	struct rsc_name		e;
	int			*map, nobj, idx, i, cnt, first, moved, bad = 0;
	char			eb[300];

	*mapp = NULL;
	*dropped = 0;
	if (!rsc_tree_objects(S->r, sn->tree, &nobj))
		return -1;
	map = malloc((size_t)nobj * sizeof *map);
	tmp = rsc_names_new();
	if (!map || !tmp)
	{
		free(map);
		rsc_names_free(tmp);
		rcs_error("out of memory");
		return -1;
	}
	idx = rsc_obj_copy_subtree(c->r, dt, dparent, -1, S->r, sn->tree, sn->obj, map);
	if (idx < 0)
	{
		free(map);
		rsc_names_free(tmp);
		rcs_error("%s: cannot copy: the tree would have more than %d objects, a link is broken or memory ran out",
			  cmd, RSC_MAX_OBJECTS);
		return -1;
	}
	cnt = mode == 2 ? 0 : rsc_names_count(S->n);
	for (i = 0; i < cnt && !bad; i++)
	{
		if (rsc_names_get(S->n, i, &e) != 0 || e.kind != RSC_NAME_OBJ || e.tree != sn->tree
		    || e.obj >= nobj || map[e.obj] < 0 || e.obj == sn->obj)
			continue;
		if (rcs_name_taken(c->n, RSC_NAME_OBJ, dt, e.name, -1) >= 0)
		{
			if (mode == 1)
			{
				rcs_error("%s: the name '%s' of an object in the source is used in the target tree already",
					  cmd, arg_path(eb, sizeof eb, e.name));
				bad = 1;
			}
			else
				(*dropped)++;
			continue;
		}
		if (rsc_names_add(tmp, RSC_NAME_OBJ, 0, e.tree, e.obj, e.name, e.guessed) != 0)
			bad = 2;
	}
	if (!bad && mode != 2 && rsc_names_copy_objs(c->n, dt, tmp, sn->tree, map, nobj) < 0)
		bad = 2;
	if (!bad && rootname && rsc_names_add(c->n, RSC_NAME_OBJ, 0, dt, idx, rootname, 0) != 0)
		bad = 2;
	rsc_names_free(tmp);
	if (bad)
	{
		delete_obj(c, dt, idx, &first, &moved);
		free(map);
		if (bad == 2)
			rcs_error("%s: cannot add the names (the name list is full or memory ran out)", cmd);
		return -1;
	}
	*mapp = map;
	*nmapp = nobj;
	return idx;
}

/* ---- cp ---------------------------------------------------------------------- */

static int cp_obj(struct rcs_ctx *c, struct rcs_src *S, const struct rcs_node *sn, const char *sb,
		  const struct target *t, int rflag)
{
	struct rcs_desc	d;
	const char	*newname, *rootname;
	char		nb[RSC_NAME_MAX + 1];
	int		dt, dparent, idx, *map, nmap, dropped;

	if (rcs_describe(S->v, sn, &d) != 0 || !d.reachable)
	{
		rcs_error("cp: %s: not reachable from the root of its tree", sb);
		return 1;
	}
	if (d.nchildren > 0 && !rflag)
	{
		rcs_error("cp: %s: is a directory (use -r)", sb);
		return 1;
	}
	if (obj_place(c, "cp", t, &dt, &dparent, &newname) != 0)
		return 1;
	if (newname)
	{
		if (new_name_ok("cp", newname, 0) != 0)
			return 1;
		rootname = newname;
	}
	else
		rootname = carried_name(S, sn, nb);
	if (rootname && name_free(c, "cp", RSC_NAME_OBJ, dt, rootname, -1) != 0)
		return 1;
	idx = copy_subtree(c, "cp", S, sn, dt, dparent, rootname, 0, &map, &nmap, &dropped);
	if (idx < 0)
		return 1;
	free(map);
	if (dropped)
		rcs_note("cp: %d %s of objects in the copy %s not carried: the tree has them already", dropped,
			 plural(dropped, "name", "names"), plural(dropped, "was", "were"));
	return 0;
}

static int cp_tree(struct rcs_ctx *c, struct rcs_src *S, const struct rcs_node *sn, const char *sb,
		   const struct target *t, int rflag)
{
	const char	*name = NULL;
	char		nb[RSC_NAME_MAX + 1];
	int		*map, nobj, newt, tkind;

	if (!rflag)
	{
		rcs_error("cp: %s: is a directory (use -r)", sb);
		return 1;
	}
	if (t->node.kind != RCS_NODE_ROOT)
	{
		rcs_error("cp: %s: %s", t->arg, t->exists && t->node.kind == RCS_NODE_TREE ? "already exists"
			  : "a tree can only be copied to /");
		return 1;
	}
	if (t->exists)
		name = carried_name(S, sn, nb);
	else
	{
		name = t->name;
		if (new_name_ok("cp", name, 1) != 0)
			return 1;
	}
	if (name && name_free(c, "cp", RSC_NAME_TREE, 0, name, -1) != 0)
		return 1;
	tkind = rsc_names_tree_kind(S->n, sn->tree, NULL);
	if (tkind < 0)
		tkind = RSC_TKIND_UNKN;
	if (!rsc_tree_objects(S->r, sn->tree, &nobj))
		return 1;
	map = malloc((size_t)nobj * sizeof *map);
	if (!map)
	{
		rcs_error("out of memory");
		return 1;
	}
	newt = rsc_tree_copy(c->r, -1, S->r, sn->tree, map);
	if (newt < 0)
	{
		free(map);
		rcs_error("cp: cannot copy: the tree has a broken link or memory ran out");
		return 1;
	}
	if (rsc_names_copy_objs(c->n, newt, S->n, sn->tree, map, nobj) < 0)
	{
		rsc_tree_delete(c->r, newt);
		free(map);
		rcs_error("cp: cannot add the names (the name list is full or memory ran out)");
		return 1;
	}
	free(map);
	if (name && rsc_names_add(c->n, RSC_NAME_TREE, tkind, newt, 0, name, 0) != 0)
	{
		rsc_tree_delete(c->r, newt);
		rsc_names_tree_removed(c->n, newt);
		rcs_error("cp: cannot add the name (the name list is full)");
		return 1;
	}
	return 0;
}

static int cp_free(struct rcs_ctx *c, struct rcs_src *S, const struct rcs_node *sn, const struct target *t)
{
	int		str = sn->kind == RCS_NODE_FRSTR;
	int		dirkind = str ? RCS_NODE_STRDIR : RCS_NODE_IMGDIR;
	int		nkind = str ? RSC_NAME_FRSTR : RSC_NAME_FRIMG;
	const char	*name = NULL, *text = NULL;
	char		nb[RSC_NAME_MAX + 1];
	BITBLK		bb;
	int		idx, k, tkind = RSC_TKIND_UNKN;

	if (t->node.kind != dirkind)
	{
		rcs_error("cp: %s: %s", t->arg, t->exists && t->node.kind == sn->kind ? "already exists"
			  : str ? "a free string can only be copied to /.strings"
			  : "a free image can only be copied to /.images");
		return 1;
	}
	if (t->exists)
		name = carried_name(S, sn, nb);
	else
	{
		name = t->name;
		if (new_name_ok("cp", name, 0) != 0)
			return 1;
	}
	if (name && name_free(c, "cp", nkind, 0, name, -1) != 0)
		return 1;
	if (str)
	{
		text = rsc_frstr(S->r, sn->index);
		tkind = str_tkind(text);
	}
	idx = str ? rsc_nfrstr(c->r) : rsc_nfrimg(c->r);
	if (name && rsc_names_add(c->n, nkind, tkind, 0, idx, name, 0) != 0)
	{
		rcs_error("cp: cannot add the name (the name list is full)");
		return 1;
	}
	if (str)
		k = rsc_frstr_add(c->r, text);
	else
	{
		bb = *rsc_frimg(S->r, sn->index);
		k = rsc_frimg_add(c->r, &bb);
	}
	if (k != idx)
	{
		if (name)
			rsc_names_remove(c->n, nkind, 0, idx);
		rcs_error("cp: cannot copy (memory ran out)");
		return 1;
	}
	return 0;
}

static int do_cp(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_src	S;
	struct rcs_node	sn;
	struct target	t;
	const char	*spath;
	char		sb[200];
	unsigned	set;
	int		i, rc;

	i = options("cp", argc, argv, "r", &set);
	if (i < 0)
		return 1;
	if (argc - i != 2)
	{
		rcs_error("cp: usage: cp [-r] SRC DST");
		return 1;
	}
	if (arg_side(c, "cp", argv[i], &S, &spath) != 0 || side_resolve("cp", &S, spath, &sn) != 0)
		return 1;
	arg_path(sb, sizeof sb, argv[i]);
	if (dst_target(c, "cp", argv[i + 1], &t) != 0)
		return 1;
	switch (sn.kind)
	{
	case RCS_NODE_OBJ:
		rc = cp_obj(c, &S, &sn, sb, &t, (set & 1) != 0);
		break;
	case RCS_NODE_TREE:
		rc = cp_tree(c, &S, &sn, sb, &t, (set & 1) != 0);
		break;
	case RCS_NODE_FRSTR:
	case RCS_NODE_FRIMG:
		rc = cp_free(c, &S, &sn, &t);
		break;
	case RCS_NODE_ICON:
		rcs_error("cp: %s: colour icon entries cannot be copied yet (copy the object)", sb);
		return 1;
	default:
		rcs_error("cp: %s: cannot be copied", sb);
		return 1;
	}
	if (rc == 0 && !c->no_guess && rsc_names_guess(c->n, c->r) < 0)
	{
		rcs_error("cp: cannot name the copy: the name list has no room (use --no-guess)");
		return 1;
	}
	return rc;
}

/* ---- mv ---------------------------------------------------------------------- */

static int mv_obj(struct rcs_ctx *c, struct rcs_src *S, const struct rcs_node *sn, const char *sb,
		  const struct target *t)
{
	struct rcs_desc	d;
	struct rsc_name	e;
	const char	*newname;
	int		dt, dparent, cur, p, steps, nobj, idx, *map, nmap, dropped, i, cnt, first, moved, clash = 0;

	if (rcs_describe(S->v, sn, &d) != 0 || !d.reachable)
	{
		rcs_error("mv: %s: not reachable from the root of its tree", sb);
		return 1;
	}
	if (sn->obj == 0)
	{
		rcs_error("mv: %s: the root of a tree cannot be moved (it is the tree: mv /TREE /NEW)", sb);
		return 1;
	}
	if (t->exists && rcs_node_eq(sn, &t->node))
	{
		rcs_error("mv: %s and %s are the same node", sb, t->arg);
		return 1;
	}
	if (obj_place(c, "mv", t, &dt, &dparent, &newname) != 0)
		return 1;
	if (newname && new_name_ok("mv", newname, 0) != 0)
		return 1;
	if (dt == sn->tree)
	{
		rsc_tree_objects(c->r, dt, &nobj);
		for (p = dparent, steps = 0; p >= 0 && steps <= nobj; steps++)
		{
			if (p == sn->obj)
			{
				rcs_error("mv: %s: is inside %s", t->arg, sb);
				return 1;
			}
			p = rcs_obj_parent(c->v, dt, p);
		}
		if (newname && name_free(c, "mv", RSC_NAME_OBJ, dt, newname, sn->obj) != 0)
			return 1;
		cur = rcs_obj_parent(c->v, dt, sn->obj);
		if (!(newname && dparent == cur) && rsc_obj_move(c->r, dt, sn->obj, dparent, -1) != 0)
		{
			rcs_error("mv: cannot move %s (broken links or memory ran out)", sb);
			return 1;
		}
		if (newname && rename_entry(c, sn, newname) != 0)
		{
			rcs_error("mv: cannot add the name (the name list is full)");
			return 1;
		}
		return 0;
	}
	if (newname && name_free(c, "mv", RSC_NAME_OBJ, dt, newname, -1) != 0)
		return 1;
	idx = copy_subtree(c, "mv", S, sn, dt, dparent, NULL, 2, &map, &nmap, &dropped);
	if (idx < 0)
		return 1;
	cnt = rsc_names_count(c->n);
	for (i = 0; i < cnt && !clash; i++)
	{
		char	eb[300];

		if (rsc_names_get(c->n, i, &e) != 0 || e.kind != RSC_NAME_OBJ || e.tree != sn->tree
		    || e.obj >= nmap || map[e.obj] < 0 || (newname && e.obj == sn->obj))
			continue;
		if (rcs_name_taken(c->n, RSC_NAME_OBJ, dt, e.name, -1) >= 0)
		{
			rcs_error("mv: the name '%s' of an object in %s is used in the target tree already",
				  arg_path(eb, sizeof eb, e.name), sb);
			clash = 1;
		}
	}
	if (clash)
	{
		delete_obj(c, dt, idx, &first, &moved);
		free(map);
		return 1;
	}
	rsc_names_move_objs(c->n, sn->tree, dt, map, nmap);
	free(map);
	if (newname)
	{
		struct rcs_node	nn = *sn;

		nn.tree = dt;
		nn.obj = idx;
		if (rename_entry(c, &nn, newname) != 0)
		{
			rcs_error("mv: cannot add the name (the name list is full)");
			return 1;
		}
	}
	if (delete_obj(c, sn->tree, sn->obj, &first, &moved) != 0)
	{
		rcs_error("mv: cannot remove the source %s", sb);
		return 1;
	}
	if (moved > 0)
		rcs_note("mv: %d %s of the source tree %s renumbered (from #%d on)", moved,
			 plural(moved, "object", "objects"), plural(moved, "was", "were"), first);
	return 0;
}

static int mv_tree(struct rcs_ctx *c, const struct rcs_node *sn, const struct target *t)
{
	int	n;

	if (t->node.kind != RCS_NODE_ROOT)
	{
		rcs_error("mv: %s: %s", t->arg, t->exists && t->node.kind == RCS_NODE_TREE ? "already exists"
			  : "a tree can only go to /");
		return 1;
	}
	if (!t->exists)
		return rename_node(c, "mv", sn, t->name) != 0;
	n = rsc_ntrees(c->r);
	if (rsc_tree_move(c->r, sn->tree, -1) != 0 || rsc_names_tree_moved(c->n, sn->tree, n - 1) != 0)
	{
		rcs_error("mv: cannot move the tree");
		return 1;
	}
	return 0;
}

static int mv_free(struct rcs_ctx *c, const struct rcs_node *sn, const struct target *t)
{
	int	str = sn->kind == RCS_NODE_FRSTR, n;

	if (t->node.kind != (str ? RCS_NODE_STRDIR : RCS_NODE_IMGDIR))
	{
		rcs_error("mv: %s: %s", t->arg, t->exists && t->node.kind == sn->kind ? "already exists"
			  : str ? "a free string can only go to /.strings" : "a free image can only go to /.images");
		return 1;
	}
	if (!t->exists)
		return rename_node(c, "mv", sn, t->name) != 0;
	n = str ? rsc_nfrstr(c->r) : rsc_nfrimg(c->r);
	if ((str ? rsc_frstr_move(c->r, sn->index, -1) : rsc_frimg_move(c->r, sn->index, -1)) != 0
	    || (str ? rsc_names_str_moved(c->n, sn->index, n - 1) : rsc_names_img_moved(c->n, sn->index, n - 1)) != 0)
	{
		rcs_error("mv: cannot move the item");
		return 1;
	}
	return 0;
}

static int do_mv(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_src	S;
	struct rcs_node	sn;
	struct target	t;
	const char	*spath;
	char		sb[200];
	unsigned	set;
	int		i;

	i = options("mv", argc, argv, "", &set);
	if (i < 0)
		return 1;
	if (argc - i != 2)
	{
		rcs_error("mv: usage: mv SRC DST");
		return 1;
	}
	arg_path(sb, sizeof sb, argv[i]);
	if (arg_side(c, "mv", argv[i], &S, &spath) != 0)
		return 1;
	if (!S.is_main)
	{
		rcs_error("mv: %s: a second file is read-only, so nothing can be moved out of it (use cp)", sb);
		return 1;
	}
	if (side_resolve("mv", &S, spath, &sn) != 0 || dst_target(c, "mv", argv[i + 1], &t) != 0)
		return 1;
	switch (sn.kind)
	{
	case RCS_NODE_OBJ:
		return mv_obj(c, &S, &sn, sb, &t);
	case RCS_NODE_TREE:
		return mv_tree(c, &sn, &t);
	case RCS_NODE_FRSTR:
	case RCS_NODE_FRIMG:
		return mv_free(c, &sn, &t);
	case RCS_NODE_ICON:
		rcs_error("mv: %s: colour icon entries cannot be moved yet", sb);
		return 1;
	default:
		rcs_error("mv: %s: cannot be moved", sb);
		return 1;
	}
}

/* ---- rm ---------------------------------------------------------------------- */

static int do_rm(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_src	S;
	struct rcs_node	sn, tn;
	struct rcs_desc	d;
	const char	*path;
	char		sb[200], tb[200];
	unsigned	set;
	int		i, first, moved, n;

	i = options("rm", argc, argv, "r", &set);
	if (i < 0)
		return 1;
	if (argc - i != 1)
	{
		rcs_error(argc - i < 1 ? "rm: missing PATH" : "rm: too many arguments");
		return 1;
	}
	arg_path(sb, sizeof sb, argv[i]);
	if (arg_side(c, "rm", argv[i], &S, &path) != 0)
		return 1;
	if (!S.is_main)
	{
		rcs_error("rm: %s: a second file is read-only", sb);
		return 1;
	}
	if (side_resolve("rm", &S, path, &sn) != 0)
		return 1;
	switch (sn.kind)
	{
	case RCS_NODE_OBJ:
		if (rcs_describe(c->v, &sn, &d) != 0)
			return 1;
		if (sn.obj == 0)
		{
			rcs_error("rm: %s: the root of a tree cannot be removed (remove the tree: rm -r /TREE)", sb);
			return 1;
		}
		if (!d.reachable)
		{
			rcs_error("rm: %s: not reachable from the root of its tree (a write drops it)", sb);
			return 1;
		}
		if (d.nchildren > 0 && !(set & 1))
		{
			rcs_error("rm: %s: is a directory (use -r)", sb);
			return 1;
		}
		tn = sn;
		tn.kind = RCS_NODE_TREE;
		tn.obj = 0;
		node_path_str(&S, &tn, tb, sizeof tb);
		if (delete_obj(c, sn.tree, sn.obj, &first, &moved) != 0)
		{
			rcs_error("rm: cannot remove %s (broken links or memory ran out)", sb);
			return 1;
		}
		if (moved > 0)
			rcs_note("rm: %d %s of %s %s renumbered (from #%d on)", moved, plural(moved, "object", "objects"), tb,
				 plural(moved, "was", "were"), first);
		return 0;
	case RCS_NODE_TREE:
		if (!(set & 1))
		{
			rcs_error("rm: %s: is a directory (use -r)", sb);
			return 1;
		}
		n = rsc_ntrees(c->r);
		if (rsc_tree_delete(c->r, sn.tree) != 0 || rsc_names_tree_removed(c->n, sn.tree) != 0)
		{
			rcs_error("rm: cannot remove %s", sb);
			return 1;
		}
		if (sn.tree < n - 1)
			rcs_note("rm: %d %s renumbered (from #%d on)", n - 1 - sn.tree,
				 plural(n - 1 - sn.tree, "tree was", "trees were"), sn.tree);
		return 0;
	case RCS_NODE_FRSTR:
	case RCS_NODE_FRIMG:
		n = sn.kind == RCS_NODE_FRSTR ? rsc_nfrstr(c->r) : rsc_nfrimg(c->r);
		if (sn.kind == RCS_NODE_FRSTR
		    ? (rsc_frstr_delete(c->r, sn.index) != 0 || rsc_names_str_removed(c->n, sn.index) != 0)
		    : (rsc_frimg_delete(c->r, sn.index) != 0 || rsc_names_img_removed(c->n, sn.index) != 0))
		{
			rcs_error("rm: cannot remove %s", sb);
			return 1;
		}
		if (sn.index < n - 1)
			rcs_note("rm: %d free %s renumbered (from #%d on)", n - 1 - sn.index,
				 sn.kind == RCS_NODE_FRSTR ? plural(n - 1 - sn.index, "string was", "strings were")
				 : plural(n - 1 - sn.index, "image was", "images were"), sn.index);
		return 0;
	case RCS_NODE_ICON:
		rcs_error("rm: %s: colour icon entries cannot be removed yet", sb);
		return 1;
	case RCS_NODE_ROOT:
		rcs_error("rm: %s: the root cannot be removed", sb);
		return 1;
	default:
		rcs_error("rm: %s: cannot be removed (remove its entries)", sb);
		return 1;
	}
}

/* ---- default records, geometry, types -------------------------------------------- */

/* cells (x or y direction) in the unit of the file */
static int cells_to_unit(const struct rsc *r, int cells, int vertical)
{
	int	cw, ch;

	rsc_cell(r, &cw, &ch);
	if (cw == 0)
		return cells;
	return cells * (vertical ? ch : cw);
}

static int default_record(struct rsc *r, int t, int i, enum rsc_spec_kind k)
{
	TEDINFO		te;
	ICONBLK		ib;
	BITBLK		bb;

	switch (k)
	{
	case RSC_SPEC_TEDINFO:
		memset(&te, 0, sizeof te);
		te.te_ptext = "";
		te.te_ptmplt = "";
		te.te_pvalid = "";
		te.te_font = 3;
		te.te_color = 0x1100;
		return rsc_obj_set_tedinfo(r, t, i, &te);
	case RSC_SPEC_ICONBLK:
	case RSC_SPEC_CICON:
		memset(&ib, 0, sizeof ib);
		ib.ib_ptext = "";
		ib.ib_char = 0x1000;
		ib.ib_wicon = 32;
		ib.ib_hicon = 32;
		ib.ib_ytext = 32;
		ib.ib_wtext = 32;
		ib.ib_htext = 8;
		if (k == RSC_SPEC_ICONBLK)
			return rsc_obj_set_iconblk(r, t, i, &ib);
		return rsc_obj_set_cicon(r, t, i, &ib, "");
	case RSC_SPEC_BITBLK:
		memset(&bb, 0, sizeof bb);
		bb.bi_wb = 2;
		bb.bi_hl = 16;
		bb.bi_color = 1;
		return rsc_obj_set_bitblk(r, t, i, &bb);
	default:
		return 0;
	}
}

/* The default spec and size of the new object i of tree t. */
static int init_object(struct rsc *r, int t, int i, unsigned type)
{
	unsigned	lo = type & 0xff;
	int		w = 8, h = 1;

	switch (rsc_spec_kind(type))
	{
	case RSC_SPEC_VALUE:
		if (lo == G_BOX || lo == G_IBOX)
		{
			if (rsc_obj_set_value(r, t, i, 0x00001100) != 0)
				return -1;
		}
		else if (lo == G_BOXCHAR && rsc_obj_set_value(r, t, i, 0x20001100) != 0)
			return -1;
		break;
	case RSC_SPEC_STRING:
		if (rsc_obj_set_str(r, t, i, RSC_STR_SPEC, "") != 0)
			return -1;
		break;
	default:
		if (default_record(r, t, i, rsc_spec_kind(type)) != 0)
			return -1;
		break;
	}
	if (lo == G_ICON || lo == G_CICON)
	{
		w = 4;
		h = 2;
	}
	else if (lo == G_IMAGE)
		w = 2;
	return rsc_obj_set_geom(r, t, i, 0, 0, cells_to_unit(r, w, 0), cells_to_unit(r, h, 1));
}

/* The type of a name (G_BUTTON, BUTTON, button) or a number (decimal or
 * 0x hex, up to 0xFFFF). */
static int parse_type(const char *s, unsigned *type)
{
	unsigned	i;
	const char	*p;
	long		v = 0;
	int		nd = 0, hex = 0;

	for (i = 0; i < sizeof type_names / sizeof type_names[0]; i++)
		if (eq_ci(s, type_names[i]) || eq_ci(s, type_names[i] + 2))
		{
			*type = G_BOX + i;
			return 0;
		}
	if (eq_ci(s, "G_USERDEF") || eq_ci(s, "USERDEF"))
	{
		*type = G_PROGDEF;
		return 0;
	}
	p = s;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
	{
		hex = 1;
		p += 2;
	}
	for (; nd < 6 && p[nd]; nd++)
	{
		int	ch = p[nd], dgt;

		if (ch >= '0' && ch <= '9')
			dgt = ch - '0';
		else if (hex && ch >= 'a' && ch <= 'f')
			dgt = ch - 'a' + 10;
		else if (hex && ch >= 'A' && ch <= 'F')
			dgt = ch - 'A' + 10;
		else
			return -1;
		v = v * (hex ? 16 : 10) + dgt;
	}
	if (nd == 0 || p[nd] || v > 0xffff)
		return -1;
	*type = (unsigned)v;
	return 0;
}

/* ---- mkdir ----------------------------------------------------------------------- */

static int add_box(struct rsc *r, int t, int parent, unsigned type, int x, int y, int w, int h,
		   uint32_t value, const char *text)
{
	int	i = rsc_obj_add(r, t, parent, type);

	if (i < 0)
		return -1;
	if (text)
	{
		if (rsc_obj_set_str(r, t, i, RSC_STR_SPEC, text) != 0)
			return -1;
	}
	else if (rsc_obj_set_value(r, t, i, value) != 0)
		return -1;
	return rsc_obj_set_geom(r, t, i, cells_to_unit(r, x, 0), cells_to_unit(r, y, 1),
				cells_to_unit(r, w, 0), cells_to_unit(r, h, 1)) == 0 ? i : -1;
}

/* Fills the new tree t (one G_IBOX) as a dialog, panel or menu. */
static int build_tree(struct rsc *r, int t, int kind)
{
	int	bar, act, scr, drop;

	if (kind != RSC_TKIND_MENU)
	{
		if (rsc_obj_set_type(r, t, 0, G_BOX) != 0
		    || rsc_obj_set_value(r, t, 0, kind == RSC_TKIND_DIAL ? 0x00021100 : 0x00001100) != 0)
			return -1;
		return rsc_obj_set_geom(r, t, 0, 0, 0, cells_to_unit(r, 32, 0), cells_to_unit(r, 8, 1));
	}
	if (rsc_obj_set_geom(r, t, 0, 0, 0, cells_to_unit(r, 80, 0), cells_to_unit(r, 25, 1)) != 0)
		return -1;
	bar = add_box(r, t, 0, G_BOX, 0, 0, 80, 1, 0x00001100, NULL);
	act = bar < 0 ? -1 : add_box(r, t, bar, G_IBOX, 2, 0, 7, 1, 0, NULL);
	if (act < 0 || add_box(r, t, act, G_TITLE, 0, 0, 7, 1, 0, " Title ") < 0)
		return -1;
	scr = add_box(r, t, 0, G_IBOX, 0, 1, 80, 24, 0, NULL);
	drop = scr < 0 ? -1 : add_box(r, t, scr, G_BOX, 2, 0, 14, 1, 0x00FF1100, NULL);
	if (drop < 0 || add_box(r, t, drop, G_STRING, 0, 0, 14, 1, 0, "  Item  ") < 0)
		return -1;
	return 0;
}

static int do_mkdir(struct rcs_ctx *c, int argc, char **argv)
{
	const char	*name = NULL, *type = "dialog";
	char		eb[300];
	int		i, idx, kind;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--type") == 0)
		{
			if (i + 1 >= argc)
			{
				rcs_error("mkdir: --type needs dialog, menu, panel or alert");
				return 1;
			}
			type = argv[++i];
		}
		else if (argv[i][0] == '-' && argv[i][1])
		{
			rcs_error("mkdir: unknown option '%s'", arg_path(eb, sizeof eb, argv[i]));
			return 1;
		}
		else if (name)
		{
			rcs_error("mkdir: too many arguments");
			return 1;
		}
		else
			name = argv[i];
	}
	if (!name)
	{
		rcs_error("mkdir: missing NAME");
		return 1;
	}
	if (strcmp(type, "dialog") == 0)
		kind = RSC_TKIND_DIAL;
	else if (strcmp(type, "menu") == 0)
		kind = RSC_TKIND_MENU;
	else if (strcmp(type, "panel") == 0)
		kind = RSC_TKIND_PANL;
	else if (strcmp(type, "alert") == 0)
		kind = RSC_TKIND_ALRT;
	else
	{
		rcs_error("mkdir: bad type '%s' (dialog, menu, panel or alert)", arg_path(eb, sizeof eb, type));
		return 1;
	}
	if (name[0] == '/')
		name++;
	if (new_name_ok("mkdir", name, kind != RSC_TKIND_ALRT) != 0
	    || name_free(c, "mkdir", kind == RSC_TKIND_ALRT ? RSC_NAME_FRSTR : RSC_NAME_TREE, 0, name, -1) != 0)
		return 1;
	if (kind == RSC_TKIND_ALRT)
	{
		idx = rsc_nfrstr(c->r);
		if (rsc_names_add(c->n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, idx, name, 0) != 0)
		{
			rcs_error("mkdir: cannot add the name (the name list is full)");
			return 1;
		}
		if (rsc_frstr_add(c->r, "[1][text][OK]") != idx)
		{
			rsc_names_remove(c->n, RSC_NAME_FRSTR, 0, idx);
			rcs_error("mkdir: out of memory");
			return 1;
		}
		return 0;
	}
	idx = rsc_ntrees(c->r);
	if (rsc_names_add(c->n, RSC_NAME_TREE, kind, idx, 0, name, 0) != 0)
	{
		rcs_error("mkdir: cannot add the name (the name list is full)");
		return 1;
	}
	if (rsc_tree_add(c->r, 1) != idx || build_tree(c->r, idx, kind) != 0)
	{
		if (rsc_ntrees(c->r) > idx)
			rsc_tree_delete(c->r, idx);
		rsc_names_tree_removed(c->n, idx);
		rcs_error("mkdir: cannot create the tree (memory ran out or too many trees)");
		return 1;
	}
	return 0;
}

/* ---- touch ----------------------------------------------------------------------- */

static int do_touch(struct rcs_ctx *c, int argc, char **argv)
{
	struct target	t;
	struct rcs_desc	d;
	const char	*path = NULL, *typename = NULL;
	char		eb[300];
	unsigned	type = G_BOX;
	int		i, parent, idx, tree, first, moved;
	BITBLK		bb;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--type") == 0)
		{
			if (i + 1 >= argc)
			{
				rcs_error("touch: --type needs a type");
				return 1;
			}
			typename = argv[++i];
		}
		else if (argv[i][0] == '-' && argv[i][1])
		{
			rcs_error("touch: unknown option '%s'", arg_path(eb, sizeof eb, argv[i]));
			return 1;
		}
		else if (path)
		{
			rcs_error("touch: too many arguments");
			return 1;
		}
		else
			path = argv[i];
	}
	if (!path)
	{
		rcs_error("touch: missing PATH");
		return 1;
	}
	if (typename && parse_type(typename, &type) != 0)
	{
		rcs_error("touch: bad type '%s' (an AES type name such as G_BUTTON, or a number)",
			  arg_path(eb, sizeof eb, typename));
		return 1;
	}
	if (dst_target(c, "touch", path, &t) != 0)
		return 1;
	if (t.exists)
		return 0;
	if (new_name_ok("touch", t.name, 0) != 0)
		return 1;
	switch (t.node.kind)
	{
	case RCS_NODE_STRDIR:
		if (name_free(c, "touch", RSC_NAME_FRSTR, 0, t.name, -1) != 0)
			return 1;
		idx = rsc_nfrstr(c->r);
		if (rsc_names_add(c->n, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, idx, t.name, 0) != 0
		    || rsc_frstr_add(c->r, "") != idx)
		{
			rsc_names_remove(c->n, RSC_NAME_FRSTR, 0, idx);
			rcs_error("touch: cannot create the string (memory ran out or the name list is full)");
			return 1;
		}
		return 0;
	case RCS_NODE_IMGDIR:
		if (name_free(c, "touch", RSC_NAME_FRIMG, 0, t.name, -1) != 0)
			return 1;
		idx = rsc_nfrimg(c->r);
		memset(&bb, 0, sizeof bb);
		bb.bi_wb = 2;
		bb.bi_hl = 16;
		bb.bi_color = 1;
		if (rsc_names_add(c->n, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, idx, t.name, 0) != 0
		    || rsc_frimg_add(c->r, &bb) != idx)
		{
			rsc_names_remove(c->n, RSC_NAME_FRIMG, 0, idx);
			rcs_error("touch: cannot create the image (memory ran out or the name list is full)");
			return 1;
		}
		return 0;
	case RCS_NODE_TREE:
		parent = 0;
		break;
	case RCS_NODE_OBJ:
		if (rcs_describe(c->v, &t.node, &d) != 0 || !d.reachable)
		{
			rcs_error("touch: the parent of %s is not reachable from the root of its tree", t.arg);
			return 1;
		}
		parent = t.node.obj;
		break;
	case RCS_NODE_ROOT:
		rcs_error("touch: %s: use mkdir to create a tree", t.arg);
		return 1;
	default:
		rcs_error("touch: %s: nothing can be created there", t.arg);
		return 1;
	}
	tree = t.node.tree;
	if (name_free(c, "touch", RSC_NAME_OBJ, tree, t.name, -1) != 0)
		return 1;
	if (rsc_names_count(c->n) >= RSC_NAMES_MAX)
	{
		rcs_error("touch: the name list is full (at most %d entries)", RSC_NAMES_MAX);
		return 1;
	}
	idx = rsc_obj_add(c->r, tree, parent, type);
	if (idx < 0 || init_object(c->r, tree, idx, type) != 0
	    || rsc_names_add(c->n, RSC_NAME_OBJ, 0, tree, idx, t.name, 0) != 0)
	{
		if (idx >= 0)
			delete_obj(c, tree, idx, &first, &moved);
		rcs_error("touch: cannot create the object (the tree is full, memory ran out or the name list is full)");
		return 1;
	}
	return 0;
}

/* ---- chmod ----------------------------------------------------------------------- */

struct bitname
{
	const char	*name;
	unsigned	mask;
};

static const struct bitname flag_bits[] =
{
	{ "SELECTABLE", 0x1 }, { "DEFAULT", 0x2 }, { "EXIT", 0x4 }, { "EDITABLE", 0x8 },
	{ "RBUTTON", 0x10 }, { "LASTOB", 0x20 }, { "TOUCHEXIT", 0x40 }, { "HIDETREE", 0x80 },
	{ "INDIRECT", 0x100 }, { "FL3DIND", 0x200 }, { "FL3DBAK", 0x400 }, { "FL3DACT", 0x600 },
	{ NULL, 0 }
};

static const struct bitname state_bits[] =
{
	{ "SELECTED", 0x1 }, { "CROSSED", 0x2 }, { "CHECKED", 0x4 }, { "DISABLED", 0x8 },
	{ "OUTLINED", 0x10 }, { "SHADOWED", 0x20 }, { NULL, 0 }
};

/* The mask of a word (case-insensitive, with or without the prefix) or 0. */
static unsigned bit_mask(const struct bitname *tab, const char *prefix, const char *w, size_t len)
{
	char	buf[32];
	size_t	i;

	if (len >= 3 && lower_c(w[0]) == lower_c(prefix[0]) && lower_c(w[1]) == lower_c(prefix[1]) && w[2] == '_')
	{
		w += 3;
		len -= 3;
	}
	if (len == 0 || len >= sizeof buf)
		return 0;
	memcpy(buf, w, len);
	buf[len] = 0;
	for (i = 0; tab[i].name; i++)
		if (eq_ci(buf, tab[i].name))
			return tab[i].mask;
	return 0;
}

static int do_chmod(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_src	S;
	struct rcs_node	nd;
	const struct bitname *tab;
	const char	*path, *p, *q, *prefix;
	char		eb[300], sb[200];
	unsigned	cur, val, old, mask, all;
	int		i, state = 0, first, tree;

	i = 1;
	if (i < argc && strcmp(argv[i], "--state") == 0)
	{
		state = 1;
		i++;
	}
	if (argc - i < 2)
	{
		rcs_error("chmod: usage: chmod [--state] [+-=]NAME[,NAME...]... PATH");
		return 1;
	}
	tab = state ? state_bits : flag_bits;
	prefix = state ? "OS" : "OF";
	arg_path(sb, sizeof sb, argv[argc - 1]);
	if (arg_side(c, "chmod", argv[argc - 1], &S, &path) != 0)
		return 1;
	if (!S.is_main)
	{
		rcs_error("chmod: %s: a second file is read-only", sb);
		return 1;
	}
	if (side_resolve("chmod", &S, path, &nd) != 0)
		return 1;
	if (nd.kind != RCS_NODE_OBJ)
	{
		rcs_error("chmod: %s: not an object", sb);
		return 1;
	}
	{
		const OBJECT	*o = rsc_tree_objects(c->r, nd.tree, NULL);

		cur = state ? o[nd.obj].ob_state : o[nd.obj].ob_flags;
	}
	old = val = cur;
	for (first = i; first < argc - 1; first++)
	{
		const char	*m = argv[first];

		if (m[0] != '+' && m[0] != '-' && m[0] != '=')
		{
			rcs_error("chmod: bad mode '%s' (+NAME, -NAME or =NAME,... )", arg_path(eb, sizeof eb, m));
			return 1;
		}
		all = 0;
		p = m + 1;
		if (m[0] != '=' && !*p)
		{
			rcs_error("chmod: bad mode '%s' (no names)", arg_path(eb, sizeof eb, m));
			return 1;
		}
		while (*p)
		{
			q = strchr(p, ',');
			if (!q)
				q = p + strlen(p);
			mask = bit_mask(tab, prefix, p, (size_t)(q - p));
			if (mask == 0)
			{
				char	wb[100];

				snprintf(wb, sizeof wb, "%.*s", (int)(q - p > 40 ? 40 : q - p), p);
				rcs_error("chmod: unknown %s '%s'", state ? "state" : "flag", arg_path(eb, sizeof eb, wb));
				return 1;
			}
			if (!state && (mask & 0x20))
			{
				rcs_error("chmod: LASTOB is kept by the library and cannot be changed");
				return 1;
			}
			all |= mask;
			p = *q ? q + 1 : q;
			if (*q == ',' && !*p)
			{
				rcs_error("chmod: bad mode '%s' (empty name)", arg_path(eb, sizeof eb, m));
				return 1;
			}
		}
		if (m[0] == '+')
			val |= all;
		else if (m[0] == '-')
			val &= ~all;
		else
			val = (state ? 0 : (val & 0x20)) | all;
	}
	tree = nd.tree;
	if (state)
		return rsc_obj_set_state(c->r, tree, nd.obj, val) != 0;
	if (rsc_obj_set_flags(c->r, tree, nd.obj, val) != 0)
	{
		rcs_error("chmod: cannot set the flags");
		return 1;
	}
	if ((old ^ val) & 0x100)
		rcs_note("chmod: INDIRECT changed the kind of the spec: the old spec was released");
	return 0;
}

/* ---- set ------------------------------------------------------------------------- */

enum set_key
{
	K_TEXT, K_TEMPLATE, K_VALID, K_X, K_Y, K_W, K_H, K_COLOR, K_THICK, K_FONT, K_JUST, K_TYPE, K_NAME
};

static const char *const set_keys[] =
{
	"text", "template", "valid", "x", "y", "w", "h", "color", "thickness", "font", "just", "type", "name"
};

struct setop
{
	int		key;
	const char	*val;
	long		num;		/* x y w h (as the model holds it), color, thickness, font, just, type */
	char		bad[100];	/* the assignment, escaped, for messages */
};

/* A coordinate: in pixels a signed decimal that has a file form, else
 * CELLS, CELLS+REM or CELLS-REM (cells 0-255, rem -127..128). */
static int parse_coord(const char *s, int cell, long *out)
{
	long	v = 0, rem = 0;
	int	nd = 0, neg = 0, sign = 0;

	if (cell)
	{
		if (*s == '-')
		{
			neg = 1;
			s++;
		}
		for (; *s >= '0' && *s <= '9' && nd < 7; s++, nd++)
			v = v * 10 + (*s - '0');
		if (nd == 0 || *s)
			return -1;
		if (neg)
			v = -v;
		if (v < -127 || v > 255L * cell + 128)
			return -1;
		*out = v;
		return 0;
	}
	for (; *s >= '0' && *s <= '9' && nd < 5; s++, nd++)
		v = v * 10 + (*s - '0');
	if (nd == 0 || v > 255)
		return -1;
	if (*s == '+' || *s == '-')
	{
		sign = *s == '+' ? 1 : -1;
		for (s++, nd = 0; *s >= '0' && *s <= '9' && nd < 5; s++, nd++)
			rem = rem * 10 + (*s - '0');
		if (nd == 0 || (sign > 0 && rem > 128) || (sign < 0 && rem > 127))
			return -1;
		rem *= sign;
	}
	if (*s)
		return -1;
	*out = (short)(unsigned short)((v & 0xff) | ((rem & 0xff) << 8));
	return 0;
}

/* A whole number in lo..hi (decimal, optional sign; hex with 0x when hex). */
static int parse_num(const char *s, int hex, long lo, long hi, long *out)
{
	long	v = 0;
	int	nd = 0, neg = 0, base = 10, dgt;

	if (*s == '-' && !hex)
	{
		neg = 1;
		s++;
	}
	if (hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	if (hex)
		base = 16;
	for (; *s && nd < 9; s++, nd++)
	{
		if (*s >= '0' && *s <= '9')
			dgt = *s - '0';
		else if (hex && *s >= 'a' && *s <= 'f')
			dgt = *s - 'a' + 10;
		else if (hex && *s >= 'A' && *s <= 'F')
			dgt = *s - 'A' + 10;
		else
			return -1;
		v = v * base + dgt;
	}
	if (nd == 0 || *s)
		return -1;
	if (neg)
		v = -v;
	if (v < lo || v > hi)
		return -1;
	*out = v;
	return 0;
}

/* Free string k got new text: its entries follow the shape of the text (ALRT
 * or not); a guessed entry that no longer fits is dropped. */
static int retkind_str(struct rcs_ctx *c, int k)
{
	struct rsc_names	*tmp = rsc_names_new();
	struct rsc_name		e;
	int			i, cnt = rsc_names_count(c->n), ok = tmp != NULL;
	const char		*text = rsc_frstr(c->r, k);

	for (i = 0; ok && i < cnt; i++)
		if (rsc_names_get(c->n, i, &e) == 0 && e.kind == RSC_NAME_FRSTR && e.obj == k)
			ok = (e.guessed && !rcs_is_alert(text))
			    || rsc_names_add(tmp, RSC_NAME_FRSTR, str_tkind(text), 0, k, e.name, e.guessed) == 0;
	if (ok)
	{
		rsc_names_remove(c->n, RSC_NAME_FRSTR, 0, k);
		cnt = rsc_names_count(tmp);
		for (i = 0; ok && i < cnt; i++)
			ok = rsc_names_get(tmp, i, &e) == 0
			    && rsc_names_add(c->n, RSC_NAME_FRSTR, e.tkind, 0, k, e.name, e.guessed) == 0;
	}
	rsc_names_free(tmp);
	return ok ? 0 : -1;
}

static int do_set(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_src	S;
	struct rcs_node	nd;
	struct setop	ops[64];
	const char	*path, *eq;
	char		sb[200], kb[40], eb[300];
	unsigned	simtype = 0, simflags = 0, lo;
	int		i, k, n, nops, cw, ch, isobj, kind, t, o;
	enum rsc_spec_kind	sk;
	OBJECT		*obj;
	long		num;

	if (argc < 3)
	{
		rcs_error("set: usage: set PATH key=value...");
		return 1;
	}
	path = argv[1];
	arg_path(sb, sizeof sb, path);
	if (argc - 2 > 64)
	{
		rcs_error("set: too many assignments");
		return 1;
	}
	if (arg_side(c, "set", path, &S, &path) != 0)
		return 1;
	if (!S.is_main)
	{
		rcs_error("set: %s: a second file is read-only", sb);
		return 1;
	}
	if (side_resolve("set", &S, path, &nd) != 0)
		return 1;
	isobj = nd.kind == RCS_NODE_OBJ;
	if (!isobj && nd.kind != RCS_NODE_TREE && nd.kind != RCS_NODE_FRSTR && nd.kind != RCS_NODE_FRIMG)
	{
		rcs_error("set: %s: not an object", sb);
		return 1;
	}
	t = nd.tree;
	o = nd.obj;
	if (isobj)
	{
		obj = rsc_tree_objects(c->r, t, &n);
		simtype = obj[o].ob_type;
		simflags = obj[o].ob_flags;
	}
	rsc_cell(c->r, &cw, &ch);
	nops = argc - 2;
	/* pass 1: every assignment is checked, against the type the earlier ones leave */
	for (i = 0; i < nops; i++)
	{
		struct setop	*op = &ops[i];
		const char	*a = argv[i + 2];

		arg_path(op->bad, sizeof op->bad, a);
		eq = strchr(a, '=');
		if (!eq || eq == a)
		{
			rcs_error("set: bad assignment '%s' (key=value)", op->bad);
			return 1;
		}
		snprintf(kb, sizeof kb, "%.*s", (int)(eq - a > 30 ? 30 : eq - a), a);
		for (k = 0; k < (int)(sizeof set_keys / sizeof set_keys[0]); k++)
			if (strcmp(kb, set_keys[k]) == 0)
				break;
		if (k == (int)(sizeof set_keys / sizeof set_keys[0]))
		{
			rcs_error("set: unknown key '%s'", arg_path(eb, sizeof eb, kb));
			return 1;
		}
		op->key = k;
		op->val = eq + 1;
		op->num = 0;
		if (k == K_NAME)
		{
			if (new_name_ok("set", op->val, nd.kind == RCS_NODE_TREE) != 0)
				return 1;
			kind = nd.kind == RCS_NODE_TREE ? RSC_NAME_TREE : isobj ? RSC_NAME_OBJ
			     : nd.kind == RCS_NODE_FRSTR ? RSC_NAME_FRSTR : RSC_NAME_FRIMG;
			if (name_free(c, "set", kind, t, op->val, nd.kind == RCS_NODE_TREE ? t : isobj ? o : nd.index) != 0)
				return 1;
			continue;
		}
		if (nd.kind == RCS_NODE_FRSTR && k == K_TEXT)
			continue;
		if (!isobj)
		{
			rcs_error("set: %s: only name= applies to %s", sb, nd.kind == RCS_NODE_TREE ? "a tree"
				  : "a free string (and text=) or image");
			return 1;
		}
		lo = simtype & 0xff;
		sk = (simflags & OF_INDIRECT) ? RSC_SPEC_VALUE : rsc_spec_kind(simtype);
		switch (k)
		{
		case K_TEXT:
			if (sk == RSC_SPEC_STRING || sk == RSC_SPEC_ICONBLK)
				break;
			if (sk == RSC_SPEC_TEDINFO && strlen(op->val) >= 32767)
				goto toolong;
			if (sk == RSC_SPEC_CICON && strlen(op->val) > 12)
				goto toolong;
			if (sk == RSC_SPEC_TEDINFO || sk == RSC_SPEC_CICON)
				break;
			goto notapply;
		case K_TEMPLATE:
		case K_VALID:
			if (sk == RSC_SPEC_TEDINFO && strlen(op->val) >= 32767)
				goto toolong;
			if (sk == RSC_SPEC_TEDINFO)
				break;
			goto notapply;
		case K_X:
		case K_Y:
		case K_W:
		case K_H:
			if (parse_coord(op->val, cw ? (k == K_X || k == K_W ? cw : ch) : 0, &op->num) != 0)
			{
				rcs_error("set: bad value '%s' (%s)", op->bad, cw ? "a whole number of pixels"
					  : "cells, as 3, 3+4 or 3-2");
				return 1;
			}
			break;
		case K_COLOR:
			if (parse_num(op->val, 1, 0, 0xffff, &op->num) != 0)
				goto badvalue;
			if (sk == RSC_SPEC_TEDINFO || (sk == RSC_SPEC_VALUE && !(simflags & OF_INDIRECT)
			    && (lo == G_BOX || lo == G_IBOX || lo == G_BOXCHAR)))
				break;
			goto notapply;
		case K_THICK:
			if (parse_num(op->val, 0, -128, 127, &op->num) != 0)
				goto badvalue;
			if (sk == RSC_SPEC_TEDINFO || (sk == RSC_SPEC_VALUE && !(simflags & OF_INDIRECT)
			    && (lo == G_BOX || lo == G_IBOX || lo == G_BOXCHAR)))
				break;
			goto notapply;
		case K_FONT:
			if (eq_ci(op->val, "IBM"))
				op->num = 3;
			else if (eq_ci(op->val, "SMALL"))
				op->num = 5;
			else if (parse_num(op->val, 0, 0, 32767, &op->num) != 0)
				goto badvalue;
			if (sk == RSC_SPEC_TEDINFO)
				break;
			goto notapply;
		case K_JUST:
			if (eq_ci(op->val, "left"))
				op->num = 0;
			else if (eq_ci(op->val, "right"))
				op->num = 1;
			else if (eq_ci(op->val, "center"))
				op->num = 2;
			else if (parse_num(op->val, 0, 0, 2, &op->num) != 0)
				goto badvalue;
			if (sk == RSC_SPEC_TEDINFO)
				break;
			goto notapply;
		default:	/* K_TYPE */
			if (parse_type(op->val, &simtype) != 0)
			{
				rcs_error("set: bad type '%s' (an AES type name such as G_BUTTON, or a number)", op->bad);
				return 1;
			}
			op->num = simtype;
			break;
		}
		continue;
badvalue:
		rcs_error("set: bad value '%s'", op->bad);
		return 1;
notapply:
		type_str(eb, sizeof eb, simtype);
		rcs_error("set: %s: %s does not apply to a %s object%s", sb, kb, eb,
			  (simflags & OF_INDIRECT) ? " with INDIRECT" : "");
		return 1;
toolong:
		rcs_error("set: %s: the value of %s is too long (at most %d characters)", sb, kb,
			  sk == RSC_SPEC_CICON ? 12 : 32766);
		return 1;
	}
	/* pass 2: nothing can be refused any more but for memory */
	for (i = 0; i < nops; i++)
	{
		struct setop	*op = &ops[i];
		int		bad = 0;

		if (op->key == K_NAME)
		{
			if (rename_node(c, "set", &nd, op->val) != 0)
				return 1;
			continue;
		}
		if (nd.kind == RCS_NODE_FRSTR)
		{
			if (rsc_frstr_set(c->r, nd.index, op->val) != 0 || retkind_str(c, nd.index) != 0)
				bad = 1;
		}
		else
		{
			obj = rsc_tree_objects(c->r, t, &n);
			sk = rsc_obj_spec_kind(&obj[o]);
			switch (op->key)
			{
			case K_TEXT:
			case K_TEMPLATE:
			case K_VALID:
				if (op->key == K_TEXT && sk == RSC_SPEC_STRING)
					bad = rsc_obj_set_str(c->r, t, o, RSC_STR_SPEC, op->val);
				else
				{
					if ((sk == RSC_SPEC_TEDINFO && !obj[o].ob_spec.tedinfo)
					    || (sk == RSC_SPEC_ICONBLK && !obj[o].ob_spec.iconblk)
					    || (sk == RSC_SPEC_CICON && !rsc_obj_cicon(c->r, t, o)))
						bad = default_record(c->r, t, o, sk);
					if (!bad)
						bad = rsc_obj_set_str(c->r, t, o, op->key == K_TEXT
								      ? (sk == RSC_SPEC_TEDINFO ? RSC_STR_TE_TEXT : RSC_STR_IB_TEXT)
								      : op->key == K_TEMPLATE ? RSC_STR_TE_TMPLT : RSC_STR_TE_VALID,
								      op->val);
				}
				break;
			case K_X:
			case K_Y:
			case K_W:
			case K_H:
				bad = rsc_obj_set_geom(c->r, t, o, op->key == K_X ? (int)op->num : obj[o].ob_x,
						       op->key == K_Y ? (int)op->num : obj[o].ob_y,
						       op->key == K_W ? (int)op->num : obj[o].ob_width,
						       op->key == K_H ? (int)op->num : obj[o].ob_height);
				break;
			case K_COLOR:
			case K_THICK:
				if (sk == RSC_SPEC_TEDINFO)
				{
					if (!obj[o].ob_spec.tedinfo)
						bad = default_record(c->r, t, o, sk);
					if (!bad)
					{
						if (op->key == K_COLOR)
							obj[o].ob_spec.tedinfo->te_color = (short)op->num;
						else
							obj[o].ob_spec.tedinfo->te_thickness = (short)op->num;
					}
				}
				else
				{
					uint32_t	v = 0;

					rsc_obj_get_value(c->r, t, o, &v);
					if (op->key == K_COLOR)
						v = (v & 0xffff0000UL) | (uint32_t)op->num;
					else
						v = (v & 0xff00ffffUL) | ((uint32_t)(op->num & 0xff) << 16);
					bad = rsc_obj_set_value(c->r, t, o, v);
				}
				break;
			case K_FONT:
			case K_JUST:
				if (!obj[o].ob_spec.tedinfo)
					bad = default_record(c->r, t, o, sk);
				if (!bad)
				{
					if (op->key == K_FONT)
						obj[o].ob_spec.tedinfo->te_font = (short)op->num;
					else
						obj[o].ob_spec.tedinfo->te_just = (short)op->num;
				}
				break;
			default:
				bad = rsc_obj_set_type(c->r, t, o, (unsigned)op->num);
				break;
			}
		}
		if (bad)
		{
			rcs_error("set: %s: cannot set %s (memory ran out)", sb, set_keys[op->key]);
			return 1;
		}
	}
	(void)num;
	return 0;
}

static int run_read(struct rcs_ctx *c, int argc, char **argv)
{
	struct rcs_node	nd;
	unsigned	set;
	int		i;
	const char	*cmd = argv[0];
	char		ob[64];

	if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "tree") == 0 || strcmp(cmd, "stat") == 0
	    || strcmp(cmd, "cat") == 0)
	{
		const char	*allowed = strcmp(cmd, "ls") == 0 ? "lR" : strcmp(cmd, "cat") == 0 ? "v" : "";
		const char	*path, *spath;
		struct rcs_src	side;
		struct rcs_ctx	sub;

		i = options(cmd, argc, argv, allowed, &set);
		if (i < 0)
			return 1;
		if (argc - i > 1)
		{
			rcs_error("%s: too many arguments", cmd);
			return 1;
		}
		if (argc - i == 0 && (strcmp(cmd, "stat") == 0 || strcmp(cmd, "cat") == 0))
		{
			rcs_error("%s: missing PATH", cmd);
			return 1;
		}
		path = argc - i == 1 ? argv[i] : "/";
		if (arg_side(c, cmd, path, &side, &spath))
			return 1;
		sub = *c;
		sub.r = side.r;
		sub.n = side.n;
		sub.v = side.v;
		if (side_resolve(cmd, &side, spath, &nd))
			return 1;
		if (strcmp(cmd, "ls") == 0)
			return do_ls(&sub, &nd, (set & 1) != 0, (set & 2) != 0);
		if (strcmp(cmd, "tree") == 0)
			return do_tree(&sub, &nd);
		if (strcmp(cmd, "stat") == 0)
			return do_stat(&sub, &nd);
		return do_cat(&sub, &nd, (set & 1) != 0, path);
	}
	if (strcmp(cmd, "convert") == 0)
		return do_convert(c, argc, argv);
	rcs_error("unknown command '%s'", arg_path(ob, sizeof ob, cmd));
	return 1;
}

/* ---- the dispatcher of the edit commands ------------------------------------------ */

int rcs_is_edit(const char *cmd)
{
	static const char *const names[] = { "cp", "mv", "rm", "mkdir", "touch", "chmod", "set" };
	unsigned	i;

	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		if (strcmp(cmd, names[i]) == 0)
			return 1;
	return 0;
}

/* A growing buffer for the snapshot of --selfcheck. */
struct sbuf
{
	char	*p;
	size_t	len, cap;
	int	bad;
	int	enc;		/* the resource was encoded into the buffer */
};

static void sb_put(struct sbuf *b, const void *d, size_t n)
{
	char	*q;

	if (b->bad)
		return;
	if (b->len + n > b->cap)
	{
		b->cap = (b->len + n) * 2 + 64;
		q = realloc(b->p, b->cap);
		if (!q)
		{
			b->bad = 1;
			return;
		}
		b->p = q;
	}
	memcpy(b->p + b->len, d, n);
	b->len += n;
}

/* The encoded resource, the object counts and every name entry. */
static int snapshot(struct rcs_ctx *c, struct sbuf *b)
{
	struct rsc_error	err;
	struct rsc_name		e;
	uint8_t			*buf;
	size_t			len;
	char			line[160];
	int			i, cw, ch, n, nobj;

	memset(b, 0, sizeof *b);
	rsc_cell(c->r, &cw, &ch);
	if (rsc_atari_write(c->r, cw, ch, &buf, &len, &err) == 0)
	{
		b->enc = 1;
		sb_put(b, buf, len);
		rsc_mfree(buf);
	}
	n = rsc_ntrees(c->r);
	for (i = 0; i < n; i++)
	{
		rsc_tree_objects(c->r, i, &nobj);
		snprintf(line, sizeof line, "T%d:%d\n", i, nobj);
		sb_put(b, line, strlen(line));
	}
	n = rsc_names_count(c->n);
	for (i = 0; i < n; i++)
		if (rsc_names_get(c->n, i, &e) == 0)
		{
			snprintf(line, sizeof line, "N%d,%d,%d,%d,%d,%s\n", e.kind, e.tkind, e.tree, e.obj,
				 e.guessed, e.name);
			sb_put(b, line, strlen(line));
		}
	snprintf(line, sizeof line, "S%d I%d\n", rsc_nfrstr(c->r), rsc_nfrimg(c->r));
	sb_put(b, line, strlen(line));
	return b->bad ? -1 : 0;
}

static int run_edit(struct rcs_ctx *c, int argc, char **argv)
{
	const char	*cmd = argv[0];

	if (strcmp(cmd, "cp") == 0)
		return do_cp(c, argc, argv);
	if (strcmp(cmd, "mv") == 0)
		return do_mv(c, argc, argv);
	if (strcmp(cmd, "rm") == 0)
		return do_rm(c, argc, argv);
	if (strcmp(cmd, "mkdir") == 0)
		return do_mkdir(c, argc, argv);
	if (strcmp(cmd, "touch") == 0)
		return do_touch(c, argc, argv);
	if (strcmp(cmd, "chmod") == 0)
		return do_chmod(c, argc, argv);
	return do_set(c, argc, argv);
}

int rcs_run(struct rcs_ctx *c, int argc, char **argv)
{
	struct sbuf	before, after;
	int		rc, have = 0, quiet;

	if (!rcs_is_edit(argv[0]))
		return run_read(c, argc, argv);
	/* touch, chmod and set may change nothing: that is no edit */
	quiet = strcmp(argv[0], "touch") == 0 || strcmp(argv[0], "chmod") == 0 || strcmp(argv[0], "set") == 0;
	if (c->selfcheck || quiet)
		have = snapshot(c, &before) == 0;
	rc = run_edit(c, argc, argv);
	if (rcs_view_reset(c->v) != 0 && rc == 0)
	{
		rcs_error("%s: out of memory", argv[0]);
		rc = 1;
	}
	if (rc == 0)
	{
		if (!quiet || !have)
			c->edited = 1;
		else if (snapshot(c, &after) != 0 || !after.enc || !before.enc || after.len != before.len
			 || memcmp(after.p, before.p, before.len) != 0)
			c->edited = 1;
		if (quiet && have)
			free(after.p);
	}
	else if (have && c->selfcheck)
	{
		if (snapshot(c, &after) != 0 || after.len != before.len || memcmp(after.p, before.p, before.len) != 0)
		{
			rcs_error("internal error: %s failed but changed the model", argv[0]);
			rc = 1;
		}
		free(after.p);
	}
	if (have)
		free(before.p);
	return rc;
}
