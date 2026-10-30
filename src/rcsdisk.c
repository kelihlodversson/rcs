/*	RCSDISK.C	the formats of the files, whatever the host looks like	*/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include "rcsdefs.h"
#include "rcsdata.h"
#include "rcsdisk.h"

unsigned short rd_be16(const void *p)
{
	const unsigned char *b = (const unsigned char *)p;

	return (unsigned short)((b[0] << 8) | b[1]);
}

unsigned long rd_be32(const void *p)
{
	const unsigned char *b = (const unsigned char *)p;

	return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) |
		((unsigned long)b[2] << 8) | b[3];
}

void wr_be16(void *p, unsigned short v)
{
	unsigned char *b = (unsigned char *)p;

	b[0] = (unsigned char)(v >> 8);
	b[1] = (unsigned char)v;
}

void wr_be32(void *p, unsigned long v)
{
	unsigned char *b = (unsigned char *)p;

	b[0] = (unsigned char)(v >> 24);
	b[1] = (unsigned char)(v >> 16);
	b[2] = (unsigned char)(v >> 8);
	b[3] = (unsigned char)v;
}

unsigned short rd_le16(const void *p)
{
	const unsigned char *b = (const unsigned char *)p;

	return (unsigned short)((b[1] << 8) | b[0]);
}

void wr_le16(void *p, unsigned short v)
{
	unsigned char *b = (unsigned char *)p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
}

/* ---- disk to memory ---------------------------------------------------- */

/* a 16 or 32 bit field at addr, big-endian to the host's order, in place */
static void fix16(long addr)
{
	*(unsigned short *)addr = rd_be16((void *)addr);
}

/* the longs of a record at the start, then words: as src (disk) to dst
 * (memory); the same place is fine */
static void fix_record(long dst, long src, short nlongs, short firstword, short nwords)
{
	short	i;
	unsigned long	l[8];
	unsigned short	w[16];

	for (i = 0; i < nlongs; i++)
		l[i] = rd_be32((void *)(src + 4 * i));
	for (i = 0; i < nwords; i++)
		w[i] = rd_be16((void *)(src + firstword + 2 * i));
	for (i = 0; i < nlongs; i++)
		*(unsigned long *)(dst + 4 * i) = l[i];
	for (i = 0; i < nwords; i++)
		*(unsigned short *)(dst + firstword + 2 * i) = w[i];
}

static void fix_object(long dst, long src)
{
	unsigned short	w[10];
	unsigned long	spec;
	short	i;

	for (i = 0; i < 6; i++)
		w[i] = rd_be16((void *)(src + 2 * i));
	spec = rd_be32((void *)(src + 12));
	for (i = 0; i < 4; i++)
		w[6 + i] = rd_be16((void *)(src + 16 + 2 * i));
	for (i = 0; i < 6; i++)
		*(unsigned short *)(dst + 2 * i) = w[i];
	*(unsigned long *)(dst + 12) = spec;
	for (i = 0; i < 4; i++)
		*(unsigned short *)(dst + 16 + 2 * i) = w[6 + i];
}

static void fix_tedinfo(long dst, long src)
{
	fix_record(dst, src, 3, 12, 8);
}

static void fix_iconblk(long dst, long src)
{
	fix_record(dst, src, 3, 12, 11);
}

static void fix_bitblk(long dst, long src)
{
	fix_record(dst, src, 1, 4, 5);
}

static void fix_longs(long dst, long src)
{
	*(unsigned long *)dst = rd_be32((void *)src);
}

/* room for bytes at a multiple of align; at a multiple of OBJECT from ref
 * when there is one */
static long place(long bytes, long align, long ref)
{
	long	p = rcs_free;

	for (;;)
	{
		if (p % align == 0 && (!ref || (p - ref) % DISK_OBJECT_SIZE == 0))
			break;
		p++;
	}
	if (p + bytes > head + buff_size)
		return 0;
	rcs_free = p + bytes;
	return p;
}

#define H(x)	(*(unsigned short *)(hdr + (x)))

/* convert (and move when needed) one table of count records; field is the
 * offset of its offset in the header */
