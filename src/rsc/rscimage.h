/* RSCIMAGE.H	icon and bitmap images as pictures: PBM/PGM/PPM files, the
 *		standard VDI palette, planar words and chunky pixels, depth
 *		conversion, and the images of the objects of a model
 *
 * Pure C99, no I/O: the file functions work on byte buffers, the planar
 * ones on the word arrays of the model.  Every buffer the library returns
 * is allocated with rsc_malloc and freed with rsc_mfree (or the free
 * function named below).  A call that fails returns a negative
 * enum rsc_img_error and leaves what it was given as it was; a failed
 * output argument is cleared.
 *
 * Pixels.  A struct rsc_pixmap holds an image as one byte per pixel
 * (chunky): pix[y * w + x] is the pixel value, 0..(1 << planes) - 1, the
 * value the planes of a CICON build bit by bit (bit p of the value is bit
 * 15 - (x % 16) of word (p * words_per_row * h) + y * words_per_row + x /
 * 16, words_per_row = (w + 15) / 16; plane 0 is the least significant
 * bit).  For one plane a set bit (1) is the black pixel, as the AES draws
 * icons.  Mono images, masks and the BITBLK of G_IMAGE objects are one
 * plane.  The words are in the byte order of the machine, as everywhere in
 * the model; PNM files are bytes, so no byte order shows.
 *
 * The standard palette.  The value of a pixel is an index into the VDI
 * palette of its depth, through the pen-to-pixel mapping the AES uses (the
 * MAP_COL table of the VDI): see docs/resource-model.md, "Images".
 * Supported depths for colour data are 2, 4 and 8 planes
 * (rsc_depth_supported); 1 plane is the black and white palette of mono
 * images.  Other depths (3, 5 to 7, 9 to 16) are kept by the model but have
 * no palette: the calls that need one return RSC_IMG_ENODEPTH.
 */
#ifndef RSCIMAGE_H
#define RSCIMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "rscmodel.h"

enum rsc_img_error
{
	RSC_IMG_OK = 0,
	RSC_IMG_EARG = -1,	/* a bad argument (NULL, a bad index or size, a
				 * mismatch of planes) */
	RSC_IMG_ENOMEM = -2,	/* an allocation failed */
	RSC_IMG_EFORMAT = -3,	/* not a PNM file, or a malformed one */
	RSC_IMG_ETRUNC = -4,	/* the PNM file ends before its data */
	RSC_IMG_ETOOBIG = -5,	/* a dimension above RSC_PNM_MAX_DIM, or more
				 * pixels than the budget allows */
	RSC_IMG_EMAXVAL = -6,	/* maxval above 255 (16-bit samples) */
	RSC_IMG_ENODEPTH = -7,	/* the depth has no palette: not 2, 4 or 8 planes */
	RSC_IMG_ESIZE = -8,	/* the sizes of two images differ */
	RSC_IMG_ENOIMAGE = -9,	/* the object has no such image (wrong type, no
				 * record, no such version, no selected image) */
	RSC_IMG_ENOMASK = -10,	/* a G_IMAGE or a free image has no mask */
	RSC_IMG_EEXIST = -11,	/* a version with that many planes exists already */
	RSC_IMG_ELIMIT = -12,	/* RSC_MAX_VERSIONS versions already */
	RSC_IMG_ENOTPAL = -13	/* exact import: a colour is not in the palette */
};

/* ---- pixmaps ---------------------------------------------------------- */

/* The most pixels a pixmap or a decoded PNM file may have (16 Mi). */
#define RSC_PIXMAP_MAX_PIXELS	(1UL << 24)

struct rsc_pixmap
{
	int		w, h;		/* 0..32767 */
	int		planes;		/* 1..8: the depth the values belong to */
	unsigned char	*pix;		/* w * h bytes, row by row; NULL when empty */
};

/* A zeroed pixmap of w x h pixels (0..32767 each, at most
 * RSC_PIXMAP_MAX_PIXELS pixels) and 1..8 planes.  On failure *pm is
 * cleared (pix NULL, sizes 0). */
