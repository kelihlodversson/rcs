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
	rcs_tally.nbytes += (*(unsigned short *)(RSH_NTREE(head))) * sizeof(long);
	rcs_tally.nbytes += sizeof(RSHDR);
}

static long get_dspace(short drv)
{
	long	dsk_avail;
	_DISKINFO dsk_free_buf;

	Dfree(&dsk_free_buf, drv);
	dsk_avail = dsk_free_buf.b_free *
		dsk_free_buf.b_secsiz *
		dsk_free_buf.b_clsiz;
	return ( dsk_avail );
}

static short dspace(short drv, unsigned short nbytes)
{
	long	dsk_avail;
	_DISKINFO dsk_free_buf;

	Dfree(&dsk_free_buf, drv);
	dsk_avail = dsk_free_buf.b_free *
		dsk_free_buf.b_secsiz *
		dsk_free_buf.b_clsiz;
	if ( nbytes < (unsigned short) dsk_avail )
		return (false);
	else
		return (true);
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
		for (i = 0; i < (*(unsigned short *)(RSH_NTREE(head))); i++)
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
			hchar = (*(char *)(addr++));
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

static void wr_str(long where)
{
	long	staddr;
	short	stlen;

	staddr = (*(long *)(where));
	if ( staddr != -1L)
	{
		c_string(staddr);
		(*(long *)(where) = ((long) rcs_wraddr & 0xffff));
		stlen = 1 + (short)strlen((char *)(staddr));
		fwrite((void *)staddr, 1, stlen, rcs_rhndl);
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
		if ( (iconblk = GET_SPEC(tree, which)) != -1L)
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
	nfree = (*(unsigned short *)(RSH_NSTRING(head)));
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

	nfree = (*(unsigned short *)(RSH_NSTRING(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; c_freestr(ifree++))
			;
		nfree *= (short)sizeof(long);
		rsc_fwrite_longs(rcs_rhndl, str_ptr(0), nfree / (short)sizeof(long));
		(*(unsigned short *)(RSH_FRSTR(head)) = (rcs_wraddr));
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
			vwd = (*(short *)(addr + iwd));
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

	imaddr = (*(long *)(where));
	if ( imaddr != -1L)
	{
		size = w * h;
		gsx_untrans(imaddr, w, (long)((long)(wr_obndx)), w, h);
		imaddr = (long)((long)(wr_obndx));
		c_imdata(imaddr, size);
		(*(long *)(where) = ((long) rcs_wraddr & 0xffff));
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
			w = (*(short *)(BI_WB(bitblk)));
			h = (*(short *)(BI_HL(bitblk)));
			wr_imdata(BI_PDATA(bitblk), w, h);
		}
	}
	else if (type == G_ICON)
	{
		iconblk = GET_SPEC(tree, which);
		if ( iconblk != -1L)
		{
			w = ((*(short *)(IB_WICON(iconblk))) + 7) / 8;
			h = (*(short *)(IB_HICON(iconblk)));
			wr_imdata(IB_PMASK(iconblk), w, h);
			wr_imdata(IB_PDATA(iconblk), w, h);
		}
	}
}

static void map_frimg(void)
{
	short	ifree, nfree, w, h;
	long	bitblk;

	nfree = (*(unsigned short *)(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
		{
			if ( (bitblk = img_addr(ifree)) == -1L)
				break;
			w = (*(short *)(BI_WB(bitblk)));
			h = (*(short *)(BI_HL(bitblk)));
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
		memmove((void *)(((long)(&here))), (void *)(addr), sizeof(ICONBLK));
		here.ib_pdata = (short  *) c_imgno((long)here.ib_pdata);
		here.ib_pmask = (short  *) c_imgno((long)here.ib_pmask);
		here.ib_ptext = (char  *) c_strno((long)here.ib_ptext);
		c_comma(c_nib);
		sprintf(hline, "%ldL, %ldL, %ldL, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d",
			(long)here.ib_pmask, (long)here.ib_pdata, (long)here.ib_ptext,
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

	if (((GET_TYPE(tree, which)) & 0xff) == G_ICON)
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
		memmove((void *)((long)((long)(&here))), (void *)(addr), sizeof(BITBLK));
		here.bi_pdata = (short *) c_imgno((long)here.bi_pdata);
		c_comma(c_nbb);
		sprintf(hline, "%ldL, %d, %d, %d, %d, %d",
			(long)here.bi_pdata, here.bi_wb, here.bi_hl,
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

	nfree = (*(unsigned short *)(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
		{
			if ( (bitblk = img_addr(ifree)) == -1L)
				break;
			(*(long *)(img_ptr(ifree)) = ((long) rcs_wraddr & 0xffff));
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
		blkno = (short) ((addr - (*(unsigned short *)(RSH_BITBLK(head)))) / DISK_BITBLK_SIZE);
		c_comma(c_nfrbit);
		sprintf(hline, "%dL", blkno);
		c_wrlin();
		c_nfrbit++;
	}
}

static void wr_frbit(void)
{
	short	ifree, nfree;

	nfree = (*(unsigned short *)(RSH_NIMAGES(head)));
	if ( nfree )
	{
		for (ifree = 0; ifree < nfree; ifree++)
			c_frb(img_addr(ifree));
		nfree *= (short)sizeof(long);
		rsc_fwrite_longs(rcs_rhndl, img_ptr(0), nfree / (short)sizeof(long));
		(*(unsigned short *)(RSH_FRIMG(head)) = (rcs_wraddr));
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
		memmove((void *)(((long)(&here))), (void *)(addr), sizeof(TEDINFO));
		here.te_ptext = (char *) c_strno((long)here.te_ptext);
		here.te_pvalid = (char *) c_strno((long)here.te_pvalid);
		here.te_ptmplt = (char *) c_strno((long)here.te_ptmplt);
		c_comma(c_nted);
		sprintf(hline, "%ldL, %ldL, %ldL, %d, %d, %d, 0x%X, 0x%X, %d, %d,%d",
			(long)here.te_ptext, (long)here.te_ptmplt, (long)here.te_pvalid,
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
		memmove((void *)(((long)(&here.next))), (void *)(OB_NEXT(ROOT)), 3 * sizeof(short));
		memmove((void *)(((long)(&here.spec))), (void *)(OB_SPEC(ROOT)), sizeof(long) + 4 * sizeof(unsigned short));

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
				here.spec = ( (short) here.spec - (*(unsigned short *)(RSH_TEDINFO(head))) )
					/ sizeof(TEDINFO);
				break;
			case G_IMAGE:
				here.spec = ( (short) here.spec - (*(unsigned short *)(RSH_BITBLK(head))) )
					/ DISK_BITBLK_SIZE;
				break;
			case G_ICON:
				here.spec = ( (short) here.spec - (*(unsigned short *)(RSH_ICONBLK(head))) )
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
			here.spec, here.x, here.y, here.w, here.h );
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
	if(obtype == G_ICON || obtype == G_IMAGE) /*0 hi byte*/
		(*(short *)(OB_TYPE(where)) = (obtype));
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

	ntree = (*(unsigned short *)(RSH_NTREE(head)));

	for (itree = 0; itree < ntree; itree++)
	{
		where = find_tree(itree);
		set_value(where, (char *) itree);
		tree = tree_addr(itree);
		(*(long *)(tree_ptr(itree)) = ((long) rcs_wraddr & 0xffff));
		wr_obnum = 0;
		map_tree(tree, 0, -1, (fkt_parm)indx_obs);
		(*(unsigned short *)(RSH_NOBS(head)) = (wr_obnum + (*(unsigned short *)(RSH_NOBS(head)))));
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
		objno = (short) (addr - (*(unsigned short *)(RSH_OBJECT(head))) / sizeof(OBJECT));
		c_comma(c_ntree);
		sprintf(hline, "%dL", objno);
		c_wrlin();
		c_ntree++;
	}
}

static void wr_trindex(void)
{
	short	ntree, itree;

	ntree = (*(unsigned short *)(RSH_NTREE(head)));
	rsc_fwrite_longs(rcs_rhndl, tree_ptr(0), ntree);

	for (itree = 0; itree < ntree; itree++)
		c_tree(tree_addr(itree));

	(*(unsigned short *)(RSH_TRINDEX(head)) = (rcs_wraddr));	/* Don't move this line! */
	rcs_wraddr += ntree * (short)sizeof(long);
}

static void c_defs(void)
{
	char	*rptr, *sptr;

	if (rcs_cflag)
	{
		strcpy(hline, "\n");
		c_wrlin();
		c_defline("NUM_STRINGS", c_nstring);
		c_defline("NUM_FRSTR", (*(unsigned short *)(RSH_NSTRING(head))));
		c_defline("NUM_IMAGES", c_nimage);
		c_defline("NUM_BB", (*(unsigned short *)(RSH_NBB(head))));
		c_defline("NUM_FRIMG", (*(unsigned short *)(RSH_NIMAGES(head))));
		c_defline("NUM_IB", (*(unsigned short *)(RSH_NIB(head))));
		c_defline("NUM_TI", (*(unsigned short *)(RSH_NTED(head))));
		c_defline("NUM_OBS", (*(unsigned short *)(RSH_NOBS(head))));
		c_defline("NUM_TREE", (*(unsigned short *)(RSH_NTREE(head))));
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
	wr_include( UNKN,  (*(unsigned short *)(RSH_NTREE(head))), 0, ptns,true );
	obj_include( (*(unsigned short *)(RSH_NTREE(head))), (*(unsigned short *)(RSH_NOBS(head))), ptns );
	wr_include(ALRT, (*(unsigned short *)(RSH_NSTRING(head))), 1, ptns, false);
	wr_include(FRSTR,(*(unsigned short *)(RSH_NSTRING(head))), 3, ptns, false);
	wr_include(FRIMG,(*(unsigned short *)(RSH_NIMAGES(head))), 4, ptns, false);
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

short write_files(void)
{
	long	mem_needs;
	
	do_tally();			 
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
	(*(unsigned short *)(RSH_STRING(head)) = (rcs_wraddr));
	c_bases();

	show_prog(STSTR);
	c_strhead();
	map_all((fkt_parm)wr_string);
	map_frstr();		/* handles strings not ref'ed in objects */
	c_tail(c_nstring);
	wr_sync();

	show_prog(STIMG);
	(*(unsigned short *)(RSH_IMDATA(head)) = (rcs_wraddr));
	c_nimage = 0;
	map_all((fkt_parm)wr_image);
	c_frimg = c_nimage;
	map_frimg();

	show_prog(STFRSTR);
	c_frshead();
	wr_freestr();
	c_tail(c_nfrstr);

	show_prog(STBB);
	(*(unsigned short *)(RSH_BITBLK(head)) = (rcs_wraddr));
	(*(unsigned short *)(RSH_NBB(head)) = (0));
	c_bithead();
	map_all((fkt_parm)wr_bitblk);
	map_frbit();	
	c_tail(c_nbb);

	show_prog(STFRIMG);
	c_frbhead();
	wr_frbit();
	c_tail(c_nfrbit);

	show_prog(STIB);
	(*(unsigned short *)(RSH_ICONBLK(head)) = (rcs_wraddr));
	(*(unsigned short *)(RSH_NIB(head)) = (0));
	c_iconhead();
	map_all((fkt_parm)wr_iconblk);
	c_tail(c_nib);

	show_prog(STTI);
	(*(unsigned short *)(RSH_TEDINFO(head)) = (rcs_wraddr));
	(*(unsigned short *)(RSH_NTED(head)) = (0));
	c_tedhead();
	map_all((fkt_parm)wr_tedinfo);
	c_tail(c_nted);

	show_prog(STOBJ);
	(*(unsigned short *)(RSH_OBJECT(head)) = (rcs_wraddr));
	(*(unsigned short *)(RSH_NOBS(head)) = (0));
	c_objhead();
	wr_trees();
	c_tail(c_nobs);

	show_prog(STTRIND);
	c_treehead();
	wr_trindex();		/* also handles RSH_TRINDEX(head) */
	c_tail(c_ntree);

	show_prog(STHDR);
	(*(unsigned short *)(RSH_RSSIZE(head)) = (rcs_wraddr));
	wr_header();		/* rewrite updated header	  */
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
