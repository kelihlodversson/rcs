/* RCSCMD.C	the commands of rcs-cli: ls, tree, stat, cat, convert, and their
 *		output formats (docs/rcs-cli.md says them exactly)
 *
 * Every byte that comes from the resource or the names goes through
 * rcs_escape (or out_text for cat), so the output is printable ASCII and
 * a name takes a bounded number of characters.  The walks of ls -R and
 * tree keep their own stack: no recursion on the C stack.
 */
#include <errno.h>
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

/* Encodes the resource first, then writes it to a temporary file next to
 * OUT and renames that over OUT once it is written and closed; on any
 * failure the temporary file is removed and an existing OUT is untouched. */
static int do_convert(struct rcs_ctx *c, int argc, char **argv)
{
	struct rsc_error	err;
	uint8_t			*buf = NULL;
	size_t			len = 0;
	const char		*out;
	char			pb[600], *tmp;
	FILE			*f;
	int			cw, ch, bad, fd, e;
	mode_t			mask;

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
	tmp = malloc(strlen(out) + sizeof ".XXXXXX");
	if (!tmp)
	{
		rcs_error("out of memory");
		rsc_mfree(buf);
		return 1;
	}
	strcpy(tmp, out);
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
		rcs_error("convert: %s: cannot write: %s", arg_path(pb, sizeof pb, out), strerror(e));
		free(tmp);
		rsc_mfree(buf);
		return 1;
	}
	/* mkstemp makes the file private: give it the mode fopen would have */
	mask = umask(0);
	umask(mask);
	bad = fchmod(fd, 0666 & ~mask) != 0;
	if (!bad)
		bad = fwrite(buf, 1, len, f) != len;
	if (fclose(f) != 0)
		bad = 1;
	rsc_mfree(buf);
	if (!bad)
		bad = rename(tmp, out) != 0;
	if (bad)
	{
		e = errno;
		remove(tmp);
		rcs_error("convert: %s: cannot write: %s", arg_path(pb, sizeof pb, out), strerror(e));
		free(tmp);
		return 1;
	}
	free(tmp);
	return 0;
}

/* ---- the dispatcher ------------------------------------------------------------ */

/* Resolves path for command cmd, with an error line on failure. */
static int resolve(struct rcs_ctx *c, const char *cmd, const char *path, struct rcs_node *nd)
{
	char	msg[RCS_MSG_MAX];

	if (rcs_resolve(c->v, path, nd, msg, sizeof msg) != 0)
	{
		rcs_error("%s: %s", cmd, msg);
		return -1;
	}
	return 0;
}

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

int rcs_run(struct rcs_ctx *c, int argc, char **argv)
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
		const char	*path;

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
		if (resolve(c, cmd, path, &nd))
			return 1;
		if (strcmp(cmd, "ls") == 0)
			return do_ls(c, &nd, (set & 1) != 0, (set & 2) != 0);
		if (strcmp(cmd, "tree") == 0)
			return do_tree(c, &nd);
		if (strcmp(cmd, "stat") == 0)
			return do_stat(c, &nd);
		return do_cat(c, &nd, (set & 1) != 0, path);
	}
	if (strcmp(cmd, "convert") == 0)
		return do_convert(c, argc, argv);
	rcs_error("unknown command '%s'", arg_path(ob, sizeof ob, cmd));
	return 1;
}
