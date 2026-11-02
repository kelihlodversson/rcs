/* RCSNODE.H	the node layer of rcs-cli: a resource file and its names seen as
 *		a filesystem of addressable nodes
 *
 * A node is something a path can address (docs/rcs-cli.md, "Paths"):
 *  - the root "/", which holds the trees and the two directories
 *    ".strings" and ".images";
 *  - a tree (index t): a directory of the children of its root object;
 *  - an object (tree t, object o): a directory when it has children or is
 *    a G_CICON object with a colour icon;
 *  - "/.strings" and "/.images", the directories of the free strings and
 *    the free images, and a free string or free image (index k);
 *  - an entry of a colour icon: "mono", and per colour version v "<planes>"
 *    and, when the version has a selected image, "<planes>.sel".  When an
 *    earlier version has the same plane count the entry is "<planes>#<v>"
 *    ("<planes>#<v>.sel"), v the 0-based version index; "<planes>#<v>" is
 *    also accepted for the first version of a plane count.
 *
 * A struct rcs_node carries indices only, never a pointer into the model,
 * so that it stays meaningful across an edit that reallocates (stage 3).
 * The functions take a struct rcs_view, which pairs a resource with its
 * names (NULL: no names).  Names come from the accessors and lookups of
 * rscnames.h on every use (the view keeps no copy of them); the view
 * caches only, per tree, which objects the root reaches and their parents
 * (from rsc_tree_preorder, built on first use).  After an edit of the
 * resource, call rcs_view_reset before using the view again.
 *
 * The node layer reads no file and prints nothing; messages are written to
 * a buffer the caller passes (at most msgsize bytes with the NUL), with
 * user input escaped (rcs_escape) and bounded.  Every walk over a tree is
 * iterative and bounded by the object count, so a 32,767-level chain costs
 * no C stack.  Memory is malloc'd (host only); every function that can fail
 * returns -1 and leaves the view usable.
 */
#ifndef RCSNODE_H
#define RCSNODE_H

#include <stddef.h>
#include "rscmodel.h"
#include "rscnames.h"

/* Room for a canonical path: "/" tree "/" object "/" entry, each name at
 * most RSC_NAME_MAX bytes escaped to at most 4 characters a byte. */
#define RCS_PATH_MAX	512
/* Room for a message of the node layer. */
#define RCS_MSG_MAX	512

enum rcs_node_kind
{
	RCS_NODE_ROOT,		/* "/" */
	RCS_NODE_TREE,		/* tree */
	RCS_NODE_OBJ,		/* tree, obj */
	RCS_NODE_STRDIR,	/* "/.strings" */
	RCS_NODE_IMGDIR,	/* "/.images" */
	RCS_NODE_FRSTR,		/* index */
	RCS_NODE_FRIMG,		/* index */
	RCS_NODE_ICON		/* tree, obj, index (the version, -1 for mono), sel */
};

struct rcs_node
{
	int	kind;		/* enum rcs_node_kind */
	int	tree;		/* TREE, OBJ, ICON; else 0 */
	int	obj;		/* OBJ, ICON; else 0 */
	int	index;		/* FRSTR, FRIMG: the index; ICON: the version or -1; else 0 */
	int	sel;		/* ICON: 1 for the selected image of a version; else 0 */
};

struct rcs_view;

/* A view of r with names n (may be NULL), or NULL (no memory).  The view
 * borrows r and n: they must outlive it. */
struct rcs_view *rcs_view_new(const struct rsc *r, const struct rsc_names *n);
/* Frees the view (not r or n); NULL is allowed. */
void rcs_view_free(struct rcs_view *v);
/* Drops the caches after an edit of r or n.  Returns 0 or -1 (no memory:
 * the view then has no names until the next successful reset). */
int rcs_view_reset(struct rcs_view *v);

/* Two nodes are the same node. */
int rcs_node_eq(const struct rcs_node *a, const struct rcs_node *b);

/* Resolves path to *out.  The empty path is "/"; otherwise the path starts
 * with '/', components are separated by '/', one trailing '/' is ignored
 * and an empty component ("//") is an error.  Components are matched as
 * docs/rcs-cli.md says: at the root ".strings" and ".images" first, then
 * the trees (rsc_names_find_tree); below a tree any object of the tree
 * (rsc_names_find_obj, reachable or not); below an object the entries of
 * its colour icon first, then its children only; in the two directories
 * the free strings or images.  Returns 0, or -1 with a message in msg:
 * "not found: 'C' in P", "ambiguous name 'C' in P (use #N)", "not a
 * directory: P", "bad path 'X': ...", P the path as typed up to the
 * component C. */
int rcs_resolve(struct rcs_view *v, const char *path, struct rcs_node *out,
		char *msg, size_t msgsize);

/* Whether node is a directory: the root, a tree, the two directories, an
 * object the root reaches that has children or a colour icon. */
int rcs_is_dir(struct rcs_view *v, const struct rcs_node *node);

