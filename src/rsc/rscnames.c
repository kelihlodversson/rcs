/* RSCNAMES.C	the names of a resource file (rscnames.h): the list, the
 *		readers of the .DFN, .DEF/.RSD and .HRD formats, the check
 *		against a resource, the guesser and the lookups
 *
 * The formats, as found in real files (src/atari/rcs.dfn with rcs.rsc,
 * EmuTOS' desktop.def, gem.def, icon.def and mform.def with their
 * resources, TeraDesk's fm_desktop.hrd):
 *
 * .DFN (RCS): a little-endian count word, then count records of 14 bytes:
 *	a value word and a kind word (little-endian), 10 name bytes.  The
 *	name is the bytes up to the first NUL within the first 8 (8 bytes
 *	without a NUL are a name of 8): real files have stale bytes after
 *	the NUL, and old tools wrote 8 usable bytes.  Kinds: 0 UNKN, 1 PANL,
 *	2 MENU, 3 DIAL (a tree; the value is its index), 4 ALRT (a free
 *	string that is an alert; the value is its index), 0x100 an object
 *	(value = tree << 8 | object), 0x101 a free string, 0x102 a free
 *	image (the index).  5 (FREE) is never in a file; it and any other
 *	kind become an entry of kind RSC_NAME_UNKNOWN.  The file must be
 *	exactly 2 + 14 * count bytes: shorter is RSC_ETRUNC at the end,
 *	longer RSC_EHEADER where the extra bytes start.
 *
 * .DEF and .RSD (EmuTOS tools/erd.c, rscview): records of 16 bytes,
 *	big-endian.  Record 0 holds the count in bytes 0-1 and flags in
 *	bytes 2-3 (ignored) and is an entry like the others: the entries are
 *	records 0 to count - 1.  A record: [4] tree, [5] object or index,
 *	[6] indicator, [7] type, [8..15] the name, the bytes up to the first
 *	NUL (not always terminated: 8 bytes are a name of 8); bytes 0-3 of
 *	the other records are ignored.  Types: 0 an object ([4], [5]) with
 *	indicator 1, a tree of kind UNKN ([5]) with indicator 0 (any other
 *	indicator: RSC_NAME_UNKNOWN), 1 PANL, 2 MENU, 3 DIAL (a tree, [5]),
 *	4 ALRT, 5 a free string, 6 a free image ([5]); other types are
 *	RSC_NAME_UNKNOWN; the indicator of types 1 to 6 and [4] of their
 *	records are ignored.  The size must be a multiple of 16 (else
 *	RSC_ETRUNC where the partial record starts), at least one record,
 *	and the count the number of records, or one less when the last
 *	record is all zero (EmuTOS' desktop.def has such a record, gem.def
 *	does not); a larger count is RSC_ETRUNC at the end, a smaller one
 *	RSC_EHEADER at 0.  The old RCS reader refused files without the zero
 *	record (so no names loaded for gem.def and mform.def): not copied.
 *
 * .HRD (Interface, ORCS, ResourceMaster): a header of 8 bytes (a
 *	big-endian version word, which must be 1, else RSC_EHEADER; a flags
 *	word and 4 bytes, ignored), then records: a type byte, a filler
 *	byte, a big-endian tree word, a big-endian object word and a name
 *	ending with a NUL (inside the file, else RSC_ETRUNC at the name).
 *	Types: 0 DIAL, 1 MENU (a tree), 2 ALRT, 3 a free string, 4 a free
 *	image (the index in the tree word; the object word must be 0, else
 *	RSC_EOFFSET at it), 5 an object (below RSC_MAX_OBJECTS, else
 *	RSC_EOFFSET), 6 the end, 7 a prefix (ignored, its name too); other
 *	types are RSC_NAME_UNKNOWN (the tree word kept as the value).  The
 *	end record is required (RSC_ETRUNC at the end of the file without
 *	it); bytes after it are ignored.  Names longer than 16 characters
 *	are cut to 16 (the format's limit).
 *
 * Names: every byte of a name in the file (the whole name, also where it is
 * cut) must be 0x20..0x7e or 0x80..0xff, and a name not empty, else
 * RSC_ESTRING at the name; the end and prefix records are not checked.
 * The case is kept.  A file may have several entries for one node and one
 * name for several nodes: kept; the lookups report ambiguity.
 *
 * Hostile files: every read of the buffer is range checked, every loop is
 * bounded by the buffer length, and a reader makes two passes over the
 * file: the first checks everything and counts the entries (no
 * allocation), the second allocates and fills.  So a bad file fails before
 * anything is allocated, with a code, the position and a message.  At most
 * RSC_NAMES_MAX entries (RSC_ETOOBIG at the first record beyond).
 *
 * The budget.  Every allocation of a reader (the list, its entry array of
 * exactly the counted size, each name) is charged before it is made, at its
 * size plus BLOCK_COST bytes, against 4 * len + RSC_NAMES_BUDGET_EXTRA
 * (4096) bytes; beyond it the read fails with RSC_ETOOBIG.  Unlike
 * resource records, entries are not shared: every entry is made of its own
 * bytes of the file, so no file can exceed the budget today.  The densest
 * file is an .HRD of 8-byte records (a one-character name): each entry
 * is charged its slot in the array (sizeof(struct entry), 24 bytes with
 * 8-byte pointers) and its name (2 bytes plus BLOCK_COST), 30 bytes of the
 * 32 it brings (a .DFN record: at most 37 of 56, a .DEF record: 37 of 64).  The budget is the net that keeps memory linear in len if a
 * later format or change breaks that; the tests pin the charges and the
 * boundary (rsc_test_names_budget, rsc_test_names_charged).
 */
