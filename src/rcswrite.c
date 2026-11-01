 /*	RCSWRITE.C	1/28/85 - 1/28/85	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include "rcsdisk.h"
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <mint/osbind.h>
#include <stdio.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcslib.h"
#include "rcsdata.h"
#include "rcsintf.h"
#include "rcsalert.h"
#include "rcsfiles.h"
#include "rcsvdi.h"
#include "rcswrite.h"
#include "rcscicon.h"
#include "rcsaddr.h"

#ifdef RCS_HOST
#include <stdlib.h>
/* gsx_untrans() and rsc_fwrite_words() take an RCS address.  On m68k and ARM
 * the scratch area for the standard form of an image is the array wr_obndx[];
 * on the host that array is not in the arena, so the scratch area is a buffer
 * of the arena, as large as the largest image asked for (a file has at most
 * 64 KiB, so it is one allocation in practice). */
static raddr wr_scratch;
static unsigned long wr_scratch_size;

static raddr wr_scratch_buf(unsigned long bytes)
{
	if (bytes > wr_scratch_size)
	{
		wr_scratch_size = bytes < 0x10000UL ? 0x10000UL : bytes;
		wr_scratch = rcs_alloc(wr_scratch_size);
		if (!wr_scratch)
		{
			fprintf(stderr, "rcswrite: out of memory for the image buffer\n");
			exit(1);
		}
	}
	return wr_scratch;
}
#define WR_OBNDX(bytes)	wr_scratch_buf((unsigned long)(bytes))
/* the 4 byte fields of a record, as a long (an address or -1) */
#define HPTR(t)		(rentry)
#define HV(x)		((long)(int)(x))
#define HSPEC(x)	((unsigned long)(x) & 0xffffffffUL)
#else
#define WR_OBNDX(bytes)	((long)((long)(wr_obndx)))
#define HPTR(t)		(t *)
#define HV(x)		((long)(x))
#define HSPEC(x)	(x)
#endif

typedef	struct	outobj
{
	short	next;
	short	head;
	short	tail;
	char	*type;
	char	*flags;
	char	*state;
	long	spec;
	unsigned short	x;
	unsigned short	y;
	unsigned short	w;
	unsigned short	h;
} OUTOBJ;

typedef struct 	h_out
{
	char	*p0;
	short	p1;
	short	p2;
} HOUT;

static void do_tally(void)
{
	clr_tally();
	map_all((fkt_parm)tally_obj);
	tally_free();
	rcs_tally.nbytes += (*(unsigned short *)RP(RSH_NTREE(head))) * RS_ENTRY;
	rcs_tally.nbytes += sizeof(RSHDR);
}

static long get_dspace(short drv)
{
	long	dsk_avail;
	_DISKINFO dsk_free_buf;

#ifdef RCS_HOST
	(void)drv;
	(void)dsk_avail;
	(void)dsk_free_buf;
	return 0x7fffffffL;		/* the host disk always has room */
#else
	Dfree(&dsk_free_buf, drv);
	dsk_avail = dsk_free_buf.b_free *
		dsk_free_buf.b_secsiz *
		dsk_free_buf.b_clsiz;
	return ( dsk_avail );
#endif
}

static short dspace(short drv, unsigned short nbytes)
{
	long	dsk_avail;
	_DISKINFO dsk_free_buf;

#ifdef RCS_HOST
	(void)drv;
	(void)nbytes;
	(void)dsk_avail;
	(void)dsk_free_buf;
	return (false);			/* the host disk always has room */
#else
	Dfree(&dsk_free_buf, drv);
	dsk_avail = dsk_free_buf.b_free *
		dsk_free_buf.b_secsiz *
		dsk_free_buf.b_clsiz;
	if ( nbytes < (unsigned short) dsk_avail )
		return (false);
	else
		return (true);
#endif
}

/* the "*.def" files keep the structure of the IBM PC: little-endian */

static void wr_68kdef(void)
{
	unsigned short	idx;
	unsigned char	b[2];
	
	wr_le16(b, rcs_ndxno);
	fwrite( b, 1, 2, rcs_dhndl );
	for ( idx = 0; idx < rcs_ndxno; idx++ )
	{
		wr_le16(b, (unsigned short) (long) rcs_index[idx].val );
		fwrite( b, 1, 2, rcs_dhndl );
		wr_le16(b, rcs_index[idx].kind );
		fwrite( b, 1, 2, rcs_dhndl );
		fwrite( &rcs_index[idx].name[0], 1, 10, rcs_dhndl );
	}
}

static short c_imgno(long addr)
{
	short	imgno;

	if (addr == NIL)
		return (NIL);
	else
	{
		for (imgno = 0; imgno < c_nimage; imgno++)
			if ( (short) addr == c_obndx[imgno + c_nstring])
				return (imgno);
		return (NIL);
  	}
}

static short c_strno(long addr)
{
	short	strno;

	if (addr == NIL)
		return (NIL);
	else
	{
		for (strno = 0; strno < c_nstring; strno++)
			if ( (short) addr == c_obndx[strno])
				return (strno);
		return (NIL);
	}
}

static void c_wrlin(void)
{
	fputs( hline, rcs_hhndl );
}