int rsc_pixmap_new(struct rsc_pixmap *pm, int w, int h, int planes);
/* Frees pm->pix and clears *pm; NULL and cleared pixmaps are allowed. */
void rsc_pixmap_free(struct rsc_pixmap *pm);
/* *out := in scaled to w x h by the nearest neighbour (the pixel at
 * (x * in.w / w, y * in.h / h)); lossy.  An empty source needs an empty
 * target. */
int rsc_pixmap_scale(const struct rsc_pixmap *in, int w, int h, struct rsc_pixmap *out);

/* ---- the standard palette -------------------------------------------------- */

/* Nonzero for the depths with a palette for colour data: 2, 4 and 8. */
int rsc_depth_supported(int planes);

/* The colour of pixel value `pixel` at 1, 2, 4 or 8 planes as 8-bit r, g,
 * b (0..255; any may be NULL).  Returns 0, RSC_IMG_ENODEPTH for other
 * depths, RSC_IMG_EARG for a pixel outside 0..(1 << planes) - 1. */
int rsc_palette_rgb(int planes, unsigned pixel, int *r, int *g, int *b);

/* The pixel value of the palette entry nearest to r, g, b (0..255; the
 * sum of the squares of the differences; the lowest pixel value among
 * equals), or a negative error (RSC_IMG_ENODEPTH, RSC_IMG_EARG for a
 * colour outside 0..255). */
int rsc_palette_nearest(int planes, int r, int g, int b);

/* ---- planar words and pixels --------------------------------------------- */

/* The pixel (x, y) of an image of planes planes (1..8) and w x h pixels in
 * the planar word layout above; NULL data is a blank image.  0 for a
 * position outside the image or a bad depth. */
unsigned rsc_plane_get(const short *data, int planes, int w, int h, int x, int y);
/* Sets the pixel (x, y), the value masked to the planes.  Returns 0, or -1
 * for a position outside the image, a NULL data or a bad depth. */
int rsc_plane_put(short *data, int planes, int w, int h, int x, int y, unsigned pixel);

/* The whole image as a pixmap (NULL data: zeros) and back.  The bits of a
 * word beyond the width are ignored by the first and written as zero by the
 * second, which allocates the block (planes * rsc_plane_bytes(w, h) bytes)
 * in *out for rsc_mfree. */
int rsc_planes_to_pixmap(const short *data, int planes, int w, int h, struct rsc_pixmap *pm);
int rsc_pixmap_to_planes(const struct rsc_pixmap *pm, short **out);

/* Converts the pixels of `in` to `planes` planes into *out (the depth of
 * `in` is in->planes):
 *  - the same depth: a copy;
 *  - from 1 plane: a set pixel becomes the black of the palette (pen 1, the
 *    value (1 << planes) - 1), a clear pixel 0 (white); any depth 1..8;
 *  - otherwise through the palette: every pixel becomes the nearest colour
 *    of the new depth (in->planes must be 2, 4 or 8, planes 1, 2, 4 or
 *    8, else RSC_IMG_ENODEPTH).
 * *inexact (may be NULL) is the number of pixels whose colour is not the
 * same afterwards (0 for a copy and for mono to colour). */
int rsc_pixmap_convert(const struct rsc_pixmap *in, int planes, struct rsc_pixmap *out,
		       long *inexact);

/* ---- PNM files ---------------------------------------------------------------- */

/* The largest width or height of a PNM file (the model's image sizes). */
#define RSC_PNM_MAX_DIM		32767
/* The default budget of pixels of rsc_image_from_pnm. */
#define RSC_PNM_DEFAULT_MAX_PIXELS	(1UL << 22)

/* A decoded PNM file: format is the digit of the magic (1 to 6),
 * maxval 1 for the bitmaps (1 is black there, as in the file) and 1..255
 * for the others, channels 3 for P3 and P6, else 1; samples has w * h *
 * channels samples, each 0..maxval. */
