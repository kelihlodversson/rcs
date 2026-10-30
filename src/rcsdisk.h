/* RCSDISK.H	the formats of the files, whatever the host looks like */
#ifndef RCSDISK_H

#define RCSDISK_H

#include <stdio.h>
#include <gem.h>

/* The records of a resource file are big-endian and have these sizes on disk.
 * In memory they are the records of the host: the same fields at the same
 * offsets, but the bytes of a field in the order of the host, and the
 * structures padded to what the host's compiler wants (an ICONBLK is 36
 * bytes on ARM, a BITBLK 16), so a table has a different stride in memory. */
#define DISK_RSHDR_SIZE		36
#define DISK_OBJECT_SIZE	24
#define DISK_TEDINFO_SIZE	28
#define DISK_ICONBLK_SIZE	34
#define DISK_BITBLK_SIZE	14

/* byte order of the bytes at p, independent of the host */
extern	unsigned short	rd_be16(const void *p);
extern	unsigned long	rd_be32(const void *p);
extern	void	wr_be16(void *p, unsigned short v);
extern	void	wr_be32(void *p, unsigned long v);
extern	unsigned short	rd_le16(const void *p);
extern	void	wr_le16(void *p, unsigned short v);

/* The image of a resource file, as read from disk, is at hdr.  Turn it into
 * what the rest of RCS works on: header, records and index tables in the byte
 * order of the host and with the alignment the host needs (a table that is
 * misaligned, or that has a larger stride in memory, is moved to the free
 * memory, rcs_free); the strings and the image data stay as they are.  The
 * object table is put at a distance of a multiple of an OBJECT from the one at
 * ref if it has to be moved.  The offset of the end of the image goes into the
 * header (RSH_RSSIZE).  Returns false when the file is not consistent or
 * there is not enough memory. */
extern	short	rsc_to_native(long hdr, long ref);

/* write the records of a resource file at their disk size and byte order */
extern	short	rsc_fwrite_header(FILE *f, long hdr);
extern	short	rsc_fwrite_object(FILE *f, long obj);
extern	short	rsc_fwrite_tedinfo(FILE *f, long ted);
extern	short	rsc_fwrite_iconblk(FILE *f, long ib);
extern	short	rsc_fwrite_bitblk(FILE *f, long bb);
extern	short	rsc_fwrite_longs(FILE *f, long first, short count);
/* image data: words of the host, written as big-endian words */
extern	short	rsc_fwrite_words(FILE *f, long first, long bytes);

#endif
