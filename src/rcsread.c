 /*	RCSREAD.C	1/28/85 - 1/28/85	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcslib.h"
#include "rcsintf.h"
#include "rcsalert.h"
#include "rcsfiles.h"
#include "rcswrite.h"
#include "rcsread.h"

#define ROB_TYPE (psubstruct + 6)	/* Long pointer in OBJECT	*/
#define ROB_FLAGS (psubstruct + 8)	/* Long pointer in OBJECT	*/
#define ROB_SPEC (psubstruct + 12)	/* Long pointer in OBJECT	*/

#define RTE_PTEXT (psubstruct + 0)	/* Long pointers in TEDINFO	*/
#define RTE_PTMPLT (psubstruct + 4)
#define RTE_PVALID (psubstruct + 8)
#define RTE_TXTLEN (psubstruct + 24)
#define RTE_TMPLEN (psubstruct + 26)

#define RIB_PMASK (psubstruct + 0)	/* Long pointers in ICONBLK	*/
#define RIB_PDATA (psubstruct + 4)
#define RIB_PTEXT (psubstruct + 8)

#define RBI_PDATA (psubstruct + 0)	/* Long pointer in BITBLK	*/
#define RBI_WB (psubstruct + 4)
#define RBI_HL (psubstruct + 6)
/* in global array		*/
#define APP_LOPNAME (rs_global + 12)
#define APP_LO1RESV (rs_global + 16)
#define APP_LO2RESV (rs_global + 20)

static short read_68k(short merge)
/* simulate IBM PC data storage in "*.def" files. */
{
	unsigned short	idx, old_ndxno, rd_ndxno, rd_val, rd_kind;

	if ( merge )
		old_ndxno = rcs_ndxno;
	else
		old_ndxno = 0;

	if ( fseek( rcs_dhndl, 0, SEEK_SET) )
		return( rcs_ndxno = 0 );
	else if (fread( &rd_ndxno, 1, 2, rcs_dhndl ) != 2)
		return( rcs_ndxno = 0);
	else
	{
		rcs_ndxno = swap_bytes( rd_ndxno );
		rcs_ndxno = old_ndxno + rcs_ndxno;
		for ( idx = old_ndxno; idx < rcs_ndxno; idx++)
		{
			if (fread( &rd_val, 1, 2, rcs_dhndl ) != 2)
				return( rcs_ndxno = 0 );
			else
			{
				rcs_index[idx].val = (char *) ( ((short)(swap_bytes(rd_val))) & 0xffffL );
				if (fread( &rd_kind, 1, 2, rcs_dhndl ) != 2)
					return( rcs_ndxno = 0 );
				else
				{
					rcs_index[idx].kind = swap_bytes( rd_kind );
					if (fread( &rcs_index[idx].name[0], 1, 10, rcs_dhndl ) != 10)
						return( rcs_ndxno = 0 );
				}
			}
		}
		return( rcs_ndxno );
	}
}

short open_files(short def)
{
	short	ii;

	if (!def)
		if (!get_file(STLDRES))
			return (false);
	for (;;)
	{
		rcs_rhndl = fopen(rcs_rfile, "rb");
		if (rcs_rhndl != NULL)
			break;
		if (!form_error(errno))
			return(false);
	}
	for (;;)
	{
		rcs_dhndl = fopen(rcs_dfile, "rb");
		if (rcs_dhndl != NULL)
			return (true);
		else
		{
			ii = hndl_alert(2, string_addr(STNODEF));
			if (ii == 1)
			{
				fclose( rcs_rhndl );
				return (false);
			}
			if (ii == 2)
			{
				rcs_dhndl = NULL;
				return (true);
			}
		}
	}
}

static long get_sub(unsigned short rsindex, short rtype, short rsize)
{
	unsigned short		offset;

	offset = (*(short *)(rs_hdr + ((short)(rtype*2))));
	/* get base of objects	*/
	/*   and then index in	*/
	return( rs_hdr + offset + (unsigned short)(rsize * rsindex) );
}