static short fix_table(long hdr, short field, unsigned short count,
	short disksize, short memsize, short align, long ref,
	void (*fix)(long dst, long src))
{
	long	src, dst, bytes;
	unsigned short	i;

	if (!count)
		return true;
	src = hdr + H(field);
	if ((unsigned long)H(field) + (unsigned long)count * disksize > H(34))
		return false;
	if (memsize == disksize && src % align == 0 &&
		(!ref || (src - ref) % DISK_OBJECT_SIZE == 0))
	{
		for (i = 0; i < count; i++)
			fix(src + (long)i * disksize, src + (long)i * disksize);
		return true;
	}
	bytes = (long)count * memsize;
	dst = place(bytes, align, ref);
	if (!dst || dst - hdr > 0xffffL)
		return false;
	for (i = 0; i < count; i++)
		fix(dst + (long)i * memsize, src + (long)i * disksize);
	H(field) = (unsigned short)(dst - hdr);
	return true;
}

/* A reference to a record, an offset from the header, that was at old_tab
 * when the table was the one of the file, is now at new_tab with records of
 * memsize bytes. */
static unsigned long remap(unsigned long ref, unsigned short old_tab,
	unsigned short new_tab, unsigned short count, short disksize, short memsize)
{
	if (ref >= old_tab && ref < (unsigned long)old_tab + (unsigned long)count * disksize)
		return new_tab + (ref - old_tab) / disksize * memsize;
	return ref;
}

short rsc_to_native(long hdr, long ref)
{
	short	i;
	unsigned short	o_obj, o_ted, o_ib, o_bb;	/* where the tables were */
	unsigned short	n_obj, n_ted, n_ib, n_bb;	/* and where they are now */
	long	obj;

	for (i = 0; i < DISK_RSHDR_SIZE; i += 2)
		fix16(hdr + i);

	if (H(34) < DISK_RSHDR_SIZE)
		return false;

	o_obj = H(2);
	o_ted = H(4);
	o_ib = H(6);
	o_bb = H(8);

	if (!fix_table(hdr, 2, H(20), DISK_OBJECT_SIZE, sizeof(OBJECT),
			__alignof__(OBJECT), ref, fix_object))
		return false;
	if (!fix_table(hdr, 4, H(24), DISK_TEDINFO_SIZE, sizeof(TEDINFO),
			__alignof__(TEDINFO), 0, fix_tedinfo))
		return false;
	if (!fix_table(hdr, 6, H(26), DISK_ICONBLK_SIZE, sizeof(ICONBLK),
			__alignof__(ICONBLK), 0, fix_iconblk))
		return false;
	if (!fix_table(hdr, 8, H(28), DISK_BITBLK_SIZE, sizeof(BITBLK),
			__alignof__(BITBLK), 0, fix_bitblk))
		return false;
	if (!fix_table(hdr, 10, H(30), 4, sizeof(long), __alignof__(long), 0, fix_longs))
		return false;
	if (!fix_table(hdr, 16, H(32), 4, sizeof(long), __alignof__(long), 0, fix_longs))
		return false;
	if (!fix_table(hdr, 18, H(22), 4, sizeof(long), __alignof__(long), 0, fix_longs))
		return false;

	n_obj = H(2);
	n_ted = H(4);
	n_ib = H(6);
	n_bb = H(8);

	/* what points into a table that was moved, or has a different stride,
	   has to follow it: the roots of the trees into the objects, the specs
	   of the objects into the other records, the free images into the
	   BITBLKs */
	if (n_obj != o_obj)
		for (i = 0; i < (short)H(22); i++)
		{
			long	tr = hdr + H(18) + 4L * i;

			*(unsigned long *)tr = remap(*(unsigned long *)tr, o_obj, n_obj,
				H(20), DISK_OBJECT_SIZE, sizeof(OBJECT));
		}
	if (n_ted != o_ted || sizeof(TEDINFO) != DISK_TEDINFO_SIZE ||
		n_ib != o_ib || sizeof(ICONBLK) != DISK_ICONBLK_SIZE ||
		n_bb != o_bb || sizeof(BITBLK) != DISK_BITBLK_SIZE)
	{
		for (i = 0; i < (short)H(20); i++)
		{
			unsigned long	*spec;

			obj = hdr + H(2) + (long)i * sizeof(OBJECT);
			spec = (unsigned long *)(obj + 12);
			switch (*(unsigned short *)(obj + 6) & 0xff)
			{
				case G_TEXT:
				case G_BOXTEXT:
				case G_FTEXT:
				case G_FBOXTEXT:
					*spec = remap(*spec, o_ted, n_ted, H(24),
						DISK_TEDINFO_SIZE, sizeof(TEDINFO));
					break;
				case G_ICON:
					*spec = remap(*spec, o_ib, n_ib, H(26),
						DISK_ICONBLK_SIZE, sizeof(ICONBLK));
					break;
				case G_IMAGE:
					*spec = remap(*spec, o_bb, n_bb, H(28),
						DISK_BITBLK_SIZE, sizeof(BITBLK));
					break;
				default:
					break;
			}
		}
		for (i = 0; i < (short)H(32); i++)
		{
			long	fr = hdr + H(16) + 4L * i;

			*(unsigned long *)fr = remap(*(unsigned long *)fr, o_bb, n_bb,
				H(28), DISK_BITBLK_SIZE, sizeof(BITBLK));
		}
	}

	if (rcs_free & 1)
		rcs_free++;
	if (rcs_free - hdr > 0xffffL)
		return false;
	H(34) = (unsigned short)(rcs_free - hdr);
	return true;
}

