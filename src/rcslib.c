/*	RCSLIB.C	10/11/84 - 1/25/85 	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include "rcsmono.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include "rcsdefs.h"
#include "rcsvdi.h"
#include "rcslib.h"
#include "rcsaddr.h"

extern	MFDB	gl_dst;

void set_slsize(short w_handle, short h_size, short v_size)
{
	short	cur_size, foo;

	wind_get(w_handle, WF_HSLSIZE, &cur_size, &foo, &foo, &foo);
	if (cur_size != h_size)
		wind_set(w_handle, WF_HSLSIZE, h_size, 0, 0, 0);
	wind_get(w_handle, WF_VSLSIZE, &cur_size, &foo, &foo, &foo);
	if (cur_size != v_size)
		wind_set(w_handle, WF_VSLSIZE, v_size, 0, 0, 0);
}	

void set_slpos(short w_handle, short h_pos, short v_pos)
{
	short	cur_pos, foo;

	wind_get(w_handle, WF_HSLIDE, &cur_pos, &foo, &foo, &foo);
	if (cur_pos != h_pos)
		wind_set(w_handle, WF_HSLIDE, h_pos, 0, 0, 0);
	wind_get(w_handle, WF_VSLIDE, &cur_pos, &foo, &foo, &foo);
	if (cur_pos != v_pos)
		wind_set(w_handle, WF_VSLIDE, v_pos, 0, 0, 0);
}

short ini_tree(long *tree, short which)	/* find tree address */
{
	return ( rsrc_gaddr(R_TREE, which, tree) );
}

long image_addr(short which)
{
	long	where;

	rsrc_gaddr(R_IMAGEDATA, which, &where);
	return (where);
} 

long string_addr(short which)
{
	long	where;

	rsrc_gaddr(R_STRING, which, &where);
	return (where);
} 

void arrange_tree(long tree, short wint, short hint,short *maxw, short *maxh)
{
	short	obj, x, y, rowh, wroot;

	wroot = GET_WIDTH(tree, ROOT);
	if ( wroot )
	{
		x = wint;
		y = hint;
		rowh = 0;
		*maxw = 0;

		for (obj = GET_HEAD(tree, ROOT); obj != ROOT; 
			obj = GET_NEXT(tree, obj))
		{
			if (rowh && (x + GET_WIDTH(tree, obj)) > wroot)
			{
				x = wint;
				y += (rowh + hint);
				rowh = 0;
			}
			SET_X(tree, obj, x);
			SET_Y(tree, obj, y);
			if ( !(GET_FLAGS(tree, obj) & HIDETREE) )
			{
				x += (GET_WIDTH(tree, obj) + wint); 
				*maxw = max(*maxw, x);
				rowh = max(rowh, GET_HEIGHT(tree, obj));
			}
		}
		*maxh = y + rowh + hint;
	}
}

/*
*	Routine that will find the parent of a given object.  The
*	idea is to walk to the end of our siblings and return
*	our parent.  If object is the root then return NIL as parent.
*/
short get_parent(long tree, short obj)
{
	short		pobj;

	if (obj == NIL)
		return (NIL);
	else
	{
		pobj = GET_NEXT(tree, obj);
		if (pobj != NIL)
		{
			while( GET_TAIL(tree, pobj) != obj ) 
			{
				obj = pobj;
				pobj = GET_NEXT(tree, obj);
			}
		}
		return(pobj);
	}
} /* get_parent */

short nth_child(long tree, short nobj, short n)
{
	for (nobj = GET_HEAD(tree, nobj); --n; nobj = GET_NEXT(tree, nobj))
		;
	return (nobj);
}

short child_of(long tree, short nobj, short obj)
{
	short	nbar;

	nobj = GET_HEAD(tree, nobj);
	for (nbar = 1; nobj != obj; nbar++)
		nobj = GET_NEXT(tree, nobj);
	return (nbar);
}

void table_code(long tree, short obj0, short *types, short ntypes, short type)
{
	short	itype;

	for (itype = 0; itype < ntypes; itype++)
		if (type == types[itype])
		{
			sel_obj(tree, obj0 + itype);
			return;
		}
}

short encode(long tree, short ob1st, short num)
{
	for (; num--; )
		if (GET_STATE(tree, ob1st+num) & SELECTED)
			return(num);
	return (0);
}