struct rsc_pnm
{
	int		format;
	int		w, h;
	int		maxval;
	int		channels;
	unsigned char	*samples;
};

/* Writes the picture as PBM (a pixmap of one plane; set pixel = 1 = black)
 * or PPM (every pixel through the palette of pm->planes: 1, 2, 4 or 8) in
 * *out, *len bytes, for rsc_mfree.  ascii 0: the binary forms P4 and P6;
 * ascii 1: P1 and P3 (text; lines of at most 70 characters, a row starting
 * a line).  The header is "P4\nW H\n" ("P6\nW H\n255\n").  An empty image
 * (w or h 0) is RSC_IMG_EARG. */
int rsc_image_to_pbm(const struct rsc_pixmap *pm, int ascii, unsigned char **out, size_t *len);
int rsc_image_to_ppm(const struct rsc_pixmap *pm, int ascii, unsigned char **out, size_t *len);

/* Decodes a PNM file (P1 to P6) from buf[0..len): whitespace and `#`
 * comments between the header fields (in the text forms also between
 * samples), exactly one whitespace byte before binary data.  Hostile
 * input is safe: the dimensions (1 to RSC_PNM_MAX_DIM, else RSC_IMG_ETOOBIG
 * or RSC_IMG_EFORMAT for 0) are checked against max_pixels (0:
 * RSC_PNM_DEFAULT_MAX_PIXELS; at most RSC_PIXMAP_MAX_PIXELS) and against
 * the bytes the file really has before anything is allocated, numbers of
 * any length are read in constant memory, maxval 0 is RSC_IMG_EFORMAT and
 * above 255 RSC_IMG_EMAXVAL, a sample above maxval RSC_IMG_EFORMAT, a
 * file that ends early RSC_IMG_ETRUNC.  Bytes after the data are ignored.
 * *out is cleared on failure. */
int rsc_image_from_pnm(const unsigned char *buf, size_t len, size_t max_pixels, struct rsc_pnm *out);
/* Frees p->samples and clears *p; NULL allowed. */
void rsc_pnm_free(struct rsc_pnm *p);

/* The colour of pixel (x, y) of a decoded file as 8-bit r, g, b (samples
 * scaled from maxval; bitmaps: 1 black, 0 white).  Returns 0, or
 * RSC_IMG_EARG for a position outside the picture. */
int rsc_pnm_rgb(const struct rsc_pnm *p, int x, int y, int *r, int *g, int *b);

/* The picture as a pixmap of `planes` planes (1, 2, 4 or 8).
 *  - planes 1 (a mono image or a mask): a pixel is set when it is at least
 *    threshold percent dark (1..100, 0 means 50; the darkness is 255 minus
 *    the luminance (299 R + 587 G + 114 B) / 1000);
 *  - planes 2, 4, 8: the nearest palette colour; with exact nonzero a
 *    colour that is not in the palette is RSC_IMG_ENOTPAL and *bad (may
 *    be NULL) is the index y * w + x of the first one.
 * Other depths are RSC_IMG_ENODEPTH. */
int rsc_pnm_to_pixmap(const struct rsc_pnm *p, int planes, int threshold, int exact,
		      struct rsc_pixmap *out, long *bad);

/* ---- the images of a model --------------------------------------------------- */

/* Selects one image of a model.
 *
 *  tree >= 0   object `obj` of that tree: G_ICON (the icon, version -1),
 *              G_IMAGE (version -1, no mask), G_CICON (version -1: the
 *              mono icon; version >= 0: colour version number `version`,
 *              as rsc_cicon_version numbers them; sel 1: its selected
 *              image);
 *  tree == RSC_IMG_FREE   free image number `obj` (a BITBLK, no mask).
 *
 * mask 1 selects the mask of that image instead of its data (one plane).
 * The data of a colour version has the planes of the version, which must be
 * 2, 4 or 8 (the mask is always one plane, whatever the depth).
 *
 * As a destination of rsc_image_set and rsc_image_copy only:
 *  - version RSC_VER_NEW: a new colour version with `planes` planes (2, 4
 *    or 8) is created: its data is blank (rsc_image_set of the data: the
 *    given pixels; of the mask: the data is blank) and its mask a copy of the
 *    mono mask unless the mask is what is set;
 *  - sel 1 on a version without selected images: the selected images are
 *    created, the one not set a copy of the normal one. */
