/* RSCATARI.H	private to rscatari_read.c and rscatari_write.c: the record
 *		sizes and header layout of the classic Atari format (see the
 *		top of rscatari_read.c) and the filling of an error report
 *		(which the names readers of rscnames.c use too) */
#ifndef RSCATARI_H
#define RSCATARI_H

#include <string.h>
#include "rscfile.h"

#define FILE_NULL	0xffffffffUL	/* a NULL pointer in the file */

#define HDR_SIZE	36
#define OBJ_SIZE	24
#define TED_SIZE	28
#define IB_SIZE		34
#define BB_SIZE		14
#define CBLK_SIZE	38	/* the ICONBLK and the count of colour versions */
#define CTEXT_SIZE	12
#define CICON_SIZE	22	/* the header of a colour version */
#define EXT_SIZE	12	/* the extension at rsh_rssize */

/* positions of the header words */
enum
{
	H_VRSN, H_OBJECT, H_TEDINFO, H_ICONBLK, H_BITBLK, H_FRSTR, H_STRING,
	H_IMDATA, H_FRIMG, H_TRINDEX, H_NOBS, H_NTREE, H_NTED, H_NIB, H_NBB,
	H_NSTRING, H_NIMAGES, H_RSSIZE, H_WORDS
};

/* Appends s to the message m of *n characters, as much as fits. */
static inline void rsc_msg_app(char *m, size_t *n, const char *s)
{
	while (*s && *n < sizeof ((struct rsc_error *)0)->msg - 1)
		m[(*n)++] = *s++;
	m[*n] = '\0';
}

/* Appends v in decimal, or in lower case hex when hex is set. */
static inline void rsc_msg_num(char *m, size_t *n, size_t v, int hex)
{
	static const char	dig[] = "0123456789abcdef";
	char	num[3 * sizeof(size_t) + 1];
	size_t	k = sizeof num - 1, b = hex ? 16 : 10;

	num[k] = '\0';
	do
	{
		num[--k] = dig[v % b];
		v /= b;
	} while (v && k > 0);
	rsc_msg_app(m, n, num + k);
}

/* Fills *e (when not NULL) with code, off and "what at offset 0x...". */
static inline void rsc_error_set(struct rsc_error *e, int code, size_t off, const char *what)
{
	size_t	n = 0;

	if (!e)
		return;
	e->code = code;
	e->offset = off;
	rsc_msg_app(e->msg, &n, what);
	rsc_msg_app(e->msg, &n, " at offset 0x");
	rsc_msg_num(e->msg, &n, off, 1);
}

#endif