void map_tree(long tree, short this, short last, fkt_parm routine)
{
	short		tmp1;

	/* non-recursive tree traversal */
child:
	/* see if we need to to stop */
	if (this == last)
		return;
	/* do this object */
	(*routine)(tree, this);
	/* if this guy has kids then do them */
	tmp1 = GET_HEAD(tree, this);
	if ( tmp1 != NIL )
	{
		this = tmp1;
		goto child;
	}

sibling:
	/* if this obj. has a	*/
	/*   sibling that is not*/
	/*   his parent, then	*/
	/*   move to him and do	*/
	/*   him and his kids	*/
	tmp1 = GET_NEXT(tree, this);
	if ( (tmp1 == last) || (tmp1 == NIL) )
		return;
	if ( GET_TAIL(tree, tmp1) != this )
	{
		this = tmp1;
		goto child;
	}
	/* if this is the root	*/
	/*   which has no parent*/
	/*   then stop else 	*/
	/*   move up to the	*/
	/*   parent and finish	*/
	/*   off his siblings	*/ 
	this = tmp1;
	goto sibling;
}

void snap_xy(short *x, short *y)
{
	*x += gl_wchar / 2;	
	*y += gl_hchar / 2;
	*x -= *x % gl_wchar;
	*y -= *y % gl_hchar;
}

void snap_wh(short *w, short *h)
{
	*w += gl_wchar / 2;
	*h += gl_hchar / 2;
	*w = max(*w - *w % gl_wchar, gl_wchar);
	*h = max(*h - *h % gl_hchar, gl_hchar);
}

void text_wh(long taddr, short type, short *w, short *h)
{
	short	font;

	font = (*(short *)RP(TE_FONT(taddr)));
	taddr = (RGET((type == G_TEXT || type == G_BOXTEXT)? TE_PTEXT(taddr): TE_PTMPLT(taddr)));
	*h = ch_height(font);
	*w = ch_width(font) * (short)strlen((char *)RP(taddr));
}

void icon_wh(long taddr, short *w, short *h)
{
	ICONBLK	here;
	GRECT	p;

	memmove((void *)(&here), (void *)RP(taddr), sizeof(ICONBLK));
	rc_copy((GRECT *) &here.ib_xchar, &p);
	rc_union((GRECT *) &here.ib_xicon, &p);
	rc_union((GRECT *) &here.ib_xtext, &p);
	*w = p.g_x + p.g_w;
	*h = p.g_y + p.g_h;
}

void icon_tfix(long taddr)
{
	short	dw;

	dw = ((*(short *)RP(IB_WICON(taddr))) - (*(short *)RP(IB_WTEXT(taddr)))) / 2;
	(*(short *)RP(IB_XICON(taddr)) = ((dw<0)? -dw: 0));
	(*(short *)RP(IB_XTEXT(taddr)) = ((dw<0)? 0: dw));
}

void trans_bitblk(long obspec)
{
	long	taddr;
	short	wb, hl;

	if ( (taddr = (RGET(BI_PDATA(obspec)))) != -1L)
	{
		wb = (*(short *)RP(BI_WB(obspec)));
		hl = (*(short *)RP(BI_HL(obspec)));
		gsx_trans(taddr, wb, taddr, wb, hl);
	}
}

void rast_op(short mode, GRECT *s_area, MFDB *s_mfdb, GRECT *d_area, MFDB *d_mfdb)
{
	short	pxy[8];

	/* the images of the icon editor are one-plane images in memory (see
	   rcsmono.h): between them, and to and from the screen, they are done
	   by the editor, whatever the screen looks like */
	if (is_mono(d_mfdb))
	{
		if (mode == 0 || mode == 15)
			mono_rop(mode, s_area, NULL, d_area, d_mfdb);
		else if (is_mono(s_mfdb))
			mono_rop(mode, s_area, s_mfdb, d_area, d_mfdb);
		else
			scrn_to_mono(s_area, d_mfdb, d_area, edit_colour());
		return;
	}
	if (is_mono(s_mfdb))
	{
		mono_to_scrn(s_area, s_mfdb, d_area, edit_colour());
		return;
	}
	grect_to_array(s_area, pxy);
	grect_to_array(d_area, &pxy[4]);
	vro_cpyfm( gl_handle, mode, pxy, s_mfdb, d_mfdb);
}

void outl_obj(long tree, short obj, GRECT *clprect)
{
	GRECT	p;

	objc_xywh(tree, obj, &p);
	rc_intersect( clprect, &p);
	graf_mouse(M_OFF, NULL);
	gsx_outline(&p);
	graf_mouse(M_ON, NULL);
}