static void c_comma(short yesno)
{
	if (yesno)
	{
		strcpy(hline, ",\n");
		c_wrlin();
	}
}

static void c_tail(short used)
{
	if (rcs_cflag)
	{
		if (!used)
			strcpy(hline, "0};\n");
		else
			strcpy(hline, "};\n");
		c_wrlin();
	}
}

static void wr_sync(void)
{
	if (rcs_wraddr & 0x1)
	{
		fwrite("", 1, 1, rcs_rhndl);
		rcs_wraddr++;
	}
}

static void wr_header(void)
{
	fseek(rcs_rhndl, 0, SEEK_SET);
	rsc_fwrite_header(rcs_rhndl, head);
}

static void c_defline(char *name, short val)
{
	sprintf(hline, "#define %s %d\n", name, val);
	c_wrlin();
}

static void c_baseline(short num, char *field, short val)
{
	char	name[9];

	sprintf(name, "T%d%s", num, field);
	c_defline(name, val);
}

static void c_bases(void)
{
	short	i;

	if (rcs_cflag)
	{
		clr_tally();
		for (i = 0; i < (*(unsigned short *)RP(RSH_NTREE(head))); i++)
		{
			c_baseline(i, "OBJ", rcs_tally.nobj);
/*			c_baseline(i, "IB", rcs_tally.nib);	*/
/*			c_baseline(i, "BB", rcs_tally.nbb);	*/
/*			c_baseline(i, "TI", rcs_tally.nti);	*/
/*			c_baseline(i, "IMG", rcs_tally.nimg);	*/
/*			c_baseline(i, "STR", rcs_tally.nstr);	*/
			map_tree( tree_addr(i), ROOT, NIL, (fkt_parm)tally_obj);
		}
		c_defline("FREEBB", rcs_tally.nbb);
		c_defline("FREEIMG", rcs_tally.nimg);
		c_defline("FREESTR", rcs_tally.nstr);
	}
}

static void c_strhead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nBYTE *rs_strings[] = {\n");
		c_wrlin();
		c_nstring = 0;
	}
}

static void c_string(long addr)
{
	short	hsub;
	char	hchar;

	if (rcs_cflag)
	{
		c_comma(c_nstring);

		hline[0] = '"';
		hsub = 1;

		do {
			hchar = (*(char *)RP(addr++));
			if (hchar == '"')
				hline[hsub++] = '\\';
			else if (hchar == '\\')
				hline[hsub++] = '\\';
			if (!hchar)
				hline[hsub++] = '"';
			hline[hsub++] = hchar;
			if (hchar && hsub == 70)
			{
				hline[hsub] = '\0';
				strcat(hline, "\\\n");
				c_wrlin();
				hsub = 0;
			}
		} while (hchar);

		c_wrlin();
		c_obndx[c_nstring++] = rcs_wraddr;
	}
}

/* The colour icons that were read: RCS has their mono icons as G_ICONs, and
 * keeps their colour versions until now.  They are written as G_CICONs after
 * the rest, in the order in which their objects are written. */
#define MAXCICON	254
static struct cicon_kept	*wr_cic[MAXCICON];
static long	wr_cic_ib[MAXCICON];
static short	wr_ncic;
static unsigned long	wr_cic_table, wr_cic_total;
static unsigned short	wr_cic_rssize;

static struct cicon_kept *wr_kept(long tree, short which)
{
	struct cicon_kept	*k;

	if (((GET_TYPE(tree, which)) & 0xff) != G_ICON || GET_SPEC(tree, which) == -1L)
		return 0;
	k = cicon_kept(GET_SPEC(tree, which));
	return (k && k->used) ? k : 0;
}

static void wr_str(long where)
{
	long	staddr;
	short	stlen;

	staddr = RGET(where);
	if ( staddr != -1L)
	{
		c_string(staddr);
		(RFIELD(where) = ((long) rcs_wraddr & 0xffff));
		stlen = 1 + (short)strlen((char *)RP(staddr));
		fwrite(RP(staddr), 1, stlen, rcs_rhndl);
		rcs_wraddr += stlen;
	}
}

static void wr_string(long tree, short which)
{
	long	iconblk, tedinfo;
	short	type;

	type = ((GET_TYPE(tree, which)) & 0xff);
	if (type == G_BUTTON || type == G_STRING || type == G_TITLE)
		wr_str(OB_SPEC(which));
	else if (type == G_ICON)
	{
		if ( (iconblk = GET_SPEC(tree, which)) != -1L && !wr_kept(tree, which))
			wr_str(IB_PTEXT(iconblk));
	}
	else if (type == G_TEXT || type == G_BOXTEXT ||
		type == G_FTEXT || type == G_FBOXTEXT) 
	{
		tedinfo = GET_SPEC(tree, which);
		if (tedinfo != -1L)
		{
			wr_str(tedinfo);
			wr_str(TE_PTMPLT(tedinfo));
			wr_str(TE_PVALID(tedinfo));
		}
	}
}