/* return address of given type and index, INTERNAL ROUTINE */
static long get_addr(short rstype, short rsindex)
{
	long		psubstruct;
	short		size;
	short		rt;
	short		valid;

	valid = true;
	switch(rstype)
	{
		case R_OBJECT:
			rt = RT_OB;
			size = (short)sizeof(OBJECT);
			break;
		case R_TEDINFO:
		case R_TEPTEXT:
			rt = RT_TEDINFO;
			size = (short)sizeof(TEDINFO);
			break;
		case R_ICONBLK:
		case R_IBPMASK:
			rt = RT_ICONBLK;
			size = (short)sizeof(ICONBLK);
			break;
		case R_BITBLK:
		case R_BIPDATA:
			rt = RT_BITBLK;
			size = (short)sizeof(BITBLK);
			break;
		case R_OBSPEC:
			psubstruct = get_addr(R_OBJECT, rsindex);
			return( ROB_SPEC );
		case R_TEPTMPLT:
		case R_TEPVALID:
			psubstruct = get_addr(R_TEDINFO, rsindex);
	  		if (rstype == R_TEPTMPLT)
				return( RTE_PTMPLT );
			else
				return( RTE_PVALID );
		case R_IBPDATA:
		case R_IBPTEXT:
			psubstruct = get_addr(R_ICONBLK, rsindex);
			if (rstype == R_IBPDATA)
				return( RIB_PDATA );
			else
				return( RIB_PTEXT );
		case R_STRING:
			return( (*(long *)(get_sub(rsindex, RT_FREESTR, (short)sizeof(long)))) );
		case R_IMAGEDATA:
			return( (*(long *)(get_sub(rsindex, RT_FREEIMG, (short)sizeof(long)))) );
		case R_FRSTR:
			rt = RT_FREESTR;
			size = (short)sizeof(long);
			break;
		case R_FRIMG:
			rt = RT_FREEIMG;
			size = (short)sizeof(long);
			break;
		default:
			valid = false;
			break;
	}
	if (valid)
		return( get_sub(rsindex, rt, size) );
	else
		return(-1L);
} /* get_addr() */

static short fix_long(long plong)
{
	long		lngval;

	lngval = (*(long *)(plong));
	if (lngval != -1L)
	{
		(*(long *)(plong) = (rs_hdr + lngval));
		return(true);
	}
	else
		return(false);
}

static void fix_trindex(void)
{
	short		ii;
	long		ptreebase;

	ptreebase = get_sub(0, RT_TRINDEX, (short)sizeof(long));

	for (ii = NUM_TREE-1; ii >= 0; ii--)
		fix_long(ptreebase + ((short)(ii*4)));
}

static short fix_ptr(short type, short index)
{
	return( fix_long( get_addr(type, index) ) );
}

static void fix_objects(void)
{
	short		ii;
	short		obtype, obflags;
	long		psubstruct;

	for (ii = NUM_OBS-1; ii >= 0; ii--)
	{
		psubstruct = get_addr(R_OBJECT, ii);
		rs_obfix(psubstruct, 0);
		obtype = (((*(short *)(ROB_TYPE))) & 0xff);
		switch (obtype)
		{
			case G_TEXT:
			case G_BOXTEXT:
			case G_FTEXT:
			case G_FBOXTEXT:
			case G_TITLE:
			case G_ICON:
			case G_IMAGE:
			case G_STRING:
			case G_BUTTON:
				fix_long(ROB_SPEC);
				break;
			default:
				break;
		}
		obflags = (*(short *)(ROB_FLAGS));	/* zap LASTOB's */
		(*(short *)(ROB_FLAGS) = (obflags & ~LASTOB));
	}
}

static void fix_tedinfo(void)
{
	short		ii, i;
	long		psubstruct, tl[2], ls[2];

	for (ii = NUM_TI-1; ii >= 0; ii--)
	{
		psubstruct = get_addr(R_TEDINFO, ii);
		tl[0] = tl[1] = 0x0L;
		if (fix_ptr(R_TEPTEXT, ii) )
		{
			tl[0] = RTE_TXTLEN;
			ls[0] = RTE_PTEXT;
		}
		if (fix_ptr(R_TEPTMPLT, ii) )
		{
			tl[1] = RTE_TMPLEN;
			ls[1] = RTE_PTMPLT;
		}
		for(i=0; i<2; i++)
		{
			if (tl[i])
				(*(short *)(tl[i]) = (strlen((char *)((*(long *)(ls[i])))) + 1));
		}
		fix_ptr(R_TEPVALID, ii);
	}
}

static void fix_iconblk(void)
{
	short		ii;

	for (ii = NUM_IB-1; ii >= 0; ii--)
	{
		fix_ptr(R_IBPMASK, ii);
		fix_ptr(R_IBPDATA, ii);
		fix_ptr(R_IBPTEXT, ii);
	}
}