#include <string.h>
#include <limits.h>
#include "rscnames.h"
#include "rscatari.h"

/* What one allocation costs in the budget beyond its size. */
#define BLOCK_COST	4

/* The longest name a reader keeps (cut there, .HRD only). */
#define HRD_NAME_KEEP	16

struct entry
{
	char	*name;		/* rsc_malloc'd */
	int	tree, obj;	/* as struct rsc_name */
	unsigned char	kind, tkind, guessed;
};

struct rsc_names
{
	struct entry	*e;
	int	n, cap;
};

/* ---- characters ------------------------------------------------------------- */

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* a and b are equal with the ASCII letters compared without case */
static int fold_eq(const char *a, const char *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return *a == *b;
}

/* a and b are equal */
static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return *a == *b;
}

/* The n bytes at s are allowed in a name. */
static int chars_ok(const uint8_t *s, size_t n)
{
	size_t	k;

	for (k = 0; k < n; k++)
		if (s[k] < 0x20 || s[k] == 0x7f)
			return 0;
	return 1;
}

/* ---- the list --------------------------------------------------------------- */

struct rsc_names *rsc_names_new(void)
{
	struct rsc_names	*n = rsc_malloc(sizeof *n);

	if (n)
	{
		n->e = NULL;
		n->n = n->cap = 0;
	}
	return n;
}

void rsc_names_free(struct rsc_names *n)
{
	int	i;

	if (!n)
		return;
	for (i = 0; i < n->n; i++)
		rsc_mfree(n->e[i].name);
	rsc_mfree(n->e);
	rsc_mfree(n);
}

/* kind, tkind, tree and obj make an entry rsc_names_add accepts */
static int entry_ok(int kind, int tkind, int tree, int obj)
{
	switch (kind)
	{
	case RSC_NAME_TREE:
		return tkind >= RSC_TKIND_UNKN && tkind <= RSC_TKIND_DIAL
		       && tree >= 0 && tree <= RSC_NAMES_MAX_INDEX && obj == 0;
	case RSC_NAME_OBJ:
		return tkind == RSC_TKIND_UNKN && tree >= 0 && tree <= RSC_NAMES_MAX_INDEX
		       && obj >= 0 && obj < RSC_MAX_OBJECTS;
	case RSC_NAME_FRSTR:
		return (tkind == RSC_TKIND_UNKN || tkind == RSC_TKIND_ALRT) && tree == 0
		       && obj >= 0 && obj <= RSC_NAMES_MAX_INDEX;
	case RSC_NAME_FRIMG:
		return tkind == RSC_TKIND_UNKN && tree == 0 && obj >= 0 && obj <= RSC_NAMES_MAX_INDEX;
	default:
		return 0;
	}
}

/* Room for want entries in all (want <= RSC_NAMES_MAX). */
static int reserve(struct rsc_names *n, int want)
{
	struct entry	*e;
	int	cap;

	if (want <= n->cap)
		return 0;
	cap = n->cap ? n->cap : 8;
	while (cap < want)
		cap = cap > RSC_NAMES_MAX / 2 ? RSC_NAMES_MAX : cap * 2;
	e = rsc_realloc(n->e, (size_t)cap * sizeof *e);
	if (!e)
		return -1;
	n->e = e;
	n->cap = cap;
	return 0;
}

/* Appends an entry whose name is the len bytes at s (room reserved). */
static int append(struct rsc_names *n, int kind, int tkind, int tree, int obj,
		  const char *s, size_t len, int guessed)
{
	struct entry	*e = &n->e[n->n];

	e->name = rsc_malloc(len + 1);
	if (!e->name)
		return -1;
	memcpy(e->name, s, len);
	e->name[len] = '\0';
	e->tree = tree;
	e->obj = obj;
	e->kind = (unsigned char)kind;
	e->tkind = (unsigned char)tkind;
	e->guessed = guessed != 0;
	n->n++;
	return 0;
}