#define RSC_IMG_FREE	(-1)
#define RSC_VER_MONO	(-1)
#define RSC_VER_NEW	(-2)

struct rsc_imgsel
{
	int	tree;
	int	obj;
	int	version;
	int	sel;
	int	mask;
	int	planes;		/* RSC_VER_NEW only */
};

/* What an existing image is: w x h pixels, planes planes (1 for masks
 * and mono images; the version's for colour data, whatever the depth),
 * blank nonzero when its data pointer is NULL (all zero).  Returns 0, or
 * RSC_IMG_EARG (bad indices), RSC_IMG_ENOIMAGE, RSC_IMG_ENOMASK. */
struct rsc_imginfo
{
	int	w, h, planes, blank;
};
int rsc_image_info(const struct rsc *r, const struct rsc_imgsel *s, struct rsc_imginfo *info);

/* The image as a pixmap (the caller frees it with rsc_pixmap_free).  The
 * colour data of a depth that is not 2, 4 or 8 is RSC_IMG_ENODEPTH. */
int rsc_image_get(const struct rsc *r, const struct rsc_imgsel *s, struct rsc_pixmap *out);

/* Replaces the image with the pixmap, which must have the size of the icon
 * (RSC_IMG_ESIZE) and the planes of the image (RSC_IMG_EARG).  Nothing else
 * changes (the size fields stay, and the opaque file fields of the colour
 * version).  See the destinations above. */
int rsc_image_set(struct rsc *r, const struct rsc_imgsel *s, const struct rsc_pixmap *in);

/* Copies the image `from` into `to`, converting the depth (rsc_pixmap_convert;
 * *inexact as there, may be NULL): mono to colour, colour to colour by
 * the nearest colour, colour to mono.  `from` and `to` may be the same
 * model object, but not the same image.  With flags RSC_COPY_MASK the mask
 * of `from` is copied to the mask of `to` as well (when both have one, and the
 * mask of `from` has data, i.e. is not a blank NULL mask: that leaves the mask of `to` as it is; the
 * `mask` field of the selectors is then 0 and is ignored).  The sizes must
 * be equal (RSC_IMG_ESIZE).  All or nothing. */
#define RSC_COPY_MASK	1
int rsc_image_copy(struct rsc *r, const struct rsc_imgsel *from, const struct rsc_imgsel *to,
		   int flags, long *inexact);

/* ---- the versions of a colour icon -------------------------------------------- */

/* The index of the version with `planes` planes, or -1. */
int rsc_cicon_find_version(const struct rsc_cicon *c, int planes);
/* Appends a blank version (data pixel 0; the mask a copy of the mono mask)
 * with 2, 4 or 8 planes and returns its index.  RSC_IMG_EEXIST when the icon
 * has the depth already, RSC_IMG_ELIMIT at RSC_MAX_VERSIONS, RSC_IMG_ENODEPTH,
 * RSC_IMG_ENOMEM. */
int rsc_cicon_new_version(struct rsc_cicon *c, int planes);
/* Gives version v selected images: copies of its data and mask.  0, or
 * RSC_IMG_EEXIST when it has them, RSC_IMG_ENOIMAGE for a bad v. */
int rsc_cicon_add_selected(struct rsc_cicon *c, int v);
/* Removes the selected images of version v.  0, or RSC_IMG_ENOIMAGE for a
 * bad v or a version without them. */
int rsc_cicon_drop_selected(struct rsc_cicon *c, int v);

#endif