static void map_frstr(void)
{
	short	ifree, nfree;

	c_frstr = c_nstring;
	nfree = (*(unsigned short *)RP(RSH_NSTRING(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
			wr_str(str_ptr(ifree));
	}
}

static void c_frshead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nLONG rs_frstr[] = {\n");
		c_wrlin();
		c_nfrstr = 0;
	}
}

static void c_freestr(short which)
{
	if (rcs_cflag)
	{
		c_comma(c_nfrstr);
		which += c_frstr;
		sprintf(hline, "%dL", which);
		c_wrlin();
		c_nfrstr++;
	}
}

static void wr_freestr(void)
{
	short	ifree, nfree;

	nfree = (*(unsigned short *)RP(RSH_NSTRING(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; c_freestr(ifree++))
			;
		nfree *= (short)RS_ENTRY;
		rsc_fwrite_longs(rcs_rhndl, str_ptr(0), nfree / (short)RS_ENTRY);
		(*(unsigned short *)RP(RSH_FRSTR(head)) = (rcs_wraddr));
		rcs_wraddr += nfree;
	}
}

static void c_imdata(long addr, short size)
{
	short	iwd, vwd;

	if (rcs_cflag)
	{
		sprintf(hline, "\nWORD IMAG%d[] = {", c_nimage);
		c_wrlin();

		for (iwd = 0; iwd < size; iwd += 2)
		{
			if (iwd)
			{
				strcpy(hline, ", ");
				c_wrlin();
			}
			if (iwd % 8 == 0)
			{
				strcpy(hline, "\n");
				c_wrlin();
			}
			vwd = (*(short *)RP(addr + iwd));
			sprintf(hline, "0x%X", (unsigned short)vwd);
			c_wrlin();
		}

		c_tail(true);
		c_obndx[c_nstring + c_nimage++] = rcs_wraddr;
	}
}

static void wr_imdata(long where, short w, short h)
{
	short	size;
	long	imaddr;

	imaddr = RGET(where);
	if ( imaddr != -1L)
	{
		size = w * h;
		gsx_untrans(imaddr, w, WR_OBNDX(size), w, h);
		imaddr = WR_OBNDX(size);
		c_imdata(imaddr, size);
		(RFIELD(where) = ((long) rcs_wraddr & 0xffff));
		rsc_fwrite_words(rcs_rhndl, imaddr, size);
		rcs_wraddr += size;
	}
}

static void wr_image(long tree, short which)
{
	long	iconblk, bitblk;
	short	type, w, h;

	type = ((GET_TYPE(tree, which)) & 0xff);
	if (type == G_IMAGE)
	{
		bitblk = GET_SPEC(tree, which);
		if ( bitblk != -1L)
		{
			w = (*(short *)RP(BI_WB(bitblk)));
			h = (*(short *)RP(BI_HL(bitblk)));
			wr_imdata(BI_PDATA(bitblk), w, h);
		}
	}
	else if (type == G_ICON)
	{
		iconblk = GET_SPEC(tree, which);
		if ( iconblk != -1L && !wr_kept(tree, which))
		{
			w = ((*(short *)RP(IB_WICON(iconblk))) + 7) / 8;
			h = (*(short *)RP(IB_HICON(iconblk)));
			wr_imdata(IB_PMASK(iconblk), w, h);
			wr_imdata(IB_PDATA(iconblk), w, h);
		}
	}
}

static void map_frimg(void)
{
	short	ifree, nfree, w, h;
	long	bitblk;

	nfree = (*(unsigned short *)RP(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
		{
			if ( (bitblk = img_addr(ifree)) == -1L)
				break;
			w = (*(short *)RP(BI_WB(bitblk)));
			h = (*(short *)RP(BI_HL(bitblk)));
			wr_imdata(bitblk, w, h);
		}
	}
}

static void c_foobar(void)
{
	short	img;

	if (rcs_cflag)
	{
		strcpy(hline, "\nstruct foobar {\n\tWORD\tdummy;");
		c_wrlin();
		strcpy(hline, "\n\tWORD\t*image;\n\t} rs_imdope[] = {\n");
		c_wrlin();

		for (img = 0; img < c_nimage; img++)
		{
			c_comma(img);
			sprintf(hline, "0, &IMAG%d[0]", img);
			c_wrlin();
		}

		c_tail(img);
	}
}

static void c_iconhead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nICONBLK rs_iconblk[] = {\n");
		c_wrlin();
		c_nib = 0;
	}
}

static void c_iconblk(long addr)
{
	ICONBLK	here;

	if (rcs_cflag)
	{
		memmove((void *)(&here), RP(addr), sizeof(ICONBLK));
		here.ib_pdata = HPTR(short) c_imgno(HV(here.ib_pdata));
		here.ib_pmask = HPTR(short) c_imgno(HV(here.ib_pmask));
		here.ib_ptext = HPTR(char) c_strno(HV(here.ib_ptext));
		c_comma(c_nib);
		sprintf(hline, "%ldL, %ldL, %ldL, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d",
			HV(here.ib_pmask), HV(here.ib_pdata), HV(here.ib_ptext),
			here.ib_char, here.ib_xchar, here.ib_ychar,
			here.ib_xicon, here.ib_yicon, here.ib_wicon, here.ib_hicon,
			here.ib_xtext, here.ib_ytext, here.ib_wtext, here.ib_htext );
		c_wrlin();
		c_nib++;
	}
}

static void wr_iconblk(long tree, short which)
{
	long	iconblk;

	if (((GET_TYPE(tree, which)) & 0xff) == G_ICON && !wr_kept(tree, which))
	{
		iconblk = GET_SPEC(tree, which);
		if ( iconblk != -1L)
		{
			SET_SPEC(tree, which, (long) rcs_wraddr & 0xffff);
			rsc_fwrite_iconblk(rcs_rhndl, iconblk);
			rcs_wraddr += DISK_ICONBLK_SIZE;
			LWINC(RSH_NIB(head));
			c_iconblk(iconblk);
		}
	}
}

static void c_bithead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nBITBLK rs_bitblk[] = {\n");
		c_wrlin();
		c_nbb = 0;
	}
}

static void c_bitblk(long addr)
{
	BITBLK	here;

	if (rcs_cflag)
	{
		memmove((void *)(&here), RP(addr), sizeof(BITBLK));
		here.bi_pdata = HPTR(short) c_imgno(HV(here.bi_pdata));
		c_comma(c_nbb);
		sprintf(hline, "%ldL, %d, %d, %d, %d, %d",
			HV(here.bi_pdata), here.bi_wb, here.bi_hl,
			here.bi_x, here.bi_y, here.bi_color );
		c_wrlin();
		c_nbb++;
	}
}

static void wr_bitblk(long tree, short which)
{
	long	bitblk;

	if (((GET_TYPE(tree, which)) & 0xff) == G_IMAGE)
	{
		bitblk = GET_SPEC(tree, which);
		if ( bitblk != -1L)
		{
			SET_SPEC(tree, which, (long) rcs_wraddr & 0xffff);
			rsc_fwrite_bitblk(rcs_rhndl, bitblk);
			rcs_wraddr += DISK_BITBLK_SIZE;
			LWINC(RSH_NBB(head));
			c_bitblk(bitblk);
		}
	}
}

static void map_frbit(void)
{
	short	ifree, nfree;
	long	bitblk;

	nfree = (*(unsigned short *)RP(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
		{
			if ( (bitblk = img_addr(ifree)) == -1L)
				break;
			(RFIELD(img_ptr(ifree)) = ((long) rcs_wraddr & 0xffff));
			rsc_fwrite_bitblk(rcs_rhndl, bitblk);
			rcs_wraddr += DISK_BITBLK_SIZE;
			LWINC(RSH_NBB(head));
			c_bitblk(bitblk);
		}
	}
} 

static void c_frbhead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nLONG rs_frimg[] = {\n");
		c_wrlin();
		c_nfrbit = 0;
	}
}

static void c_frb(long addr)
{
	short	blkno;

	if (rcs_cflag)
	{
		blkno = (short) ((addr - (*(unsigned short *)RP(RSH_BITBLK(head)))) / DISK_BITBLK_SIZE);
		c_comma(c_nfrbit);
		sprintf(hline, "%dL", blkno);
		c_wrlin();
		c_nfrbit++;
	}
}

static void wr_frbit(void)
{
	short	ifree, nfree;

	nfree = (*(unsigned short *)RP(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
			c_frb(img_addr(ifree));
		nfree *= (short)RS_ENTRY;
		rsc_fwrite_longs(rcs_rhndl, img_ptr(0), nfree / (short)RS_ENTRY);
		(*(unsigned short *)RP(RSH_FRIMG(head)) = (rcs_wraddr));
		rcs_wraddr += nfree;
	}
} 

static void c_tedhead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nTEDINFO rs_tedinfo[] = {\n");
		c_wrlin();
		c_nted = 0;
	}
}

static void c_tedinfo(long addr)
{
	TEDINFO	here;

	if (rcs_cflag)
	{
		memmove((void *)(&here), RP(addr), sizeof(TEDINFO));
		here.te_ptext = HPTR(char) c_strno(HV(here.te_ptext));
		here.te_pvalid = HPTR(char) c_strno(HV(here.te_pvalid));
		here.te_ptmplt = HPTR(char) c_strno(HV(here.te_ptmplt));
		c_comma(c_nted);
		sprintf(hline, "%ldL, %ldL, %ldL, %d, %d, %d, 0x%X, 0x%X, %d, %d,%d",
			HV(here.te_ptext), HV(here.te_ptmplt), HV(here.te_pvalid),
			here.te_font, here.te_fontid, here.te_just, (unsigned short)here.te_color,
			(unsigned short)here.te_fontsize, here.te_thickness, here.te_txtlen, here.te_tmplen );
		c_wrlin();
		c_nted++;
	}
}
	
static void wr_tedinfo(long tree, short which)
{
	long	tedinfo;
	short	type;

	type = ((GET_TYPE(tree, which)) & 0xff);
	if ( type == G_TEXT || type == G_BOXTEXT || 
		type == G_FTEXT || type == G_FBOXTEXT )
	{
		tedinfo = GET_SPEC(tree, which);
		if ( tedinfo != -1L)
		{
			SET_SPEC(tree, which, (long) rcs_wraddr & 0xffff);
			rsc_fwrite_tedinfo(rcs_rhndl, tedinfo);
			rcs_wraddr += DISK_TEDINFO_SIZE;
			LWINC(RSH_NTED(head));
			c_tedinfo(tedinfo);
		}
	}
}

static void c_objhead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nOBJECT rs_object[] = {\n");
		c_wrlin();
		c_nobs = 0;
	}
}

