/* RSCNAMES.H	the names of a resource file: what a .DFN, .DEF, .RSD or .HRD
 *		file says about the trees, objects, free strings and free
 *		images of a resource, kept apart from struct rsc and keyed by
 *		indices
 *
 * A struct rsc_names is an ordered list of entries, in file order (then
 * what rsc_names_add and rsc_names_guess append).  An entry is
 *  - kind: a tree, an object, a free string or a free image name (or, from
 *    a file only, an entry of a kind this library does not know, kept so
 *    that rsc_names_check can count it);
 *  - tkind: the kind of a tree (UNKN, PANL, MENU, DIAL), or ALRT for a free
 *    string that is an alert (the library keeps alerts as free strings:
 *    there are no pseudo-trees), UNKN otherwise;
 *  - tree, obj: see struct rsc_name;
 *  - the name: 1 to RSC_NAME_MAX bytes, no control character (below 0x20,
 *    and 0x7f); bytes from 0x80 up are kept as they are (the Atari
 *    character set); the case is kept;
 *  - guessed: set on the entries rsc_names_guess made.
 *
 * The list may hold several entries for one node and one name for several
 * nodes: the files do, and the lookups report what that makes ambiguous.
 * The display name of a node is that of its first entry in file order.
 *
 * Ownership as in rscmodel.h: the list owns its names; a name handed out is
 * borrowed, valid until the next call that changes the list.  A call that
 * can fail returns -1 (or NULL) and leaves the list as it was.  All memory
 * comes from rsc_malloc and friends.
 *
 * The readers (rscnames.c tells the formats and their rules) treat the
 * file as hostile in the way rsc_atari_read does: every count and offset
 * is checked against the length, the work and memory are linear in it, and
 * a structural error fails the read with a code, the position and a
 * message (struct rsc_error of rscfile.h).  They do not compare the names
 * with a resource: rsc_names_check does.
 */
#ifndef RSCNAMES_H
#define RSCNAMES_H

#include <stddef.h>
#include <stdint.h>
#include "rscmodel.h"
#include "rscfile.h"

/* The longest name (the files have at most 8 or 16 characters). */
#define RSC_NAME_MAX		32
/* The most entries a list holds (a reader refuses more: RSC_ETOOBIG). */
#define RSC_NAMES_MAX		65535
/* The largest tree, free string or free image index an entry may have
 * (counts are 16-bit in the files); object indices are below
 * RSC_MAX_OBJECTS. */
#define RSC_NAMES_MAX_INDEX	65535

enum rsc_name_kind
{
	RSC_NAME_TREE,
	RSC_NAME_OBJ,
	RSC_NAME_FRSTR,
	RSC_NAME_FRIMG,
	RSC_NAME_UNKNOWN	/* from a file only: a kind this library does not know */
};

/* The kinds of a tree, with the numbers of the .DFN format. */
enum rsc_tree_kind
{
	RSC_TKIND_UNKN = 0,
	RSC_TKIND_PANL = 1,
	RSC_TKIND_MENU = 2,
	RSC_TKIND_DIAL = 3,
	RSC_TKIND_ALRT = 4	/* free strings only */
};

/* An entry, copied out by rsc_names_get. */
struct rsc_name
{
	int	kind;		/* enum rsc_name_kind */
	int	tkind;		/* enum rsc_tree_kind */
	int	tree;		/* TREE and OBJ: the tree index; FRSTR, FRIMG: 0;
				 * UNKNOWN: the value field of the file */
	int	obj;		/* OBJ: the object index; FRSTR, FRIMG: the index of
				 * the string or image; TREE: 0; UNKNOWN: the kind or
				 * type code of the file */
	int	guessed;	/* made by rsc_names_guess */
	int	order;		/* the position in the list (file order) */
	char	name[RSC_NAME_MAX + 1];
};

struct rsc_names;

/* An empty list, or NULL. */
struct rsc_names *rsc_names_new(void);
/* Frees the list and its names; NULL is allowed. */
void rsc_names_free(struct rsc_names *n);

/* Appends an entry and returns 0, or -1: a bad name (empty, longer than
 * RSC_NAME_MAX, a control character), kind not TREE, OBJ, FRSTR or FRIMG,
 * a tkind that does not fit the kind (TREE: UNKN, PANL, MENU, DIAL; FRSTR:
 * UNKN or ALRT; OBJ and FRIMG: UNKN), an index out of range (tree and the
 * string or image index 0..RSC_NAMES_MAX_INDEX, object 0..RSC_MAX_OBJECTS
 * - 1; the unused one of tree and obj must be 0), a list of RSC_NAMES_MAX
 * entries already, or no memory.  Duplicates are allowed. */
int rsc_names_add(struct rsc_names *n, int kind, int tkind, int tree, int obj,
		  const char *name, int guessed);

/* The number of entries (0 for NULL), and entry i copied to *out (0, or -1
 * for a bad index). */
int rsc_names_count(const struct rsc_names *n);
int rsc_names_get(const struct rsc_names *n, int i, struct rsc_name *out);

/* ---- the readers ------------------------------------------------------------ */

/* Decode the len bytes at buf into a new list, or return NULL and fill
 * *err (err may be NULL).  Nothing of buf is referenced by the result.
 * rsc_names_read_def also reads .RSD files (the same format). */
struct rsc_names *rsc_names_read_dfn(const uint8_t *buf, size_t len, struct rsc_error *err);
struct rsc_names *rsc_names_read_def(const uint8_t *buf, size_t len, struct rsc_error *err);
struct rsc_names *rsc_names_read_hrd(const uint8_t *buf, size_t len, struct rsc_error *err);
/* The reader the extension of filename names (.dfn, .def, .rsd, .hrd, in
 * any case); any other name is RSC_EARG at offset 0. */