int rsc_names_add(struct rsc_names *n, int kind, int tkind, int tree, int obj,
		  const char *name, int guessed)
{
	size_t	len;

	if (!n || !name || !entry_ok(kind, tkind, tree, obj))
		return -1;
	len = strlen(name);
	if (len == 0 || len > RSC_NAME_MAX || !chars_ok((const uint8_t *)name, len))
		return -1;
	if (n->n >= RSC_NAMES_MAX || reserve(n, n->n + 1))
		return -1;
	return append(n, kind, tkind, tree, obj, name, len, guessed);
}

int rsc_names_count(const struct rsc_names *n)
{
	return n ? n->n : 0;
}

int rsc_names_get(const struct rsc_names *n, int i, struct rsc_name *out)
{
	const struct entry	*e;

	if (!n || !out || i < 0 || i >= n->n)
		return -1;
	e = &n->e[i];
	out->kind = e->kind;
	out->tkind = e->tkind;
	out->tree = e->tree;
	out->obj = e->obj;
	out->guessed = e->guessed;
	out->order = i;
	memset(out->name, 0, sizeof out->name);
	memcpy(out->name, e->name, strlen(e->name));
	return 0;
}

/* ---- the readers ------------------------------------------------------------- */

/* one record decoded */
struct rec
{
	int	kind, tkind, tree, obj;
	const uint8_t	*name;
	size_t	nlen;		/* the name in the file */
	size_t	keep;		/* what is kept of it */
	size_t	at;		/* the position of the record */
	size_t	name_at;	/* the position of the name */
};

struct rd
{
	const uint8_t	*buf;
	size_t	len;
	struct rsc_error	*err;
	size_t	budget, used;
	size_t	pos;		/* the next record (HRD) */
	long	idx, count;	/* the next record and the count (DFN, DEF) */
	int	(*next)(struct rd *d, struct rec *rc);
};

#ifdef RSC_TEST
static long	budget_extra = RSC_NAMES_BUDGET_EXTRA;
static size_t	last_charged;

void rsc_test_names_budget(long extra)
{
	budget_extra = extra;
}

size_t rsc_test_names_charged(void)
{
	return last_charged;
}
#define BUDGET_EXTRA	budget_extra
#else
#define BUDGET_EXTRA	RSC_NAMES_BUDGET_EXTRA
#endif

static int fail(struct rd *d, int code, size_t off, const char *what)
{
	rsc_error_set(d->err, code, off, what);
	return -1;
}

/* Charges an allocation of n bytes, made for the record at off. */
static int charge(struct rd *d, size_t n, size_t off)
{
	if (n > d->budget - d->used || BLOCK_COST > d->budget - d->used - n)
		return fail(d, RSC_ETOOBIG, off, "names exceed the size limit");
	d->used += n + BLOCK_COST;
	return 0;
}

static unsigned le16(const struct rd *d, size_t off)
{
	return (unsigned)d->buf[off] | (unsigned)d->buf[off + 1] << 8;
}

static unsigned be16(const struct rd *d, size_t off)
{
	return (unsigned)d->buf[off] << 8 | d->buf[off + 1];
}

/* The length of the name of at most max bytes at off: up to the first NUL. */
static size_t field_len(const struct rd *d, size_t off, size_t max)
{
	const uint8_t	*z = memchr(d->buf + off, 0, max);

	return z ? (size_t)(z - (d->buf + off)) : max;
}

static void set_rec(struct rec *rc, int kind, int tkind, int tree, int obj)
{
	rc->kind = kind;
	rc->tkind = tkind;
	rc->tree = tree;
	rc->obj = obj;
}

/* .DFN: the next record into *rc; 1, or 0 after the last. */
static int next_dfn(struct rd *d, struct rec *rc)
{
	size_t	p;
	unsigned	val, kind;

	if (d->idx >= d->count)
		return 0;
	p = 2 + 14 * (size_t)d->idx++;
	val = le16(d, p);
	kind = le16(d, p + 2);
	rc->at = p;
	rc->name_at = p + 4;
	rc->name = d->buf + p + 4;
	rc->nlen = rc->keep = field_len(d, p + 4, 8);
	if (kind <= RSC_TKIND_DIAL)
		set_rec(rc, RSC_NAME_TREE, (int)kind, (int)val, 0);
	else if (kind == RSC_TKIND_ALRT)
		set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, (int)val);
	else if (kind == 0x100)
		set_rec(rc, RSC_NAME_OBJ, RSC_TKIND_UNKN, (int)(val >> 8), (int)(val & 0xff));
	else if (kind == 0x101)
		set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, (int)val);
	else if (kind == 0x102)
		set_rec(rc, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, (int)val);
	else
		set_rec(rc, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, (int)val, (int)kind);
	return 1;
}