static void fix_bitblk(void)
{
	short	ii;

	for (ii = NUM_BB-1; ii >= 0; ii--)
		fix_ptr(R_BIPDATA, ii);
}

static void fix_frstr(void)
{
	short	ii;

	for (ii = NUM_FRSTR-1; ii >= 0; ii--)
		fix_ptr(R_FRSTR, ii);
}

static void fix_frimg(void)
{
	short	ii;

	for (ii = NUM_FRIMG-1; ii >= 0; ii--)
		fix_ptr(R_FRIMG, ii);
}

static void fix_all(void)
{
	fix_trindex();
	fix_objects();
	fix_tedinfo();
	fix_iconblk();
	fix_bitblk();
	fix_frstr();
	fix_frimg();
}   

static void comp_str(long hdr)
{
	long	frstr, where, tree, maddr;
	short	istr, nstr, lstr, w, h, obj, ndex;
	char	name[9];

	nstr = (*(unsigned short *)(RSH_NSTRING(hdr)));
	if ( nstr )
	{
		frstr = hdr + ((unsigned short)((*(unsigned short *)(RSH_FRSTR(hdr)))));

		ini_tree(&maddr, NEWPANEL);
		tree = copy_tree(maddr, ROOT, true);
		add_trindex(tree);
		ini_tree(&maddr, FREEPBX);

		for (istr = 0; istr < nstr; istr++)
		{
			where = (*(long *)(frstr + (long) (istr * sizeof(long))));
			if (where != -1L)
			{
				lstr = (short)strlen((char *)(where));
				obj = copy_objs(maddr, PBXSTR, tree, false);
				objc_add((OBJECT  *)tree, ROOT, obj);
				SET_SPEC(tree, obj, where);
				SET_WIDTH(tree, obj, gl_wchar * lstr);
				if ((ndex = find_value((char *) where)) != NIL)
				{
					set_value(ndex, (char *) (tree + 
						(long) (obj * sizeof(OBJECT))));
					set_kind(ndex, OBJKIND);
				}
			}
		}
		SET_WIDTH(tree, ROOT, 1);	/* fool arrange_tree into putting */
		arrange_tree(tree, 2 * gl_wchar, gl_hchar, &w, &h);
		SET_HEIGHT(tree, ROOT, h);	/* each on a different line */
		SET_WIDTH(tree, ROOT, max(w, 20 * gl_wchar));
		unique_name(&name[0], "FRSTR%d", 1);	/* make up a name */
		new_index((char *)tree, FREE, name);
		(*(unsigned short *)(RSH_NSTRING(hdr)) = (0));
	}
}

static void comp_img(long hdr)
{
	long	frimg, where, tree, maddr;
	short	iimg, nimg, w, h, obj, ndex;
	char	name[9];

	nimg = (*(unsigned short *)(RSH_NIMAGES(hdr)));
	if ( nimg )
	{
		frimg = hdr + ((unsigned short)((*(unsigned short *)(RSH_FRIMG(hdr)))));

		ini_tree(&maddr, NEWPANEL);
		tree = copy_tree(maddr, ROOT, true);
		add_trindex(tree);
		ini_tree(&maddr, FREEPBX);

		for (iimg = 0; iimg < nimg; iimg++)
		{
			where = (*(long *)(frimg + (unsigned short) (iimg * sizeof(long))));
			obj = copy_objs(maddr, PBXIMG, tree, false);
			objc_add((OBJECT  *)tree, ROOT, obj);
			SET_SPEC(tree, obj, where);
			SET_HEIGHT(tree, obj, (*(short *)(BI_HL(where))));
			SET_WIDTH(tree, obj, (*(short *)(BI_WB(where))) << 3);
			if ((ndex = find_value((char *) where)) != NIL)
			{
				set_value(ndex, (char *) (tree + 
					(unsigned short) (obj * sizeof(OBJECT))));
				set_kind(ndex, OBJKIND);
			}
		}
		SET_WIDTH(tree, ROOT, full.g_w);	
		arrange_tree(tree, 2 * gl_wchar, gl_hchar, &w, &h);
		SET_HEIGHT(tree, ROOT, h);	
		SET_WIDTH(tree, ROOT, w);
		map_tree(tree, ROOT, NIL, (fkt_parm)trans_obj);
		unique_name(&name[0], "FRIMG%d", 1);	/* make up a name */
		new_index((char *)tree, FREE, name);
		(*(unsigned short *)(RSH_NIMAGES(hdr)) = (0));
	}
}