struct rsc_names *rsc_names_read_by_ext(const char *filename, const uint8_t *buf, size_t len,
					struct rsc_error *err);

/* ---- against a resource -------------------------------------------------------- */

/* Removes the entries r has no node for: a tree index at or above
 * rsc_ntrees, an object index at or above the object count of its tree, a
 * free string or image index at or above rsc_nfrstr or rsc_nfrimg, and
 * entries of kind UNKNOWN.  *dropped (may be NULL) is the number removed.
 * It allocates nothing.  Returns 0, or -1 for a NULL list or resource. */
int rsc_names_check(struct rsc_names *n, const struct rsc *r, int *dropped);

/* Names what the list leaves unnamed, as the old RCS did:
 *  1. each free string without an entry whose text has the shape of an
 *     alert ('[', any character, ']') gets the FRSTR entry ALERT<k> of tkind
 *     ALRT, k its 0-based index;
 *  2. each tree without a TREE entry gets one: kind MENU if
 *     head(head(head(root))) is a G_TITLE, else DIAL if an object the root
 *     reaches has OF_EXIT, OF_DEFAULT, OF_TOUCHEXIT or OF_EDITABLE, else
 *     PANL if the root is a G_BOX, G_IBOX or G_BOXTEXT, else UNKN; and the
 *     name MENU<n>, DIALOG<n>, PANEL<n> or TREE<n>, n = tree index + 1.
 * Both count n (k) up while the name is already in the list (compared
 * without case: the lookups match without case too), entries made here
 * included.  Free strings that are not alerts and free images stay
 * unnamed; a tree with a TREE entry keeps that entry's kind.  The new
 * entries are appended, flagged guessed.  Returns their number, or -1 (no
 * memory, NULL argument, or the list would exceed RSC_NAMES_MAX) and then
 * the list is unchanged.  A tree whose links are broken is judged by its
 * root alone (a model from rsc_atari_read never has broken links). */
int rsc_names_guess(struct rsc_names *n, const struct rsc *r);

/* ---- lookup ---------------------------------------------------------------- */

/* What the lookups return besides an index. */
enum rsc_names_find_error
{
	RSC_NAMES_NOTFOUND = -1,	/* no such name, or #N out of range */
	RSC_NAMES_AMBIGUOUS = -2	/* the name names two or more nodes */
};

/* The node a path component names, among count nodes (the trees, the
 * objects of tree t, the free strings, the free images); n may be NULL
 * (no names).  The rules:
 *  - '#' followed by one or more digits, and nothing else, is always the
 *    index (leading zeros allowed): a node named "#3" cannot be found by
 *    its name; an index at or above count is RSC_NAMES_NOTFOUND (also when
 *    it would overflow).  '#' alone or followed by anything but digits only
 *    is a name;
 *  - otherwise the entries of the scope whose index is below count are
 *    searched for an exact match; when the matches name exactly one node,
 *    that is it, two or more is RSC_NAMES_AMBIGUOUS;
 *  - without an exact match, the same with the ASCII letters compared
 *    without case (bytes from 0x80 up must be equal);
 *  - no match is RSC_NAMES_NOTFOUND.
 * Two entries with the same name for the same node are not ambiguous. */
int rsc_names_find_tree(const struct rsc_names *n, const char *comp, int count);
int rsc_names_find_obj(const struct rsc_names *n, int t, const char *comp, int count);
int rsc_names_find_str(const struct rsc_names *n, const char *comp, int count);
int rsc_names_find_img(const struct rsc_names *n, const char *comp, int count);

/* The display name of a node: the name of its first entry in file order,
 * or NULL (no entry, or n NULL).  Borrowed.  Each is a scan of the list. */
const char *rsc_names_tree_name(const struct rsc_names *n, int t);
const char *rsc_names_obj_name(const struct rsc_names *n, int t, int o);
const char *rsc_names_str_name(const struct rsc_names *n, int k);
const char *rsc_names_img_name(const struct rsc_names *n, int k);
/* The tkind of the first TREE entry of tree t, and in *guessed (may be
 * NULL) whether rsc_names_guess made it; -1 when the tree has none. */
int rsc_names_tree_kind(const struct rsc_names *n, int t, int *guessed);

/* ---- following edits of the resource ------------------------------------------ */

/* Applies the old-to-new object map of rsc_tree_compact (map[old] the new
 * index or -1, nmap the old object count) to the OBJ entries of tree t:
 * entries mapped to -1, or whose object is at or above nmap, are removed.
 * Returns 0, or -1 (NULL argument, nmap < 0, or a map value below -1 or at
 * or above RSC_MAX_OBJECTS) and then nothing has changed.  It allocates
 * nothing. */
int rsc_names_renumber(struct rsc_names *n, int t, const int *map, int nmap);

/* For rsc_tree_delete(r, t): removes the TREE and OBJ entries of tree t and
 * moves those of the later trees down by one.  Returns 0, or -1 for a NULL
 * list or t < 0.  It allocates nothing. */
int rsc_names_tree_removed(struct rsc_names *n, int t);

/* The readers' budget is 4 * len + RSC_NAMES_BUDGET_EXTRA bytes. */
#define RSC_NAMES_BUDGET_EXTRA	4096

#ifdef RSC_TEST
/* Replaces the constant of the readers' budget (may be negative: the
 * budget is then 4 * len + extra, at least 0; RSC_NAMES_BUDGET_EXTRA
 * restores it), and what the last read charged. */
void rsc_test_names_budget(long extra);
size_t rsc_test_names_charged(void);
#endif

#endif