static void c_object(long tree)
{
	OUTOBJ	here;
	char	type[10], state[10], flags[10];

	if (rcs_cflag)
	{
		memmove((void *)(&here.next), RP(OB_NEXT(ROOT)), 3 * sizeof(short));
#ifdef RCS_HOST
		/* the spec of the record has 4 bytes, the long of OUTOBJ 8 */
		here.spec = RGET(OB_SPEC(ROOT));
		memmove((void *)(&here.x), RP(OB_SPEC(ROOT) + RS_ENTRY), 4 * sizeof(unsigned short));
#else
		memmove((void *)(&here.spec), RP(OB_SPEC(ROOT)), sizeof(long) + 4 * sizeof(unsigned short));
#endif

		switch (((GET_TYPE(tree, ROOT)) & 0xff))
		{
			case G_STRING:
			case G_BUTTON:
			case G_TITLE:
				here.spec = (long) c_strno(here.spec);
				break;
			case G_TEXT:
			case G_FTEXT:
			case G_BOXTEXT:
			case G_FBOXTEXT:
				here.spec = ( (short) here.spec - (*(unsigned short *)RP(RSH_TEDINFO(head))) )
					/ sizeof(TEDINFO);
				break;
			case G_IMAGE:
				here.spec = ( (short) here.spec - (*(unsigned short *)RP(RSH_BITBLK(head))) )
					/ DISK_BITBLK_SIZE;
				break;
			case G_ICON:
				here.spec = ( (short) here.spec - (*(unsigned short *)RP(RSH_ICONBLK(head))) )
					/ DISK_ICONBLK_SIZE;
				break;
			default:
				break;
		}

		c_comma(c_nobs);
		here.type = c_lookup(((GET_TYPE(tree, ROOT)) & 0xff), c_types, N_TYPES, type);
		here.flags = c_lookup(GET_FLAGS(tree, ROOT), c_flags, 
			N_FLAGS, flags);
		here.state = c_lookup(GET_STATE(tree, ROOT), c_states, 
			N_STATES, state);
		sprintf(hline, "%d, %d, %d, %s, %s, %s, 0x%lX, %d,%d, %d, %d",
			here.next, here.head, here.tail,
			here.type, here.flags, here.state,
			HSPEC(here.spec), here.x, here.y, here.w, here.h );
		c_wrlin();
		c_nobs++;
	}
}

