/* RSCFILE.H	what the resource file readers and writers share: the format
 *		selector and the error report */
#ifndef RSCFILE_H
#define RSCFILE_H

#include <stddef.h>
#include <stdint.h>
#include "rscmodel.h"

/* The on-disk formats.  RSC_FORMAT_RSX (RSXHDR) is added in phase 3 of #29. */
enum rsc_format
{
	RSC_FORMAT_CLASSIC		/* Atari big-endian RSHDR, with the colour icon extension */
};

/* Error codes of struct rsc_error.  0 is success. */
enum rsc_errcode
{
	RSC_OK = 0,
	RSC_ENOMEM,		/* an allocation failed */
	RSC_EARG,		/* a bad argument (NULL buffer, bad cell size, ...) */
	RSC_ETRUNC,		/* the file ends before the data it describes */
	RSC_EHEADER,		/* the header is inconsistent */
	RSC_EOFFSET,		/* an offset or count points outside the file */
	RSC_ESTRING,		/* a string has no NUL inside the file */
	RSC_ELINK,		/* an object link is out of range */
	RSC_ECYCLE,		/* the links of a tree form a cycle */
	RSC_ECICON,		/* a colour icon block or its table is bad */
	RSC_ETOOBIG		/* the data does not fit the format (16-bit offsets, counts),
				 * or, reading, the decoded model would exceed the budget
				 * of 4 * len + 65536 bytes (allocations charged at their
				 * size plus a small cost each; see rscatari_read.c) */
};

/* What went wrong and where: offset is the position in the file (reader)
 * or in the output (writer) the error refers to; msg is a NUL-terminated
 * text for the user. */
struct rsc_error
{
	int	code;		/* enum rsc_errcode */
	size_t	offset;
	char	msg[96];
};

/* The classic Atari format (rscatari_read.c, rscatari_write.c).
 *
 * rsc_atari_read decodes len bytes at buf into a new model, or returns NULL
 * and fills *err.  cell_w and cell_h are the character cell size the object
 * coordinates are converted with (as rsrc_obfix does); 0, 0 keeps them in
 * file form.  Nothing of buf is referenced by the result.
 *
 * rsc_atari_write encodes r into a buffer (*out, *len; allocated with
 * rsc_malloc, the caller frees it with rsc_mfree) and returns
 * 0, or returns -1 and fills *err; it does not modify r.  cell_w and cell_h
 * are the cell size pixel coordinates are converted back to file form with
 * (as unfix_chpos does); see rsc_cell for what r holds. */
struct rsc *rsc_atari_read(const uint8_t *buf, size_t len, int cell_w, int cell_h,
			   struct rsc_error *err);
int rsc_atari_write(const struct rsc *r, int cell_w, int cell_h,
		    uint8_t **out, size_t *len, struct rsc_error *err);

#endif
