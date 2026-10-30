/* RCSCICON.C	colour icons: kept through load and save */
#include <gem.h>
#include <stdlib.h>
#include <string.h>
#include "rcsdisk.h"
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcscicon.h"

#define MAXCICON	254

#define DISK_CICONBLK_SIZE	38	/* ICONBLK and the number of colour versions */
#define DISK_CICON_SIZE		22	/* the header of a colour version */

static struct cmark {
	long	iconblk;		/* the ICONBLK RCS made for it */
	struct cicon_kept	k;
} mark[MAXCICON];
static short	nmark;
static long	loaded;

/* the CICONBLKs of the file */
static struct cblk {
	unsigned char	*block;		/* what the file has, from the CICONBLK on */
	long	len;
	long	voff;			/* offset of the colour versions in it */
} cblk[MAXCICON];
static short	ncblk;

void cicon_reset(void)
{
	short	i;

	for (i = 0; i < ncblk; i++)
		free(cblk[i].block);
	for (i = 0; i < nmark; i++)
		free(mark[i].k.head);
	ncblk = nmark = 0;
	loaded = 0;
}

long cicon_loaded_bytes(void)
{
	return loaded;
}

/* Size of the CICONBLK at p in the file (the image has size bytes), or 0 if
 * it is not what it should be, as pTOS's loader would check it. */
static long cblk_len(const unsigned char *image, long size, unsigned long off, long *voff)
{
	long	words, pos, i, planes, payload;
	unsigned long	count;
	const unsigned char	*b;

	if (off & 1 || off + DISK_CICONBLK_SIZE > (unsigned long)size)
		return 0;
	b = image + off;
	if ((short)rd_be16(b + 22) < 0 || (short)rd_be16(b + 24) < 0)
		return 0;
	words = (((long)rd_be16(b + 22) + 15L) / 16L) * (long)rd_be16(b + 24);
	pos = off + DISK_CICONBLK_SIZE + words * 4L + 12L;
	count = rd_be32(b + 34);
	if (pos > size || count > 255UL)
		return 0;
	*voff = pos - off;
	for (i = 0; i < (long)count; i++)
	{
		if (pos + DISK_CICON_SIZE > size)
			return 0;
		planes = rd_be16(image + pos);
		if (planes <= 0 || planes > 16)
			return 0;
		payload = words * 2L * (planes + 1L);
		pos += DISK_CICON_SIZE + payload;
		if (rd_be32(image + pos - DISK_CICON_SIZE - payload + 10) != 0UL)
			pos += payload;		/* the selected version */
		if (pos > size)
			return 0;
	}
	return pos - off;
}

void cicon_scan(long image, long size, unsigned long table_off)
{
	const unsigned char	*im = (const unsigned char *)image;
	unsigned long	off;
	long	len, voff;

	cicon_reset();
	if (table_off == 0UL || table_off == 0xffffffffUL || (table_off & 1) || table_off + 4 > (unsigned long)size)
		return;
	for (; table_off + 4 <= (unsigned long)size && ncblk < MAXCICON; table_off += 4)
	{
		off = rd_be32(im + table_off);
		if (off == 0xffffffffUL)
			return;
		len = cblk_len(im, size, off, &voff);
		if (!len || !(cblk[ncblk].block = malloc(len)))
			return;
		memcpy(cblk[ncblk].block, im + off, len);
		cblk[ncblk].len = len;
		cblk[ncblk].voff = voff;
		loaded += len + 4;
		ncblk++;
	}
}

long cicon_iconblk(unsigned long index)
{
	struct cblk	*c;
	struct cmark	*m;
	ICONBLK	*ib;
	unsigned char	*b;
	short	w, h, i;
	long	words, data, maskp, text, ibaddr;

	if (index >= (unsigned long)ncblk || nmark >= MAXCICON)
		return 0;
	c = &cblk[index];
	b = c->block;
	w = (short)rd_be16(b + 22);
	h = (short)rd_be16(b + 24);
	words = (((long)w + 15L) / 16L) * h;
	if (words * 2L > 0xffffL)
		return 0;
	ibaddr = get_mem((unsigned short)sizeof(ICONBLK));
	data = get_mem((unsigned short)(words * 2L));
	maskp = get_mem((unsigned short)(words * 2L));
	text = get_mem(13);
	if (!ibaddr || !data || !maskp || !text)
		return 0;
	for (i = 0; i < words; i++)
	{
		((unsigned short *)maskp)[i] = rd_be16(b + DISK_CICONBLK_SIZE + 2 * words + 2 * i);
		((unsigned short *)data)[i] = rd_be16(b + DISK_CICONBLK_SIZE + 2 * i);
	}
	memcpy((void *)text, b + DISK_CICONBLK_SIZE + 4 * words, 12);
	((char *)text)[12] = '\0';
	ib = (ICONBLK *)ibaddr;
	ib->ib_pmask = (short *)maskp;
	ib->ib_pdata = (short *)data;
	ib->ib_ptext = (char *)text;
	ib->ib_char = rd_be16(b + 12);
	ib->ib_xchar = rd_be16(b + 14);
	ib->ib_ychar = rd_be16(b + 16);
	ib->ib_xicon = rd_be16(b + 18);
	ib->ib_yicon = rd_be16(b + 20);
	ib->ib_wicon = w;
	ib->ib_hicon = h;
	ib->ib_xtext = rd_be16(b + 26);
	ib->ib_ytext = rd_be16(b + 28);
	ib->ib_wtext = rd_be16(b + 30);
	ib->ib_htext = rd_be16(b + 32);

	m = &mark[nmark++];
	m->k.head = 0;
	m->iconblk = ibaddr;
	/* only icons as wide as whole words are kept: the layout of the file depends on it */
	if (!(w & 15) && (m->k.head = malloc(DISK_CICONBLK_SIZE)) != 0)
		memcpy(m->k.head, b, DISK_CICONBLK_SIZE);
	m->k.versions = b + c->voff;
	m->k.vlen = c->len - c->voff;
	m->k.wicon = w;
	m->k.hicon = h;
	m->k.words = (short)words;
	m->k.ncolour = (short)rd_be32(b + 34);
	m->k.used = 0;
	return ibaddr;
}

struct cicon_kept *cicon_kept(long iconblk)
{
	short	i;
	ICONBLK	*ib = (ICONBLK *)iconblk;

	for (i = 0; i < nmark; i++)
		if (mark[i].iconblk == iconblk)
			return (ib->ib_wicon == mark[i].k.wicon && ib->ib_hicon == mark[i].k.hicon
				&& mark[i].k.head) ? &mark[i].k : 0;
	return 0;
}

/* The icons that fit in budget bytes are the ones that are written as
 * colour icons (the size of a file is kept in 16 bits). */
void cicon_prepare(long budget)
{
	short	i;
	long	need;

	for (i = 0; i < nmark; i++)
	{
		mark[i].k.used = 0;
		need = mark[i].k.vlen + 4L * mark[i].k.words + DISK_CICONBLK_SIZE + 12 + 4;
		if (mark[i].k.head && budget >= need)
		{
			budget -= need;
			mark[i].k.used = 1;
		}
	}
}