void invert_obj(long tree, short obj, GRECT *clprect)
{
	GRECT	hot;

	objc_xywh(tree, obj, &hot);
	rc_intersect( clprect, &hot );
	graf_mouse(M_OFF, NULL);
	gsx_invert(&hot);
	graf_mouse(M_ON, NULL);
	if (iconedit_flag)
	{
		if ((*(short *)RP(OB_STATE(obj))) == SELECTED)
		    (*(short *)RP(OB_STATE(obj)) = (NORMAL));
		else
		    (*(short *)RP(OB_STATE(obj)) = (SELECTED));
	}
}

void trans_obj(long tree, short obj)
{
	short	type, wb, hl;
	long	taddr, obspec;

	type = ((GET_TYPE(tree, obj)) & 0xff);
	if ( (obspec = GET_SPEC(tree, obj)) != -1L)
	{
		switch (type)
		{
			case G_IMAGE:
				trans_bitblk(obspec);
				break;
			case G_ICON:
				hl = (*(short *)RP(IB_HICON(obspec)));
				wb = ((*(short *)RP(IB_WICON(obspec))) + 7) >> 3;
				if ( (taddr = (RGET(IB_PDATA(obspec)))) != -1L)
					gsx_trans(taddr, wb, taddr, wb, hl);
				if ( (taddr = (RGET(IB_PMASK(obspec)))) != -1L)
					gsx_trans(taddr, wb, taddr, wb, hl);
				break;
			default:
				break;
		}
	}
}

void unfix_chpos(long where, short x_or_y)
{
	short	cpos, coff, cbits;

	cpos = (*(short *)RP(where));
	coff = cpos / (x_or_y? gl_hchar: gl_wchar);
	cbits = cpos % (x_or_y? gl_hchar: gl_wchar);
	(*(short *)RP(where) = (coff | (cbits << 8)));
}

static void fix_chpos(long pfix, short ifx)
{
	short	coffset;
	short	cpos;

	cpos = (*(short *)RP(pfix));
	coffset = (short) (((cpos) >> 8) & 0xff) & 0xff;	/* Alcyon bug ! */
	cpos = (short) ((cpos) & 0xff) & 0xff;
	if (ifx)
		cpos *= gl_wchar;
	else
		cpos *= gl_hchar;
	if ( coffset > 128 )			/* crude	*/
		cpos -= (256 - coffset);
	else
		cpos += coffset;
	(*(short *)RP(pfix) = (cpos));
}

void rs_obfix(long tree, short curob)
{
	short		i, val;
	long		p;

	/* set X,Y,W,H with	*/
	/*   fixch, use val	*/
	/*   to alternate TRUEs	*/
	/*   and FALSEs		*/
	p = OB_X(curob);

	val = true;
	for (i=0; i<4; i++)
	{
		fix_chpos(p+(long)(2*i), val);
		val = !val;
	}
}

static void undo_obj(long tree, short which, short bit)
{
	short	state;

	state = GET_STATE(tree, which);
	SET_STATE(tree, which, state & ~bit);
}

static void do_obj(long tree, short which, short bit)
{
	short	state;

	state = GET_STATE(tree, which);
	SET_STATE(tree, which, state | bit);
}

void enab_obj(long tree, short which)
{
	undo_obj(tree, which, DISABLED);
}

void disab_obj(long tree, short which)
{
	do_obj(tree, which, DISABLED);
}

void sel_obj(long tree, short which)
{
	do_obj(tree, which, SELECTED);
}

void desel_obj(long tree, short which)
{
	undo_obj(tree, which, SELECTED);
}

void chek_obj(long tree, short which)
{
	do_obj(tree, which, CHECKED);
}

void unchek_obj(long tree, short which)
{
	undo_obj(tree, which, CHECKED);
}

static void unflag_obj(long tree, short which, short bit)
{
	short	flags;

	flags = GET_FLAGS(tree, which);
	SET_FLAGS(tree, which, flags & ~bit);
}

static void flag_obj(long tree, short which, short bit)
{
	short	flags;

	flags = GET_FLAGS(tree, which);
	SET_FLAGS(tree, which, flags | bit);
}

void hide_obj(long tree, short which)
{
	flag_obj(tree, which, HIDETREE);
}

void unhide_obj(long tree, short which)
{
	unflag_obj(tree, which, HIDETREE);
}

static void indir_obj(long tree, short which)
{
	flag_obj(tree, which, INDIRECT);
}

static void dir_obj(long tree, short which)
{
	unflag_obj(tree, which, INDIRECT);
}

void sble_obj(long tree, short which)
{
	flag_obj(tree, which, SELECTABLE);
}

void unsble_obj(long tree, short which)
{
	unflag_obj(tree, which, SELECTABLE);
}