static int start_dfn(struct rd *d)
{
	size_t	want;

	if (d->len < 2)
		return fail(d, RSC_ETRUNC, d->len, "names file truncated");
	d->count = (long)le16(d, 0);
	want = 2 + 14 * (size_t)d->count;
	if (d->len < want)
		return fail(d, RSC_ETRUNC, d->len, "names file shorter than its count");
	if (d->len > want)
		return fail(d, RSC_EHEADER, want, "bytes after the last entry");
	d->next = next_dfn;
	return 0;
}

/* .DEF, .RSD */
static int next_def(struct rd *d, struct rec *rc)
{
	size_t	p;
	unsigned	t, o, ind, ty;

	if (d->idx >= d->count)
		return 0;
	p = 16 * (size_t)d->idx++;
	t = d->buf[p + 4];
	o = d->buf[p + 5];
	ind = d->buf[p + 6];
	ty = d->buf[p + 7];
	rc->at = p;
	rc->name_at = p + 8;
	rc->name = d->buf + p + 8;
	rc->nlen = rc->keep = field_len(d, p + 8, 8);
	if (ty == 0 && ind == 1)
		set_rec(rc, RSC_NAME_OBJ, RSC_TKIND_UNKN, (int)t, (int)o);
	else if (ty == 0 && ind == 0)
		set_rec(rc, RSC_NAME_TREE, RSC_TKIND_UNKN, (int)o, 0);
	else if (ty >= 1 && ty <= 3)
		set_rec(rc, RSC_NAME_TREE, (int)ty, (int)o, 0);
	else if (ty == 4)
		set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, (int)o);
	else if (ty == 5)
		set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, (int)o);
	else if (ty == 6)
		set_rec(rc, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, (int)o);
	else
		set_rec(rc, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, (int)(t << 8 | o), (int)(ind << 8 | ty));
	return 1;
}

static int start_def(struct rd *d)
{
	size_t	nrec, k;
	int	zero = 1;

	if (d->len < 16)
		return fail(d, RSC_ETRUNC, d->len, "names file truncated");
	if (d->len % 16)
		return fail(d, RSC_ETRUNC, d->len - d->len % 16, "names file ends inside a record");
	nrec = d->len / 16;
	d->count = (long)be16(d, 0);
	if ((size_t)d->count > nrec)
		return fail(d, RSC_ETRUNC, d->len, "names file shorter than its count");
	for (k = d->len - 16; k < d->len; k++)
		zero &= d->buf[k] == 0;
	if ((size_t)d->count < nrec && ((size_t)d->count != nrec - 1 || !zero))
		return fail(d, RSC_EHEADER, 0, "count does not match the file size");
	d->next = next_def;
	return 0;
}

/* .HRD */
static int next_hrd(struct rd *d, struct rec *rc)
{
	const uint8_t	*z;
	size_t	p;
	unsigned	ty, t, o;

	for (;;)
	{
		p = d->pos;
		if (p >= d->len)
			return fail(d, RSC_ETRUNC, d->len, "no end record");
		if (d->len - p < 6)
			return fail(d, RSC_ETRUNC, p, "entry truncated");
		z = memchr(d->buf + p + 6, 0, d->len - p - 6);
		if (!z)
			return fail(d, RSC_ETRUNC, p + 6, "name without NUL");
		d->pos = (size_t)(z - d->buf) + 1;
		ty = d->buf[p];
		t = be16(d, p + 2);
		o = be16(d, p + 4);
		if (ty == 6)
			return 0;
		if (ty == 7)
			continue;
		rc->at = p;
		rc->name_at = p + 6;
		rc->name = d->buf + p + 6;
		rc->nlen = (size_t)(z - rc->name);
		rc->keep = rc->nlen < HRD_NAME_KEEP ? rc->nlen : HRD_NAME_KEEP;
		if (ty <= 4 && o != 0)
			return fail(d, RSC_EOFFSET, p + 4, "object field not 0");
		if (ty == 0 || ty == 1)
			set_rec(rc, RSC_NAME_TREE, ty == 0 ? RSC_TKIND_DIAL : RSC_TKIND_MENU, (int)t, 0);
		else if (ty == 2)
			set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, (int)t);
		else if (ty == 3)
			set_rec(rc, RSC_NAME_FRSTR, RSC_TKIND_UNKN, 0, (int)t);
		else if (ty == 4)
			set_rec(rc, RSC_NAME_FRIMG, RSC_TKIND_UNKN, 0, (int)t);
		else if (ty == 5)
		{
			if (o >= RSC_MAX_OBJECTS)
				return fail(d, RSC_EOFFSET, p + 4, "object index out of range");
			set_rec(rc, RSC_NAME_OBJ, RSC_TKIND_UNKN, (int)t, (int)o);
		}
		else
			set_rec(rc, RSC_NAME_UNKNOWN, RSC_TKIND_UNKN, (int)t, (int)ty);
		return 1;
	}
}