static void wr_obj(short itree, long tree, short iobj)
/* refers to position in the preorder 	*/
/* trace stored in wr_obndx[]		*/
{
	short	entno, where, link, obtype;
	long	object;
	struct cicon_kept	*kept;

	where = wr_obndx[iobj];		/* the actual object #	*/

	entno = find_obj(tree, where);
	if (entno != NIL)
		set_value(entno, (char *) ( 
			((itree & 0xFF) << 8) | (iobj & 0xFF) ) );
	if ( (link = GET_NEXT(tree, where)) != -1)
		SET_NEXT(tree, where, fnd_ob(link));
	if ( (link = GET_HEAD(tree, where)) != -1)
		SET_HEAD(tree, where, fnd_ob(link));
	if ( (link = GET_TAIL(tree, where)) != -1)
		SET_TAIL(tree, where, fnd_ob(link));
	obtype = GET_TYPE(tree, where) & 0x00ff; /*low byte*/
	if ((kept = wr_kept(tree, where)) != 0 && wr_ncic < MAXCICON)
	{
		/* a colour icon: its spec is the number in the table written last */
		wr_cic[wr_ncic] = kept;
		wr_cic_ib[wr_ncic] = GET_SPEC(tree, where);
		SET_SPEC(tree, where, (long) wr_ncic++);
		obtype = G_CICON;
	}
	if(obtype == G_ICON || obtype == G_IMAGE || obtype == G_CICON) /*0 hi byte*/
		(*(short *)RP(OB_TYPE(where)) = (obtype));
	unfix_chpos(OB_X(where), 0);
	unfix_chpos(OB_WIDTH(where), 0);
	unfix_chpos(OB_Y(where), 1);
	unfix_chpos(OB_HEIGHT(where), 1);
	object = tree + (unsigned short) (where * sizeof(OBJECT));
	rsc_fwrite_object(rcs_rhndl, object);
	rcs_wraddr += DISK_OBJECT_SIZE;
	c_object(object);
}

static void wr_trees(void)
{
	long	tree;
	short	ntree, itree, iobj, where, flags;

	ntree = (*(unsigned short *)RP(RSH_NTREE(head)));

	for (itree = 0; itree < ntree; itree++)
	{
		where = find_tree(itree);
		set_value(where, (char *) itree);
		tree = tree_addr(itree);
		(RFIELD(tree_ptr(itree)) = ((long) rcs_wraddr & 0xffff));
		wr_obnum = 0;
		map_tree(tree, 0, -1, (fkt_parm)indx_obs);
		(*(unsigned short *)RP(RSH_NOBS(head)) = (wr_obnum + (*(unsigned short *)RP(RSH_NOBS(head)))));
		iobj = wr_obndx[wr_obnum - 1];
		flags = GET_FLAGS(tree, iobj);
		SET_FLAGS(tree, iobj, flags | LASTOB);
		for (iobj = 0; iobj < wr_obnum; iobj++)
			wr_obj(itree, tree, iobj);
	}
}