/* The children of a directory, in order: for the root the trees in file
 * order then ".strings" and ".images"; for a tree the children of its root;
 * for an object the entries of its colour icon (mono, then the versions in
 * chain order, each followed by its .sel entry), then its children in
 * sibling order (ob_head, ob_next); free strings and images by index.  A
 * node that is not a directory has no children.  *kids is a malloc'd array
 * of *count nodes (NULL when there are none) that the caller frees with
 * free().  Returns 0, or -1 with a message (no memory, broken links). */
int rcs_children(struct rcs_view *v, const struct rcs_node *dir, struct rcs_node **kids,
		 int *count, char *msg, size_t msgsize);
/* The number of children rcs_children would return, without allocating;
 * -1 for broken links or no memory. */
int rcs_nchildren(struct rcs_view *v, const struct rcs_node *dir);

/* The display name of a node: the name of its first entry (a tree, an
 * object, a free string or image), ".strings" or ".images", "/" for the
 * root, the entry name of a colour icon entry; NULL when the node has
 * none.  Borrowed: a name from the names list is valid until the list
 * changes; the name of a colour icon entry until the next call. */
const char *rcs_node_name(struct rcs_view *v, const struct rcs_node *node);

/* The component that addresses node in its directory, into buf (size
 * bytes): its name when the name finds this node again (unique in its
 * scope, not "#digits", no '/', at the root not ".strings" or ".images"),
 * else "#N"; the entry name for a colour icon entry; "/" for the root.
 * The name is raw (not escaped).  Returns 0, or -1 when it does not fit. */
int rcs_node_component(struct rcs_view *v, const struct rcs_node *node, char *buf, size_t size);

/* The canonical path of node into buf: "/", "/T", "/T/O" (objects are
 * addressed below their tree, whatever their depth), "/T/O/E" for a colour
 * icon entry, "/.strings/K", "/.images/K", each component as
 * rcs_node_component makes it.  rcs_resolve of the result gives node
 * again.  Raw bytes, not escaped.  Returns 0, or -1 when it does not fit. */
int rcs_node_path(struct rcs_view *v, const struct rcs_node *node, char *buf, size_t size);

/* Everything ls -l and stat show about a node.  The pointers are borrowed
 * from the model and the view: valid until the next change of either. */
struct rcs_desc
{
	int		kind;		/* enum rcs_node_kind */
	const char	*name;		/* rcs_node_name */

	/* ROOT */
	int		ntrees, nfrstr, nfrimg;
	int		cell_w, cell_h;	/* rsc_cell: 0, 0 for file form */
	unsigned	vrsn;

	/* STRDIR, IMGDIR: the number of entries */
	int		count;

	/* TREE (index in tree) */
	int		tkind;		/* enum rsc_tree_kind of the first tree entry, or -1 */
	int		tkind_guessed;	/* that entry was guessed */
	int		nobj;		/* objects in the array (also OBJ) */
	int		nchildren;	/* children of the root (TREE), of the object (OBJ) */

	/* OBJ and ICON (tree, obj) */
	int		tree, obj;
	int		reachable;	/* the root reaches the object */
	int		parent;		/* the parent object, -1 for the root or unreachable */
	const OBJECT	*o;
	int		spec_kind;	/* rsc_obj_spec_kind */
	uint32_t	value;		/* RSC_SPEC_VALUE: the spec value */
	const char	*str;		/* RSC_SPEC_STRING: the string, may be NULL */
	const TEDINFO	*ted;		/* RSC_SPEC_TEDINFO, may be NULL */
	const ICONBLK	*icon;		/* RSC_SPEC_ICONBLK, may be NULL */
	const BITBLK	*bit;		/* RSC_SPEC_BITBLK and FRIMG, may be NULL */
	const struct rsc_cicon *cicon;	/* RSC_SPEC_CICON and ICON, may be NULL */

	/* FRSTR, FRIMG (index); FRSTR: text and alert */
	int		index;
	const char	*text;
	int		alert;		/* rcs_is_alert(text) */

	/* ICON: version (-1 mono), sel (this is a selected image), planes,
	 * has_sel (the version has a selected image), the size of the icon and
	 * the bytes of the data (all planes) and of the mask */
	int		version, sel, planes, has_sel, w, h;
	size_t		data_bytes, mask_bytes;
};

/* Fills *d for node.  Returns 0, or -1 for a node that does not exist. */
int rcs_describe(struct rcs_view *v, const struct rcs_node *node, struct rcs_desc *d);

/* Whether s has the shape of an alert: '[', a character, ']' (the test of
 * rsc_names_guess). */
int rcs_is_alert(const char *s);

/* Writes the at most max bytes of src (up to its NUL) into dst (dstsize
 * bytes with the NUL), escaped so that the result is printable ASCII:
 * backslash as \\, \n \r \t, other bytes below 0x20, 0x7f and every byte
 * from 0x80 up (the Atari character set; 0x80-0x9f are C1 controls of a
 * terminal) as \xnn with lower-case hex digits; with quote '"' also
 * the double quote as \" (and space is kept), with quote 0 the space as
 * \x20 too (for names in columns), with quote 1 nothing more (for cat).  When src does not fit, "..." ends the result.
 * Returns the length written. */
size_t rcs_escape(char *dst, size_t dstsize, const char *src, size_t max, int quote);

#endif