static int start_hrd(struct rd *d)
{
	if (d->len < 8)
		return fail(d, RSC_ETRUNC, d->len, "names file truncated");
	if (be16(d, 0) != 1)
		return fail(d, RSC_EHEADER, 0, "unsupported .HRD version");
	d->next = next_hrd;
	return 0;
}

/* The two passes over the file (start set up d->next). */
static struct rsc_names *read_names(struct rd *d, int (*start)(struct rd *))
{
	struct rsc_names	*n = NULL;
	struct rec	rc;
	long	count = 0;
	int	k;

	d->pos = 8;
	d->idx = 0;
	if (start(d))
		return NULL;
	while ((k = d->next(d, &rc)) > 0)
	{
		if (rc.nlen == 0 || !chars_ok(rc.name, rc.nlen))
		{
			fail(d, RSC_ESTRING, rc.name_at, "bad name");
			return NULL;
		}
		if (count == RSC_NAMES_MAX)
		{
			fail(d, RSC_ETOOBIG, rc.at, "too many names");
			return NULL;
		}
		count++;
	}
	if (k < 0)
		return NULL;
	if (charge(d, sizeof *n, 0))
		return NULL;
	n = rsc_names_new();
	if (!n)
		goto nomem;
	if (count)
	{
		if (charge(d, (size_t)count * sizeof n->e[0], 0))
			goto out;
		n->e = rsc_malloc((size_t)count * sizeof n->e[0]);
		if (!n->e)
			goto nomem;
		n->cap = (int)count;
	}
	d->pos = 8;
	d->idx = 0;
	while (d->next(d, &rc) > 0)
	{
		if (charge(d, rc.keep + 1, rc.at))
			goto out;
		if (append(n, rc.kind, rc.tkind, rc.tree, rc.obj, (const char *)rc.name, rc.keep, 0))
			goto nomem;
	}
	return n;
nomem:
	fail(d, RSC_ENOMEM, 0, "out of memory");
out:
	rsc_names_free(n);
	return NULL;
}

static struct rsc_names *read_with(const uint8_t *buf, size_t len, struct rsc_error *err,
				   int (*start)(struct rd *))
{
	struct rd	d;
	struct rsc_names	*n;
	long	extra = BUDGET_EXTRA;
	size_t	base, neg;

	memset(&d, 0, sizeof d);
	d.buf = buf;
	d.len = len;
	d.err = err;
	if (!buf && len)
	{
		fail(&d, RSC_EARG, 0, "no buffer");
		return NULL;
	}
	if (!buf)
		d.buf = (const uint8_t *)"";
	base = len <= SIZE_MAX / 4 ? 4 * len : SIZE_MAX;
	if (extra >= 0)
		d.budget = base > SIZE_MAX - (size_t)extra ? SIZE_MAX : base + (size_t)extra;
	else
	{
		neg = (size_t)-(extra + 1) + 1;
		d.budget = base > neg ? base - neg : 0;
	}
	n = read_names(&d, start);
	if (n && err)
	{
		err->code = RSC_OK;
		err->offset = 0;
		err->msg[0] = '\0';
	}
#ifdef RSC_TEST
	last_charged = d.used;
#endif
	return n;
}

struct rsc_names *rsc_names_read_dfn(const uint8_t *buf, size_t len, struct rsc_error *err)
{
	return read_with(buf, len, err, start_dfn);
}

struct rsc_names *rsc_names_read_def(const uint8_t *buf, size_t len, struct rsc_error *err)
{
	return read_with(buf, len, err, start_def);
}

struct rsc_names *rsc_names_read_hrd(const uint8_t *buf, size_t len, struct rsc_error *err)
{
	return read_with(buf, len, err, start_hrd);
}

struct rsc_names *rsc_names_read_by_ext(const char *filename, const uint8_t *buf, size_t len,
					struct rsc_error *err)
{
	const char	*dot = NULL, *s;

	for (s = filename; s && *s; s++)
		if (*s == '.')
			dot = s;