static void c_treehead(void)
{
	if (rcs_cflag)
	{
		strcpy(hline, "\nLONG rs_trindex[] = {\n");
		c_wrlin();
		c_ntree = 0;
	}
}

static void c_tree(long addr)
{
	short	objno;

	if (rcs_cflag)
	{
		objno = (short) (addr - (*(unsigned short *)RP(RSH_OBJECT(head))) / sizeof(OBJECT));
		c_comma(c_ntree);
		sprintf(hline, "%dL", objno);
		c_wrlin();
		c_ntree++;
	}
}

static void wr_trindex(void)
{
	short	ntree, itree;

	ntree = (*(unsigned short *)RP(RSH_NTREE(head)));
	rsc_fwrite_longs(rcs_rhndl, tree_ptr(0), ntree);

	for (itree = 0; itree < ntree; itree++)
		c_tree(tree_addr(itree));

	(*(unsigned short *)RP(RSH_TRINDEX(head)) = (rcs_wraddr));	/* Don't move this line! */
	rcs_wraddr += ntree * (short)RS_ENTRY;
}

static void c_defs(void)
{
	char	*rptr, *sptr;

	if (rcs_cflag)
	{
		strcpy(hline, "\n");
		c_wrlin();
		c_defline("NUM_STRINGS", c_nstring);
		c_defline("NUM_FRSTR", (*(unsigned short *)RP(RSH_NSTRING(head))));
		c_defline("NUM_IMAGES", c_nimage);
		c_defline("NUM_BB", (*(unsigned short *)RP(RSH_NBB(head))));
		c_defline("NUM_FRIMG", (*(unsigned short *)RP(RSH_NIMAGES(head))));
		c_defline("NUM_IB", (*(unsigned short *)RP(RSH_NIB(head))));
		c_defline("NUM_TI", (*(unsigned short *)RP(RSH_NTED(head))));
		c_defline("NUM_OBS", (*(unsigned short *)RP(RSH_NOBS(head))));
		c_defline("NUM_TREE", (*(unsigned short *)RP(RSH_NTREE(head))));
		for (sptr = rptr = &rcs_rfile[0]; *sptr; sptr++)
			if (*sptr == '\\' || *sptr == '\:')
				rptr = sptr + 1;
		sprintf(hline, "\nBYTE pname[] = \"%s\";", rptr);
		c_wrlin();
		hline[0] = '\032';
		hline[1] = '\0';
		c_wrlin();
	}
}

static void wr_include(short deftype, short ndx, short ndef, char *ptns[], short trflag)
{
	short	idx, ii;
	HOUT	h;

	for ( idx = 0; idx < ndx; idx++ )
	{
		for ( ii = 0; ii < rcs_ndxno; ii++)
		{
			if((trflag && get_kind(ii) < 4)	|| ( get_kind(ii) == deftype ))
			{
				if ( (short) get_value(ii) == idx)
				{
					strup(h.p0 = get_name(ii));
					h.p1 = idx;	
					sprintf(hline, ptns[ndef], h.p0, h.p1);
					fputs(hline, rcs_hhndl);
					break;
				}
			}
		}
	}
}

static void obj_include(short ntree, short nobs, char *ptns[])
{
	short	itree,  iobs, ii;
	HOUT	h;
	
	for ( itree = 0; itree < ntree; itree++ )
	{
		for ( iobs = 0; iobs < nobs; iobs++)
		{
	    	for ( ii = 0; ii < rcs_ndxno; ii++ )
	    	{
				if ( get_kind(ii) == OBJKIND)
				{
					h.p1 = (short) get_value(ii);
					h.p2 = (short) (((h.p1) >> 8) & 0xff) & 0xff;
					h.p1 = (short) ((h.p1) & 0xff) & 0xff;
					if ( h.p2 == itree && h.p1 == iobs)
					{
						strup(h.p0 = get_name(ii));
						sprintf(hline, ptns[2], h.p0, h.p1, h.p2);
						fputs(hline, rcs_hhndl);
						break;
					}
				}
			}
		}
	}
}

static void wrsrt_inclfile(char *ext, char *ptns[])
{
	r_to_xfile(rcs_hfile, ext);

	for (;;)
	{
		rcs_hhndl = fopen(rcs_hfile, "w");
		if (rcs_hhndl != NULL)
			break;
		else if (!form_error(errno))
			return;
	}
	wr_include( UNKN,  (*(unsigned short *)RP(RSH_NTREE(head))), 0, ptns,true );
	obj_include( (*(unsigned short *)RP(RSH_NTREE(head))), (*(unsigned short *)RP(RSH_NOBS(head))), ptns );
	wr_include(ALRT, (*(unsigned short *)RP(RSH_NSTRING(head))), 1, ptns, false);
	wr_include(FRSTR,(*(unsigned short *)RP(RSH_NSTRING(head))), 3, ptns, false);
	wr_include(FRIMG,(*(unsigned short *)RP(RSH_NIMAGES(head))), 4, ptns, false);
	fclose(rcs_hhndl);
}