void objc_xywh(long tree, short obj, GRECT *p)
{
	objc_offset((OBJECT  *)RP(tree), obj, &p->g_x, &p->g_y);
	p->g_w = GET_WIDTH(tree, obj);
	p->g_h = GET_HEIGHT(tree, obj);
}

void ob_setxywh(long tree, short obj, GRECT *pt)
{
	memmove((void *)RP(OB_X(obj)), (void *)(((long)(pt))), 2 * (sizeof(GRECT)/2));
}

void ob_relxywh(long tree, short obj, GRECT *pt)
{
	memmove((void *)(((long)(pt))), (void *)RP(OB_X(obj)), 2 * (sizeof(GRECT)/2));
}

short min(short a, short b)
{
	return( (a < b) ? a : b );
}

short max(short a, short b)
{
	return( (a > b) ? a : b );
}

void strup(char *p1)
{
	for(; *p1; p1++)
		*p1 = toupper(*p1);
}

void LLSTRCPY(long src, long dest)
{
	char	b;

	do {
		b = (*(char *)RP(src++));
		(*(char *)RP(dest++) = (b));
	} while (b);
}

short LLSTRCMP(long l1, long l2)
{
	char	b;

	b = (*(char *)RP(l1++));
	while (b)
	{
		if (b != (*(char *)RP(l2++)))
			return (false);
		else
			b = (*(char *)RP(l1++));
	}
	return (!(*(char *)RP(l2)));
}

void LWINC(long x)
{
	(*(short *)RP(x) = (1 + (*(short *)RP(x))));
}

short dmcopy(FILE *stream, long msrc, long mdest, long mln)	/* disk to memory copy */
{
	if (fseek(stream, msrc, SEEK_SET))
		return( 0 );
	else if ( fread( (void *)RP(mdest), 1, mln, stream) < (size_t)mln)
		return( 0 );
	else
		return(mln != 0);
} /* dmcopy */

static long obj_addr(long tree, short obj, short offset)
{
	return (tree + (unsigned short) (obj * sizeof(OBJECT) + offset));
}

short GET_NEXT(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 0)));
}

short GET_HEAD(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 2)));
}

short GET_TAIL(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 4)));
}

short GET_TYPE(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 6)));
}

short GET_FLAGS(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 8)));
}

short GET_STATE(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 10)));
}

long GET_SPEC(long tree, short x)
{
	return (RGET(obj_addr(tree, x, 12)));
}

short GET_X(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 16)));
}

short GET_Y(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 18)));
}

short GET_WIDTH(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 20)));
}

short GET_HEIGHT(long tree, short x)
{
	return (*(short *)RP(obj_addr(tree, x, 22)));
}

void SET_NEXT(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 0)) = (val));
}

void SET_HEAD(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 2)) = (val));
}

void SET_TAIL(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 4)) = (val));
}

static void SET_TYPE(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 6)) = (val));
}

void SET_FLAGS(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 8)) = (val));
}

void SET_STATE(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 10)) = (val));
}

void SET_SPEC(long tree, short x, long val)
{
	(RFIELD(obj_addr(tree, x, 12)) = (val));
}

void SET_X(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 16)) = (val));
}

void SET_Y(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 18)) = (val));
}

void SET_WIDTH(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 20)) = (val));
}

void SET_HEIGHT(long tree, short x, short val)
{
	(*(short *)RP(obj_addr(tree, x, 22)) = (val));
}

/* the GRECT helpers that the original build took from RCLIB.LIB; gemlib has
 * rc_copy, rc_equal and rc_intersect, but not these */

void rc_set(GRECT *r, short x, short y, short w, short h)
{
	r->g_x = x;
	r->g_y = y;
	r->g_w = w;
	r->g_h = h;
}

void rc_get(const GRECT *r, short *x, short *y, short *w, short *h)
{
	*x = r->g_x;
	*y = r->g_y;
	*w = r->g_w;
	*h = r->g_h;
}

short rc_inside(const GRECT *r, short x, short y)
{
	return x >= r->g_x && y >= r->g_y &&
		x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

/* the smallest rectangle holding both, in dst */
void rc_union(const GRECT *src, GRECT *dst)
{
	short x, y;

	x = src->g_x < dst->g_x ? src->g_x : dst->g_x;
	y = src->g_y < dst->g_y ? src->g_y : dst->g_y;
	dst->g_w = (src->g_x + src->g_w > dst->g_x + dst->g_w ?
		src->g_x + src->g_w : dst->g_x + dst->g_w) - x;
	dst->g_h = (src->g_y + src->g_h > dst->g_y + dst->g_h ?
		src->g_y + src->g_h : dst->g_y + dst->g_h) - y;
	dst->g_x = x;
	dst->g_y = y;
}