	if (dot && fold_eq(dot, ".dfn"))
		return rsc_names_read_dfn(buf, len, err);
	if (dot && (fold_eq(dot, ".def") || fold_eq(dot, ".rsd")))
		return rsc_names_read_def(buf, len, err);
	if (dot && fold_eq(dot, ".hrd"))
		return rsc_names_read_hrd(buf, len, err);
	rsc_error_set(err, RSC_EARG, 0, "unknown names file extension");
	return NULL;
}

/* ---- against a resource ------------------------------------------------------ */

int rsc_names_check(struct rsc_names *n, const struct rsc *r, int *dropped)
{
	int	i, j, nobj, drop = 0;
	const struct entry	*e;

	if (dropped)
		*dropped = 0;
	if (!n || !r)
		return -1;
	for (i = j = 0; i < n->n; i++)
	{
		int	ok;

		e = &n->e[i];
		switch (e->kind)
		{
		case RSC_NAME_TREE:
			ok = e->tree < rsc_ntrees(r);
			break;
		case RSC_NAME_OBJ:
			ok = rsc_tree_objects(r, e->tree, &nobj) != NULL && e->obj < nobj;
			break;
		case RSC_NAME_FRSTR:
			ok = e->obj < rsc_nfrstr(r);
			break;
		case RSC_NAME_FRIMG:
			ok = e->obj < rsc_nfrimg(r);
			break;
		default:
			ok = 0;
			break;
		}
		if (ok)
			n->e[j++] = *e;
		else
		{
			rsc_mfree(e->name);
			drop++;
		}
	}
	n->n = j;
	if (dropped)
		*dropped = drop;
	return 0;
}

/* ---- the guesser -------------------------------------------------------------- */

#define FLAGS_DIAL	(OF_EXIT | OF_DEFAULT | OF_TOUCHEXIT | OF_EDITABLE)

/* The kind of the tree of the nobj objects at o, as the old RCS guessed it;
 * order has room for nobj ints.  -1: no memory. */
static int guess_kind(const OBJECT *o, int nobj, int *order)
{
	int	b, ti, f, k, i, type;

	b = o[0].ob_head;
	if (b >= 0 && b < nobj)
	{
		ti = o[b].ob_head;
		f = ti >= 0 && ti < nobj ? o[ti].ob_head : -1;
		if (f >= 0 && f < nobj && (o[f].ob_type & 0xff) == G_TITLE)
			return RSC_TKIND_MENU;
	}
	k = rsc_tree_preorder(o, nobj, order);
	if (k == RSC_WALK_ENOMEM)
		return -1;
	if (k < 1)
	{
		order[0] = 0;	/* broken links: the root alone */
		k = 1;
	}
	for (i = 0; i < k; i++)
		if (o[order[i]].ob_flags & FLAGS_DIAL)
			return RSC_TKIND_DIAL;
	type = o[0].ob_type & 0xff;
	if (type == G_BOX || type == G_IBOX || type == G_BOXTEXT)
		return RSC_TKIND_PANL;
	return RSC_TKIND_UNKN;
}

/* The smallest free number at or after x in the next-free table nx. */
static int uf_find(int *nx, int x)
{
	int	root = x, y;

	while (nx[root] != root)
		root = nx[root];
	while (nx[x] != root)
	{
		y = nx[x];
		nx[x] = root;
		x = y;
	}
	return root;
}

/* The patterns: ALERT for free strings, then the tree kinds UNKN, PANL,
 * MENU, DIAL. */
static const char *const pattern[5] = { "ALERT", "TREE", "PANEL", "MENU", "DIALOG" };

/* When name is pattern p followed by a number without leading zeros below
 * lim, the number; else -1. */
static long numbered(const char *name, const char *p, long lim)
{
	long	v = 0;

	while (*p && lower((unsigned char)*name) == lower((unsigned char)*p))
	{
		name++;
		p++;
	}
	if (*p || *name < '1' || *name > '9')
		return -1;
	for (; *name; name++)
	{
		if (*name < '0' || *name > '9')
			return -1;
		v = v * 10 + (*name - '0');
		if (v >= lim)
			return -1;
	}
	return v;
}