static void write_inclfile(char *ext, char *ptns[])
{
	short	ii;
	HOUT	h;

	r_to_xfile(rcs_hfile, ext);

	for (;;)
	{
		rcs_hhndl = fopen(rcs_hfile, "w");
		if (rcs_hhndl != NULL)
			break;
		else if (!form_error(errno))
			return;
	}

	for (ii = 0; ii < rcs_ndxno; ii++)
	{
		strup(h.p0 = get_name(ii));
		h.p1 = (short) get_value(ii);
		switch ( get_kind(ii) )
		{
			case UNKN:
			case PANL:
			case DIAL:
			case MENU:
				sprintf(hline, ptns[0], h.p0, h.p1);
				break;
			case ALRT:
				sprintf(hline, ptns[1], h.p0, h.p1);
				break;
			case OBJKIND:
				h.p2 = (short) (((h.p1) >> 8) & 0xff) & 0xff;
				h.p1 = (short) ((h.p1) & 0xff) & 0xff;
				sprintf(hline, ptns[2], h.p0, h.p1, h.p2);
				break;
			case FRSTR:
				sprintf(hline, ptns[3], h.p0, h.p1);
				break;
			case FRIMG:
				sprintf(hline, ptns[4], h.p0, h.p1);
				break;
			default:
				break;
		}
		fputs(hline, rcs_hhndl);
	}
	fclose(rcs_hhndl);
}

static void wr_mono(long where, short w, short h)
{
#ifdef RCS_HOST
	/* a mono icon without data (-1): write zeros, so the sizes still add up */
	if (HV(where) == -1L)
	{
		static const unsigned char	zero[2];
		long	n;

		for (n = w * h; n > 1; n -= 2)
			fwrite(zero, 1, 2, rcs_rhndl);
		return;
	}
#endif
	gsx_untrans(where, w, WR_OBNDX(w * h), w, h);
	rsc_fwrite_words(rcs_rhndl, WR_OBNDX(w * h), w * h);
}

/* the table of the CICONBLKs and the CICONBLKs: the 38 bytes of the
 * ICONBLK as RCS has it now, its mono data and mask, its text, and the
 * colour versions as they were read */
static void wr_cicons(void)
{
	unsigned char	b[38];
	ICONBLK	*ib;
	short	i, w, h;
	unsigned long	off;

	if (!wr_ncic)
		return;
	/* The 16 bits of rsh_rssize stop at the extension, and what comes
	 * after it has 32-bit offsets: the table of CICONBLKs and the CICONBLKs
	 * are not limited by them. */
	wr_sync();
	wr_cic_rssize = rcs_wraddr;
	memset(b, 0, 12);
	fwrite(b, 1, 12, rcs_rhndl);		/* the extension is written last */
	wr_cic_table = (unsigned long)rcs_wraddr + 12UL;
	off = wr_cic_table + 4UL * (wr_ncic + 1);
	for (i = 0; i < wr_ncic; i++)
	{
		wr_be32(b, off);
		fwrite(b, 1, 4, rcs_rhndl);
		off += 38UL + 4UL * wr_cic[i]->words + 12UL + wr_cic[i]->vlen;
	}
	wr_be32(b, 0xffffffffUL);
	fwrite(b, 1, 4, rcs_rhndl);
	for (i = 0; i < wr_ncic; i++)
	{
		ib = (ICONBLK *)RP(wr_cic_ib[i]);
		memcpy(b, wr_cic[i]->head, 38);
		wr_be16(b + 12, ib->ib_char);
		wr_be16(b + 14, ib->ib_xchar);
		wr_be16(b + 16, ib->ib_ychar);
		wr_be16(b + 18, ib->ib_xicon);
		wr_be16(b + 20, ib->ib_yicon);
		wr_be16(b + 22, ib->ib_wicon);
		wr_be16(b + 24, ib->ib_hicon);
		wr_be16(b + 26, ib->ib_xtext);
		wr_be16(b + 28, ib->ib_ytext);
		wr_be16(b + 30, ib->ib_wtext);
		wr_be16(b + 32, ib->ib_htext);
		fwrite(b, 1, 38, rcs_rhndl);
		w = (ib->ib_wicon + 7) / 8;
		h = ib->ib_hicon;
		wr_mono((long)ib->ib_pdata, w, h);
		wr_mono((long)ib->ib_pmask, w, h);
		memset(b, 0, 12);
#ifdef RCS_HOST
		strncpy((char *)b, ib->ib_ptext && HV(ib->ib_ptext) != -1L ?
			(char *)RP(ib->ib_ptext) : "", 12);
#else
		strncpy((char *)b, ib->ib_ptext ? ib->ib_ptext : "", 12);
#endif
		fwrite(b, 1, 12, rcs_rhndl);
		fwrite(wr_cic[i]->versions, 1, wr_cic[i]->vlen, rcs_rhndl);
	}
	wr_cic_total = off;
}