/* ---- memory to disk ---------------------------------------------------- */

static short put(FILE *f, const unsigned char *b, short n)
{
	return fwrite(b, 1, n, f) == (size_t)n;
}

short rsc_fwrite_header(FILE *f, long hdr)
{
	unsigned char	b[DISK_RSHDR_SIZE];
	short	i;

	for (i = 0; i < DISK_RSHDR_SIZE; i += 2)
		wr_be16(b + i, *(unsigned short *)(hdr + i));
	return put(f, b, DISK_RSHDR_SIZE);
}

short rsc_fwrite_object(FILE *f, long obj)
{
	unsigned char	b[DISK_OBJECT_SIZE];
	short	i;

	for (i = 0; i < 6; i++)
		wr_be16(b + 2 * i, *(unsigned short *)(obj + 2 * i));
	wr_be32(b + 12, *(unsigned long *)(obj + 12));
	for (i = 0; i < 4; i++)
		wr_be16(b + 16 + 2 * i, *(unsigned short *)(obj + 16 + 2 * i));
	return put(f, b, DISK_OBJECT_SIZE);
}

static short fwrite_record(FILE *f, long rec, short size, short nlongs,
	short firstword, short nwords)
{
	unsigned char	b[DISK_ICONBLK_SIZE];	/* the largest record */
	short	i;

	for (i = 0; i < nlongs; i++)
		wr_be32(b + 4 * i, *(unsigned long *)(rec + 4 * i));
	for (i = 0; i < nwords; i++)
		wr_be16(b + firstword + 2 * i, *(unsigned short *)(rec + firstword + 2 * i));
	return put(f, b, size);
}

short rsc_fwrite_tedinfo(FILE *f, long ted)
{
	return fwrite_record(f, ted, DISK_TEDINFO_SIZE, 3, 12, 8);
}

short rsc_fwrite_iconblk(FILE *f, long ib)
{
	return fwrite_record(f, ib, DISK_ICONBLK_SIZE, 3, 12, 11);
}

short rsc_fwrite_bitblk(FILE *f, long bb)
{
	return fwrite_record(f, bb, DISK_BITBLK_SIZE, 1, 4, 5);
}

short rsc_fwrite_longs(FILE *f, long first, short count)
{
	unsigned char	b[4];
	short	i;

	for (i = 0; i < count; i++)
	{
		wr_be32(b, *(unsigned long *)(first + 4L * i));
		if (!put(f, b, 4))
			return false;
	}
	return true;
}