/* pattern p and the number v as a name in buf (room for RSC_NAME_MAX + 1) */
static size_t make_name(char *buf, int p, int v)
{
	char	dig[12];
	size_t	n = strlen(pattern[p]), k = 0;

	memcpy(buf, pattern[p], n);
	do
	{
		dig[k++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (k)
		buf[n++] = dig[--k];
	buf[n] = '\0';
	return n;
}

int rsc_names_guess(struct rsc_names *n, const struct rsc *r)
{
	unsigned char	*kind = NULL;	/* per tree: 0xff named, else the guessed kind;
					 * then per string: 1 to name */
	int	*order = NULL, *nx = NULL, *tab[5];
	int	ntrees, nstr, t, k, i, p, maxobj = 0, nobj, add = 0, nal = 0, ntr = 0, e0, ret = -1;
	long	lim[5];
	OBJECT	*o;

	if (!n || !r)
		return -1;
	ntrees = rsc_ntrees(r);
	nstr = rsc_nfrstr(r);
	e0 = n->n;
	kind = rsc_malloc((size_t)ntrees + (size_t)nstr);
	if (!kind)
		return -1;
	memset(kind, 0, (size_t)ntrees + (size_t)nstr);
	for (i = 0; i < n->n; i++)
		if (n->e[i].kind == RSC_NAME_TREE && n->e[i].tree < ntrees)
			kind[n->e[i].tree] = 0xff;
		else if (n->e[i].kind == RSC_NAME_FRSTR && n->e[i].obj < nstr)
			kind[ntrees + n->e[i].obj] = 0xff;
	for (k = 0; k < nstr; k++)
	{
		const char	*s = rsc_frstr(r, k);

		if (kind[ntrees + k] == 0xff || !s || s[0] != '[' || s[1] == '\0' || s[2] != ']')
			kind[ntrees + k] = 0;
		else
		{
			kind[ntrees + k] = 1;
			nal++;
		}
	}
	for (t = 0; t < ntrees; t++)
		if (kind[t] != 0xff)
		{
			rsc_tree_objects(r, t, &nobj);
			maxobj = nobj > maxobj ? nobj : maxobj;
			ntr++;
		}
	add = nal + ntr;
	if (add == 0)
	{
		ret = 0;
		goto out;
	}
	if (add > RSC_NAMES_MAX - e0)
		goto out;
	if (ntr)
	{
		order = rsc_malloc((size_t)maxobj * sizeof *order);
		if (!order)
			goto out;
		for (t = 0; t < ntrees; t++)
			if (kind[t] != 0xff)
			{
				o = rsc_tree_objects(r, t, &nobj);
				k = guess_kind(o, nobj, order);
				if (k < 0)
					goto out;
				kind[t] = (unsigned char)k;
			}
	}
	/* the next-free tables: a number below lim - 1 is taken when the list
	 * has it; lim - 1 is never taken */
	lim[0] = (long)nstr + e0 + nal + 2;
	for (p = 1; p < 5; p++)
		lim[p] = (long)ntrees + e0 + ntr + 2;
	nx = rsc_malloc(((size_t)lim[0] + 4 * (size_t)lim[1]) * sizeof *nx);
	if (!nx)
		goto out;
	for (p = 0; p < 5; p++)
	{
		tab[p] = p ? tab[p - 1] + lim[p - 1] : nx;
		for (i = 0; i < lim[p]; i++)
			tab[p][i] = i;
		for (i = 0; i < e0; i++)
		{
			long	v = numbered(n->e[i].name, pattern[p], lim[p] - 1);

			if (v >= 0)
				tab[p][v] = (int)v + 1;
		}
	}
	if (reserve(n, e0 + add))
		goto out;
	for (k = 0; k < nstr; k++)
		if (kind[ntrees + k] == 1)
		{
			char	name[RSC_NAME_MAX + 1];
			int	v = uf_find(tab[0], k);

			tab[0][v] = v + 1;
			if (append(n, RSC_NAME_FRSTR, RSC_TKIND_ALRT, 0, k, name, make_name(name, 0, v), 1))
				goto undo;
		}
	for (t = 0; t < ntrees; t++)
		if (kind[t] != 0xff)
		{
			char	name[RSC_NAME_MAX + 1];
			int	*tb = tab[1 + kind[t]];
			int	v = uf_find(tb, t + 1);

			tb[v] = v + 1;
			if (append(n, RSC_NAME_TREE, kind[t], t, 0, name, make_name(name, 1 + kind[t], v), 1))
				goto undo;
		}
	ret = add;
	goto out;
undo:
	while (n->n > e0)
		rsc_mfree(n->e[--n->n].name);
out:
	rsc_mfree(kind);
	rsc_mfree(order);
	rsc_mfree(nx);
	return ret;
}

/* ---- lookup ---------------------------------------------------------------------- */

/* comp is '#' and digits only: its value, RSC_NAMES_NOTFOUND when it is
 * count or more; -2 when comp is not of that form. */
static int index_of(const char *comp, int count)
{
	const char	*s;
	int	v = 0, over = 0, dg;

	if (comp[0] != '#' || comp[1] == '\0')
		return -2;
	for (s = comp + 1; *s; s++)
		if (*s < '0' || *s > '9')
			return -2;
	for (s = comp + 1; *s; s++)
	{
		dg = *s - '0';
		if (over || v > (INT_MAX - dg) / 10)
			over = 1;
		else
			v = v * 10 + dg;
	}
	return over || v >= count ? RSC_NAMES_NOTFOUND : v;
}

/* the node entry e names in a scope of kind kind (tree t for objects), or -1 */
static int node_of(const struct entry *e, int kind, int t)
{
	if (e->kind != kind || (kind == RSC_NAME_OBJ && e->tree != t))
		return -1;
	return kind == RSC_NAME_TREE ? e->tree : e->obj;
}

static int find(const struct rsc_names *n, int kind, int t, const char *comp, int count)
{
	int	i, pass, node, found;

	if (!comp || count <= 0)
		return RSC_NAMES_NOTFOUND;
	i = index_of(comp, count);
	if (i != -2)
		return i;
	if (!n)
		return RSC_NAMES_NOTFOUND;
	for (pass = 0; pass < 2; pass++)
	{
		found = -1;
		for (i = 0; i < n->n; i++)
		{
			node = node_of(&n->e[i], kind, t);
			if (node < 0 || node >= count)
				continue;
			if (pass == 0 ? !str_eq(n->e[i].name, comp) : !fold_eq(n->e[i].name, comp))
				continue;
			if (found >= 0 && found != node)
				return RSC_NAMES_AMBIGUOUS;
			found = node;
		}
		if (found >= 0)
			return found;
	}
	return RSC_NAMES_NOTFOUND;
}

int rsc_names_find_tree(const struct rsc_names *n, const char *comp, int count)
{
	return find(n, RSC_NAME_TREE, 0, comp, count);
}

int rsc_names_find_obj(const struct rsc_names *n, int t, const char *comp, int count)
{
	return find(n, RSC_NAME_OBJ, t, comp, count);
}

int rsc_names_find_str(const struct rsc_names *n, const char *comp, int count)
{
	return find(n, RSC_NAME_FRSTR, 0, comp, count);
}

int rsc_names_find_img(const struct rsc_names *n, const char *comp, int count)
{
	return find(n, RSC_NAME_FRIMG, 0, comp, count);
}

/* the first entry of node k in the scope, or NULL */
static const struct entry *first(const struct rsc_names *n, int kind, int t, int k)
{
	int	i;

	for (i = 0; n && i < n->n; i++)
		if (node_of(&n->e[i], kind, t) == k && k >= 0)
			return &n->e[i];
	return NULL;
}

static const char *name_of(const struct entry *e)
{
	return e ? e->name : NULL;
}

const char *rsc_names_tree_name(const struct rsc_names *n, int t)
{
	return name_of(first(n, RSC_NAME_TREE, 0, t));
}

const char *rsc_names_obj_name(const struct rsc_names *n, int t, int o)
{
	return name_of(first(n, RSC_NAME_OBJ, t, o));
}

const char *rsc_names_str_name(const struct rsc_names *n, int k)
{
	return name_of(first(n, RSC_NAME_FRSTR, 0, k));
}

const char *rsc_names_img_name(const struct rsc_names *n, int k)
{
	return name_of(first(n, RSC_NAME_FRIMG, 0, k));
}

int rsc_names_tree_kind(const struct rsc_names *n, int t, int *guessed)
{
	const struct entry	*e = first(n, RSC_NAME_TREE, 0, t);

	if (guessed)
		*guessed = e ? e->guessed : 0;
	return e ? e->tkind : -1;
}

/* ---- following edits ---------------------------------------------------------------- */

int rsc_names_renumber(struct rsc_names *n, int t, const int *map, int nmap)
{
	int	i, j;

	if (!n || !map || nmap < 0)
		return -1;
	for (i = 0; i < nmap; i++)
		if (map[i] < -1 || map[i] >= RSC_MAX_OBJECTS)
			return -1;
	for (i = j = 0; i < n->n; i++)
	{
		struct entry	*e = &n->e[i];

		if (e->kind == RSC_NAME_OBJ && e->tree == t)
		{
			if (e->obj >= nmap || map[e->obj] < 0)
			{
				rsc_mfree(e->name);
				continue;
			}
			e->obj = map[e->obj];
		}
		n->e[j++] = *e;
	}
	n->n = j;
	return 0;
}

int rsc_names_tree_removed(struct rsc_names *n, int t)
{
	int	i, j;

	if (!n || t < 0)
		return -1;
	for (i = j = 0; i < n->n; i++)
	{
		struct entry	*e = &n->e[i];

		if ((e->kind == RSC_NAME_TREE || e->kind == RSC_NAME_OBJ) && e->tree >= t)
		{
			if (e->tree == t)
			{
				rsc_mfree(e->name);
				continue;
			}
			e->tree--;
		}
		n->e[j++] = *e;
	}
	n->n = j;
	return 0;
}