short write_files(void)
{
	long	mem_needs;
	
	do_tally();			 
	wr_ncic = 0;
	cicon_prepare(0x1000000L - 1024);
	if (rcs_cflag)
		mem_needs = 3 * rcs_tally.nbytes;
	else
		mem_needs = (rcs_tally.nbytes >> 1) + rcs_tally.nbytes;

	if( mem_needs > get_dspace( rcs_rfile[0] - 'A' + 1 ) )
	{
		hndl_alert(1, string_addr(SNDSPACE));
		return(false);
	}

	for (;;)
	{
		rcs_rhndl = fopen(rcs_rfile,"wb");
		if (rcs_rhndl != NULL)
			break;
		if (!form_error(errno))
			return (false);
	}

	if (rcs_cflag)
	{
		r_to_xfile(rcs_hfile, "RSH");
		for (;;)
		{
			rcs_hhndl = fopen(rcs_hfile, "w");
			if (rcs_hhndl != NULL)
				break;
			if (!form_error(errno))
			{
				fclose(rcs_rhndl);
				return (false);
			}
		}
	}		       
	dcomp_free();
	dcomp_alerts();

	ini_prog();
	show_prog(STHDR);
	wr_header();
	rcs_wraddr = (short)sizeof(RSHDR);
	(*(unsigned short *)RP(RSH_STRING(head)) = (rcs_wraddr));
	c_bases();

	show_prog(STSTR);
	c_strhead();
	map_all((fkt_parm)wr_string);
	map_frstr();		/* handles strings not ref'ed in objects */
	c_tail(c_nstring);
	wr_sync();

	show_prog(STIMG);
	(*(unsigned short *)RP(RSH_IMDATA(head)) = (rcs_wraddr));
	c_nimage = 0;
	map_all((fkt_parm)wr_image);
	c_frimg = c_nimage;
	map_frimg();

	show_prog(STFRSTR);
	c_frshead();
	wr_freestr();
	c_tail(c_nfrstr);

	show_prog(STBB);
	(*(unsigned short *)RP(RSH_BITBLK(head)) = (rcs_wraddr));
	(*(unsigned short *)RP(RSH_NBB(head)) = (0));
	c_bithead();
	map_all((fkt_parm)wr_bitblk);
	map_frbit();	
	c_tail(c_nbb);

	show_prog(STFRIMG);
	c_frbhead();
	wr_frbit();
	c_tail(c_nfrbit);

	show_prog(STIB);
	(*(unsigned short *)RP(RSH_ICONBLK(head)) = (rcs_wraddr));
	(*(unsigned short *)RP(RSH_NIB(head)) = (0));
	c_iconhead();
	map_all((fkt_parm)wr_iconblk);
	c_tail(c_nib);

	show_prog(STTI);
	(*(unsigned short *)RP(RSH_TEDINFO(head)) = (rcs_wraddr));
	(*(unsigned short *)RP(RSH_NTED(head)) = (0));
	c_tedhead();
	map_all((fkt_parm)wr_tedinfo);
	c_tail(c_nted);

	show_prog(STOBJ);
	(*(unsigned short *)RP(RSH_OBJECT(head)) = (rcs_wraddr));
	(*(unsigned short *)RP(RSH_NOBS(head)) = (0));
	c_objhead();
	wr_trees();
	c_tail(c_nobs);

	show_prog(STTRIND);
	c_treehead();
	wr_trindex();		/* also handles RSH_TRINDEX(head) */
	c_tail(c_ntree);
	wr_cicons();

	show_prog(STHDR);
	(*(unsigned short *)RP(RSH_RSSIZE(head)) = (rcs_wraddr));
	if (wr_ncic)
		*(unsigned short *)RP(RSH_VRSN(head)) |= RCS_NEW_FORMAT;
	else
		*(unsigned short *)RP(RSH_VRSN(head)) &= ~RCS_NEW_FORMAT;
	wr_header();		/* rewrite updated header	  */
	if (wr_ncic)
	{
		/* the extension: length of the file, the table of colour icons */
		unsigned char	ext[12];

		wr_be32(ext, wr_cic_total);
		wr_be32(ext + 4, wr_cic_table);
		wr_be32(ext + 8, 0UL);
		fseek(rcs_rhndl, wr_cic_rssize, SEEK_SET);
		fwrite(ext, 1, 12, rcs_rhndl);
	}
	fclose(rcs_rhndl);
	c_foobar();
	c_defs();
	if (rcs_cflag)
		fclose(rcs_hhndl);

	for (;;)
	{
		rcs_dhndl = fopen(rcs_dfile, "wb");
		if (rcs_dhndl != NULL)
			break;
		if (!form_error(errno))
			return (false);
	}

	show_prog(STNAME);
   	wr_68kdef();
	fclose(rcs_dhndl);

	if (rcs_hflag)
		if ( rcs_fsrtflag )
			wrsrt_inclfile("H", hptns);
		else
			write_inclfile("H", hptns);
	if (rcs_oflag)
		if (rcs_fsrtflag )
			wrsrt_inclfile("I", optns);
		else
			write_inclfile("I", optns);
	if (rcs_cbflag)
		if ( rcs_fsrtflag )
			wrsrt_inclfile("B", bptns);
		else
			write_inclfile("B", bptns);
	if (rcs_f77flag)
		if ( rcs_fsrtflag )
			wrsrt_inclfile("F", fptns);
		else
			write_inclfile("F", fptns);

	return (true);
}