short read_files(void)
{
	short	ii;
	short	ntree, nobj;
	char	name[9];

	if ( ad_clip ) 
		clr_clip();
	ini_buff();
	if ( !dmcopy(rcs_rhndl, 0x0L, head, (short)sizeof(RSHDR)))
	{
		fclose(rcs_rhndl);
		ini_buff();
		return (false);
	}
	else if (avail_mem() < ((unsigned short)((*(unsigned short *)(RSH_RSSIZE(head)))))) 
	{
		hndl_alert(1, string_addr(STNROOM) );
		ini_buff();
		return (false);
	}
	else
	{
		if (!dmcopy(rcs_rhndl, (long)sizeof(RSHDR),
				head + sizeof(RSHDR),
				(*(unsigned short *)(RSH_RSSIZE(head))) - (short)sizeof(RSHDR)))
		{
			fclose(rcs_rhndl);
			ini_buff();
			return(false);
		}
		else
		{
			fclose(rcs_rhndl);
			rcs_free = (head + ((unsigned short)((*(unsigned short *)(RSH_RSSIZE(head))))) );

			if (rcs_free & 0x1) 
	    		rcs_free++;

			rs_hdr = head;
			fix_all();
			map_all((fkt_parm)trans_obj);

			if (!rcs_dhndl)
				rcs_ndxno = 0;
			else
			{
				read_68k( false );
				fclose(rcs_dhndl);
			}
			/* convert stored values to addresses */
			for (ii = 0; ii < rcs_ndxno; ii++)
			{
				switch ( get_kind(ii) )
				{
					case UNKN:
					case PANL:
					case DIAL:
					case MENU:
						set_value(ii, (char *) tree_addr(
							(short) get_value(ii)) );
						break;
					case ALRT:
					case FRSTR:
						set_value(ii, (char *) str_addr(
							(short) get_value(ii)) );
						break;
					case FRIMG:
						set_value(ii, (char *) img_addr(
							(short) get_value(ii)) );
						break;
					case OBJKIND:
						nobj = (short) (((unsigned short) get_value(ii)) & 0xff) & 0xff;
						ntree = (short) ((((unsigned short) get_value (ii)) >> 8) & 0xff) & 0xff;
						set_value(ii, (char *) (tree_addr(ntree) + 
							(unsigned short) (nobj * sizeof(OBJECT))));
						break;
					default:
						break;
				}
			}

			comp_alerts(head);	/* convert freestrs into alert trees */
			comp_str(head);	/* scavenge all other freestrs	*/
			comp_img(head);	/* and likewise for free images */
			/* make sure all trees are indexed */
			for (ii = 0; ii < (*(unsigned short *)(RSH_NTREE(head))); ii++)
				if (find_tree(ii) == NIL)
				{
					unique_name(name, "TREE%d", ii + 1);
					new_index((char *) tree_addr(ii), UNKN, name);
				}

			return (*(unsigned short *)(RSH_RSSIZE(head)));
		}
	}
}

short merge_files(void)
{
	unsigned short	foo, indoff;
	short	total, i, ii;
	short	nobj, ntree, old_ntree;
	long	merge, oldndx, newndx, old_ndxno, frstr, frimg, here;

	merge = get_mem((short)sizeof(RSHDR));
	if (!dmcopy(rcs_rhndl, 0x0L, merge, (short)sizeof(RSHDR) ))
	{ 
		fclose(rcs_rhndl);
		return(false);
	}
	else
	{
		if (rcs_low || avail_mem() < (unsigned short) (*(unsigned short *)(RSH_RSSIZE(merge))) - 
			(unsigned short) sizeof(RSHDR) )
		{
			hndl_alert(1, string_addr(STNROOM));
			return(false);
		}
		else
		{
			/* synchronize objects */
			foo = (unsigned short) (( merge + ((unsigned short)((*(unsigned short *)(RSH_OBJECT(merge))))) ) -
				( head + ((unsigned short)((*(unsigned short *)(RSH_OBJECT(head))))) ));
			foo %= (short)sizeof(OBJECT);
			if (foo) 
			{
				get_mem(foo = (short)(sizeof(OBJECT) - foo));
				merge = merge + foo;
				dmcopy(rcs_rhndl, 0x0L, merge, (short)sizeof(RSHDR));
			}

			here = get_mem((unsigned short) (*(unsigned short *)(RSH_RSSIZE(merge))) - 
				(unsigned short) sizeof(RSHDR));
			if (!dmcopy (rcs_rhndl, (long) sizeof(RSHDR), 
				here, (*(unsigned short *)(RSH_RSSIZE(merge)))))
			{
				fclose(rcs_rhndl);
				return(false);
			}
			else
			{
				fclose(rcs_rhndl);
				rs_hdr = merge;
				fix_all();

				old_ntree = (*(unsigned short *)(RSH_NTREE(head)));
				if ((*(unsigned short *)(RSH_NTREE(merge))))
				{
					total = (*(unsigned short *)(RSH_NTREE(head))) + (*(unsigned short *)(RSH_NTREE(merge)));
					newndx = get_mem((short)(sizeof(long) * total));
					indoff = (unsigned short) (newndx - head);		
					for (i = 0; i < (*(unsigned short *)(RSH_NTREE(head))); i++)
					{
						(*(long *)(newndx) = (tree_addr(i)));
						newndx += sizeof(long);
					}
					oldndx = merge + ((unsigned short)((*(unsigned short *)(RSH_TRINDEX(merge)))));
					for (i = 0; i < (*(unsigned short *)(RSH_NTREE(merge))); i++)
					{
						(*(long *)(newndx) = ((*(long *)(oldndx))));
						newndx += sizeof(long);
						oldndx += sizeof(long);
					}
					(*(unsigned short *)(RSH_NTREE(head)) = (total));
					(*(unsigned short *)(RSH_TRINDEX(head)) = (indoff));
				}

				if (rcs_dhndl)
				{  
					{	
						old_ndxno = rcs_ndxno;
						read_68k( true );
						total = rcs_ndxno;
						rcs_ndxno = (short)old_ndxno;
					}
					for ( ii = (short)old_ndxno; ii < total; ii++ )				
					{	
						if (find_name(&rcs_index[ii].name[0]) != NIL)
							if (tree_kind(rcs_index[ii].kind))
								unique_name(&rcs_index[ii].name[0], "TREE%d", old_ntree);
							else
								unique_name(&rcs_index[ii].name[0], "OBJ%d", 0);
						switch (rcs_index[ii].kind)
						{
							case UNKN:
							case PANL:
							case DIAL:
							case MENU:
								rcs_index[ii].val = (char *) tree_addr(
									(short) rcs_index[ii].val + old_ntree);
								break;
							case ALRT:
							case FRSTR:
								frstr = merge +	((unsigned short)((*(unsigned short *)(RSH_FRSTR(merge)))));
								rcs_index[ii].val = (char *) (*(long *)(frstr + 
								(unsigned short) (sizeof(long) * (short) rcs_index[ii].val)));
								break;
							case FRIMG:
								frimg = merge + ((unsigned short)((*(unsigned short *)(RSH_FRIMG(merge)))));
								rcs_index[ii].val = (char *) (*(long *)(frimg +
									(unsigned short) (sizeof(long) * (short)rcs_index[ii].val)));
								break;
							case OBJKIND:
								nobj = (short) (((unsigned short) rcs_index[ii].val) & 0xff) & 0xff;
								ntree = (short) ((((unsigned short) rcs_index[ii].val) >> 8) & 0xff) & 0xff;
								rcs_index[ii].val = (char *) (tree_addr(ntree + old_ntree) 
									+ (unsigned short) (nobj * sizeof(OBJECT))); 
								break;
							default:
								break;
						}
						if (new_index(rcs_index[ii].val, rcs_index[ii].kind, &rcs_index[ii].name[0]) == NIL)
							break;
					}
					fclose(rcs_dhndl);
				}

				for (i = old_ntree; i < (*(unsigned short *)(RSH_NTREE(head))); i++)
				{
					map_tree(tree_addr(i), ROOT, NIL, (fkt_parm)trans_obj); 
					if (find_tree(i) == NIL)
					{
						unique_name(&rcs_index[i].name[0], "TREE%d", i + 1);
						if (new_index((char *) tree_addr(i), UNKN, &rcs_index[i].name[0]) == NIL)
							break;
					}
				}

				comp_alerts(merge);
				comp_str(merge);
				comp_img(merge);
				return (true);
			}
		}
	}
}
