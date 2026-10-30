/*	RCSEDIT.C	12/26/84 - 1/25/85  	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcslib.h"
#include "rcsdata.h"
#include "rcsintf.h"
#include "rcsmain.h"
#include "rcsobjs.h"
#include "rcsmenu.h"
#include "rcsalert.h"
#include "rcsedit.h"

void set_text(long tree, short obj, long addr, short len)
{
	long	taddr;

	taddr = GET_SPEC(tree, obj);
	(*(long *)(TE_PTEXT(taddr)) = (addr));
	(*(short *)(TE_TXTLEN(taddr)) = (len));
}

void ted_set(long taddr, char *tmplt, char *valid, char *text)
{
	short	imap, itmp;

	if (!tmplt[0])
	{
		tmplt[0] = '@';
		LLSTRCPY((*(long *)(TE_PVALID(taddr))), (long)((long)(valid)));
		LLSTRCPY((*(long *)(TE_PTEXT(taddr))), (long)((long)(text)));
		if (!valid[0])
			valid[0] = '@';
		if (!text[0])
			text[0] = '@';
	}
	else
	{
		for (imap = itmp = 0; tmplt[itmp]; itmp++)
			if (tmplt[itmp] == '_')
			{
				tmplt[itmp] = '~';
				valid[itmp] = (*(char *)(imap + (*(long *)(TE_PVALID(taddr)))));
				text[itmp] = (*(char *)(imap + (*(long *)(TE_PTEXT(taddr)))));
				imap++;
			}
			else
				valid[itmp] = text[itmp] = '~';
		valid[itmp] = text[itmp] = '\0';
	}
}

void ted_get(long taddr, char *tmplt, char *valid, char *text)
{
	short	imap, itmp;

	if (tmplt[0] == '@')
		tmplt[0] = '\0';
	else
	{
		for (imap = itmp = 0; tmplt[itmp]; itmp++)
			if (tmplt[itmp] == '~')
			{
				tmplt[itmp] = '_';
				valid[imap] = valid[itmp];
				text[imap] = text[itmp];
				imap++;
			}
		valid[imap] = text[imap] = '\0';
	}

	if (valid[0] == '@')
		valid[0] = '\0';
    /*	if (text[0] == '@')
		text[0] = '\0';    allow null string as space holder */
	update_if(TE_PTMPLT(taddr), tmplt);
	update_if(TE_PVALID(taddr), valid);
	update_if(TE_PTEXT(taddr), text);
	(*(short *)(TE_TMPLEN(taddr)) = (strlen(tmplt) + 1));
	(*(short *)(TE_TXTLEN(taddr)) = (strlen(text) + 1));
}

void icon_set(long taddr, char *text, char *chr)
{
	LLSTRCPY((*(long *)(IB_PTEXT(taddr))), (long)((long)(text)));
	if (!text[0])
		text[0] = '@';
	chr[0] = (((*(short *)(IB_CHAR(taddr)))) & 0xff); 
	if (!chr[0])
		chr[0] = '@';
	chr[1] = '\0';
}

void icon_get(long taddr, char *text, char *chr)
{
	if (text[0] == '@')
		text[0] = '\0';
	update_if(IB_PTEXT(taddr), text);
	if (chr[0] == '@')
		chr[0] = '\0';
	(*(short *)(IB_CHAR(taddr)) = ((*(short *)((IB_CHAR(taddr)) & 0xff00)) | (unsigned short) chr[0]));
} 

short icon_cwhich(short xy, short wh, short iwh)
{
	if (!xy)
		return (0);
	else if (xy == iwh - wh)
		return (2);
	else
		return(1);
}

short icon_twhich(short xy, short iwh)
{
	if (!xy)
		return (0);
	else if (xy == iwh) 
		return (2);
	else
		return(1);
}

short icon_tapply(short rule, short wh, short iwh)
{
	switch (rule)
	{
		case 0:
			return ( 0 );
		case 1:
			return (iwh - wh) / 2;
		case 2:
			return (iwh);
		default:
			return (0);
	}
}

short icon_capply(short rule, short wh, short iwh)
{
	switch (rule)
	{
		case 0:
			return (0);
		case 1:
			return (iwh - wh) / 2;
		case 2:
			return (iwh - wh);
		default:
			return (0);
	}
} 

void get_obf(long tree, short obj, short *type, long *obspec)
{
	*type = ((GET_TYPE(tree, obj)) & 0xff);
	*obspec = GET_SPEC(tree, obj);
}

void get_fields(long tree, short sobj, short *type, long *obspec, GRECT *p)
{
	*type = ((GET_TYPE(tree, sobj)) & 0xff);
	*obspec = OB_SPEC(sobj);	/* Notice that this is the ADDR of */
	objc_xywh(tree, sobj, p);	/* the OB_SPEC, not the value! */
}

void edit_str(long tree, short sobj)
{
	long	obspec; 
	short	where, type, exitobj, ok;
	GRECT	p;
	short	min_width, neww, len; 
	char	text[73], name[9]; 

	get_fields(tree, sobj, &type, &obspec, &p);
	len = (short)strlen((char *)((*(long *)(obspec))));
	if (len <= 72)
	{
		ini_tree(&tree, STRDIAL);
		where = set_obname(tree, STRNAME, name, ad_view, sobj);
		set_text(tree, OSTRITEM, (long)((long)(&text[0])), 73);
		LLSTRCPY((*(long *)(obspec)), (long)((long)(&text[0])));
		if ( rcs_state == ALRT_STATE )
			hide_obj( tree, STRNAME);
		if (!text[0])
			text[0] = '@';
		do {
			exitobj = hndl_dial(tree, OSTRITEM, &p);
			desel_obj(tree, exitobj);
			ok = DEFAULT & GET_FLAGS(tree, exitobj);
		} while ( ok && !name_ok(name, where, true));

		if (ok)
		{
			rcs_edited = true;
			get_obname(name, ad_view, sobj);
			if (text[0] == '@')
				text[0] = '\0';

			if (rcs_state == ALRT_STATE)
			{		
				if (strlen(text) > 40 )
				{
					hndl_alert(1, string_addr(STOOLONG));
					text[40] = '\0';
				}
				update_if(obspec, &text[0]);
				fix_alert(ad_view);
				unhide_obj(tree, STRNAME);
			}
			else
			{
				neww = (short)(gl_wchar * strlen(text));
				if (rcs_state == MENU_STATE)
				{
					if (type == G_TITLE)
					{
						if (!newsize_obj(ad_view, sobj, neww,gl_hchar, true))
							text[len] = '\0';
						fix_menu_bar(ad_view);
					}
					else if (in_which_menu(ad_view, sobj) == 1
						&& in_menu(ad_view, sobj) == 1
						&& strlen(text) > 20 )
		    		{
						hndl_alert(1, string_addr(STOOLONG));	
						text[20] = '\0';
					}
					else  if(!newsize_obj(ad_view, sobj, neww,gl_hchar, false))
						text[len] = '\0';
				}
				else if (type == G_STRING || type == G_BUTTON )
				{
					min_width = GET_WIDTH(ad_view, sobj);
					if( neww > min_width )
						if(!newsize_obj(ad_view, sobj, neww,gl_hchar, false))
							text[len] = '\0';
				}
			}
			if( rcs_state != ALRT_STATE)
				update_if(obspec, &text[0]);
		}
	}
}

void edit_box(long tree, short sobj)
{
	long	obspec; 
	short	where, type, exitobj, ok, nilok;
	char	name[9], text[2], bxchar;
	GRECT	p;

	if (rcs_state != ALRT_STATE)
	{
		get_fields(tree, sobj, &type, &obspec, &p);

		ini_tree(&tree, BOXDIAL);
		where = set_obname(tree, BOXNAME, name, ad_view, sobj);
		if ( where != NIL && tree_view() && tree_kind( get_kind(where)) )
			nilok = false;
		else
			nilok = true;
		bxchar = (((((((*(long *)(obspec))) >> 16) & 0xffff)) >> 8) & 0xff);
		set_text(tree, OCHRITEM, (long)((long)(&text[0])), 2);
		text[0] = bxchar? bxchar: '@';
		text[1] = '\0';

		do {
			exitobj = hndl_dial(tree, OCHRITEM, &p);
			desel_obj(tree, exitobj);
			ok = DEFAULT & GET_FLAGS(tree, exitobj);
		} while (ok && !name_ok(name, where, nilok));

		if (ok)
		{
			rcs_edited = true;
			get_obname(name, ad_view, sobj);
			bxchar = (text[0] == '@')? '\0': text[0];
			(*(long *)(obspec) = (((*(long *)(obspec)) & 0xffffffL) | 
				((long) ((unsigned short) bxchar) << 24)));
		}
	}
}

void edit_text(long tree, short sobj)
{
	long	obspec, taddr; 
	short	min_width, where, type, deftext, w, h, exitobj, ok;
	GRECT	p;
	char	text[73], valid[73], tmplt[73], name[9]; 

	if (rcs_state != ALRT_STATE)
	{
		get_fields(tree, sobj, &type, &obspec, &p);
		taddr = (*(long *)(obspec));
		if (type == G_FTEXT || type == G_FBOXTEXT)
			if (strlen((char *)((*(long *)(TE_PTMPLT(taddr))))) > 72)
				return;
		ini_tree(&tree, TEXTDIAL);
		where = set_obname(tree, TEXTNAME, name, ad_view, sobj);
		set_text(tree, OTMPITEM, (long)((long)(&tmplt[0])), 73);
		set_text(tree, OVALITEM, (long)((long)(&valid[0])), 73);
		set_text(tree, OTEXITEM, (long)((long)(&text[0])), 73);
		LLSTRCPY((*(long *)(TE_PTMPLT(taddr))), (long)((long)(&tmplt[0])));
		ted_set(taddr, &tmplt[0], &valid[0], &text[0]);

		if (type == G_TEXT || type == G_BOXTEXT)
		{
			if (strlen((char *)((*(long *)(TE_PTEXT(taddr))))) > 72)
				return;
			hide_obj(tree, TMPLTTAG);
			hide_obj(tree, OTMPITEM);
			hide_obj(tree, VALIDTAG);
			hide_obj(tree, OVALITEM);
			deftext = OTEXITEM;
		}		   
		else
			deftext = (tmplt[0] != '@')? OTMPITEM: OTEXITEM;
		do {
			exitobj = hndl_dial(tree, deftext, &p);
			desel_obj(tree, exitobj);
			ok = DEFAULT & GET_FLAGS(tree, exitobj);
		} while (ok && !name_ok(name, where, true));

		if (ok)
		{
			rcs_edited = true;
			get_obname(name, ad_view, sobj);
			ted_get(taddr, &tmplt[0], &valid[0], &text[0]);
			if (type == G_TEXT || type == G_FTEXT || type == G_BOXTEXT)
			{
				text_wh(taddr, type, &w, &h);
				min_width = GET_WIDTH(ad_view, sobj);
				if ( w > min_width)
					newsize_obj(ad_view, sobj, w, h, true);
			}
		}

		unhide_obj(tree,TMPLTTAG);
		unhide_obj(tree,OTMPITEM);
		unhide_obj(tree, VALIDTAG);
		unhide_obj(tree,OVALITEM);
		map_tree(tree, ROOT, NIL, (fkt_parm)desel_obj);	/* clear radio buttons */
	}
}

void edit_bit(long tree, short sobj)
{
	short	where, exitobj, ok;
	char	name[9];
	GRECT	p;

	if (rcs_state != ALRT_STATE)
	{
		objc_xywh(tree, sobj, &p);
		ini_tree(&tree, BITDIAL);
		where = set_obname(tree, BITNAME, name, ad_view, sobj);

		do {
			exitobj = hndl_dial(tree, 0, &p);
			desel_obj(tree, exitobj);
			ok = DEFAULT & GET_FLAGS(tree, exitobj);
		} while (ok && !name_ok(name, where, true));

		if (ok)
		{
			rcs_edited = true;
			get_obname(name, ad_view, sobj);
		}
	}
}

void edit_icon(long tree, short sobj)
{
	long	obspec, taddr; 
	short	where, type, itmp, exitobj, ok;
	GRECT	p;
	char	text[14], valid[2], name[9]; 
	short	trule, crule, w, h;

	if (rcs_state != ALRT_STATE)
	{
		get_fields(tree, sobj, &type, &obspec, &p);

		ini_tree(&tree, ICONDIAL);
		where = set_obname(tree, ICONNAME, name, ad_view, sobj);
		taddr = (*(long *)(obspec));
		set_text(tree, IBTEXT, (long)((long)(&text[0])), 13);
		set_text(tree, IBCHAR, (long)((long)(&valid[0])), 2);
		icon_set(taddr, &text[0], &valid[0]);
		trule = icon_twhich((*(short *)(IB_YTEXT(taddr))), (*(short *)(IB_HICON(taddr))));
		sel_obj(tree, IBTPOS0 + trule);
		itmp = icon_cwhich((*(short *)(IB_YCHAR(taddr))),
			gl_hschar, (*(short *)(IB_HICON(taddr))));
		crule = icon_cwhich((*(short *)(IB_XCHAR(taddr))),
			gl_wschar, (*(short *)(IB_WICON(taddr))));
		crule += 3 * itmp;
		sel_obj(tree, IBCPOS0 + crule);

		do {
			exitobj = hndl_dial(tree, IBTEXT, &p);
			desel_obj(tree, exitobj);
			ok = DEFAULT & GET_FLAGS(tree, exitobj);
		} while (ok && !name_ok(name, where, true));

		if (ok)
		{
			rcs_edited = true;
			get_obname(name, ad_view, sobj);
			icon_get(taddr, &text[0], &valid[0]);
			itmp = encode(tree, IBTPOS0, 3);
			if (itmp != trule)
			{		
				itmp = icon_tapply(itmp, gl_hschar,
					(*(short *)(IB_HICON(taddr))));
				(*(short *)(IB_YTEXT(taddr)) = (itmp));
			}
			(*(short *)(IB_WTEXT(taddr)) = ((short)(gl_wschar * strlen(text))));
			icon_tfix(taddr);
			itmp = encode(tree, IBCPOS0, 9);
			if (itmp != crule)
			{
				crule = itmp;
				itmp = icon_capply(crule / 3, gl_hschar,
					(*(short *)(IB_HICON(taddr))));
				(*(short *)(IB_YCHAR(taddr)) = (itmp));
				itmp = icon_capply(crule % 3, gl_wschar,
					(*(short *)(IB_WICON(taddr))));
				(*(short *)(IB_XCHAR(taddr)) = (itmp));
			}
			icon_wh(taddr, &w, &h);
			newsize_obj(ad_view, sobj, w, h, false);
		}
		map_tree(tree, ROOT, NIL, (fkt_parm)desel_obj);	/* clear radio buttons */
	}
}

void open_obj(short sobj)
{
	long	tree; 
	short	type; 

	if (rcs_state == MENU_STATE)
		if (!in_bar(ad_view, sobj))
			if (!menu_ok(ad_view, sobj))
				return;
	if (rcs_state == FREE_STATE)
		if (sobj == ROOT)
			return;

	tree = ad_view;
	obj_redraw(tree, sobj);
	type = ((GET_TYPE(tree, sobj)) & 0xff);

	switch (type)
	{
		case G_TITLE:
		case G_STRING:
		case G_BUTTON:
			edit_str(tree, sobj);
			break;
		case G_IBOX:
		case G_BOX:
		case G_BOXCHAR:
			edit_box(tree, sobj);
			break;
		case G_TEXT:
		case G_BOXTEXT:
		case G_FTEXT:
		case G_FBOXTEXT:
			edit_text(tree, sobj);
			break;
		case G_IMAGE:
			edit_bit(tree, sobj);
			break;
		case G_ICON:
			edit_icon(tree, sobj);
			break;
		default:
			break;
	}

	if (rcs_state == MENU_STATE || rcs_state == ALRT_STATE)
		view_objs();
	else
		obj_redraw(ad_view, sobj);
}

void do_bdcol(long tree, short obj, short item)
{
	long	obspec;
	short	type, color;

	color = item - COL0POP;
	get_obf(tree, obj, &type, &obspec);
	switch (type)
	{
		case G_BOX:
		case G_IBOX:
		case G_BOXCHAR:
			obspec &= 0xffff0fffL;
			SET_SPEC(tree, obj, obspec | ((long) color << 12));
			break;
		case G_BOXTEXT:
		case G_FBOXTEXT:
			color = (color << 12) | ((*(short *)(TE_FG(obspec)))
				& 0xfff);
			(*(short *)(TE_FG(obspec)) = (color));
			break;
		default:
			return;
	}
	rcs_edited = true;
}

void do_bgcol(long tree, short obj, short item)
{
	long	obspec;
	short	type, color;

	color = item - COL0POP;
	get_obf(tree, obj, &type, &obspec);
	switch (type)
	{
		case G_BOX:
		case G_BOXCHAR:
			obspec &= 0xfffffff0L;
			SET_SPEC(tree, obj, obspec | (long) color);
			break;
		case G_BOXTEXT:
		case G_FBOXTEXT:
			color = color | ((*(short *)(TE_FG(obspec))) & 0xfff0);
			(*(short *)(TE_FG(obspec)) = (color));
			break;
		case G_ICON:
			color = (color << 8) | ((*(short *)(IB_CHAR(obspec)))
				& 0xf0ff);
			(*(short *)(IB_CHAR(obspec)) = (color));
			break;
		default:
			return;
	}
	rcs_edited = true;
}

void do_fgcol(long tree, short obj, short item)
{
	long	obspec;
	short	type, color;

	color = item - COL0POP;
	get_obf(tree, obj, &type, &obspec);
	switch (type)
	{
		case G_BOXCHAR:
			obspec &= 0xfffff0ffL;
			SET_SPEC(tree, obj, obspec | ((long) color << 8));
			return;
		case G_TEXT:
		case G_FTEXT:
		case G_BOXTEXT:
		case G_FBOXTEXT:
			color = (color << 8) | ((*(short *)(TE_FG(obspec))) 
				& 0xf0ff);
			(*(short *)(TE_FG(obspec)) = (color));
			break;
		case G_ICON:
			color = (color << 12) | ((*(short *)(IB_CHAR(obspec)))
				& 0xfff);
			(*(short *)(IB_CHAR(obspec)) = (color));
			break;
		case G_IMAGE:
			(*(short *)(BI_COLOR(obspec)) = (color));
			break;
		default:
			return;
	}
	rcs_edited = true;
}

void do_patrn(long tree, short obj, short item)
{
	long	obspec;
	short	type, patrn;

	patrn = item - PAT0POP;
	get_obf(tree, obj, &type, &obspec);
	switch (type)
	{
		case G_BOX:
		case G_BOXCHAR:
			obspec &= 0xffffff8fL;
			SET_SPEC(tree, obj, obspec | ((long) patrn << 4));
			break;
		case G_BOXTEXT:
		case G_FBOXTEXT:
			patrn = (patrn << 4) | ((*(short *)(TE_FG(obspec))) & 0xff8f);
			(*(short *)(TE_FG(obspec)) = (patrn));
			break;
		default:
			return;
	}
	rcs_edited = true;
}

void do_thick(long tree, short obj, short item)
{
	long	obspec;
	short	type, i, thick;

	for (i = 0; rcs_it2thk[i].mfrom && rcs_it2thk[i].mfrom != item; i++);
	if (rcs_it2thk[i].mfrom)
	{
		thick = rcs_it2thk[i].mto;
		get_obf(tree, obj, &type, &obspec);
		switch (type)
		{
			case G_BOX:
			case G_IBOX:
			case G_BOXCHAR:
				obspec &= 0xff00ffffL;
				SET_SPEC(tree, obj, obspec |
					((long) (thick & 0xff) << 16));
				break;
			case G_BOXTEXT:
			case G_FBOXTEXT:
				(*(short *)(TE_THICK(obspec)) = (thick));
				break;
			default:
				return;
		}
		rcs_edited = true;
	}
}

void do_rule(long tree, short obj, short item)
{
	long	obspec;
	short	type, rule;

	rule = item == RPLACPOP? 1: 0;
	get_obf(tree, obj, &type, &obspec);
	switch (type)
	{
		case G_BOXCHAR:
			if (item > RPLACPOP)
				break;
			obspec &= 0xffffff7fL;
			SET_SPEC(tree, obj, obspec | ((long) rule << 7));
			break;
		case G_TEXT:
		case G_FTEXT:
		case G_BOXTEXT:
		case G_FBOXTEXT:
			if (item <= RPLACPOP)
			{
				rule = (rule << 7) | 
					((*(short *)(TE_FG(obspec))) & 0xff7f);
				(*(short *)(TE_FG(obspec)) = (rule));
			}
			else if (item <= SMALLPOP)
				(*(short *)(TE_FONT(obspec)) = (item == SMALLPOP?
					SMALL: IBM));
			else if (item <= CENTPOP)
				(*(short *)(TE_JUST(obspec)) = (item - LEFTPOP));

			break;
		default:
			return;
	}
	rcs_edited = true;
}

void do_posn(long tree, short obj, short item)
{
	short	pobj;
	GRECT	o, p;

	if (obj != ROOT)
	{
		pobj = get_parent(tree, obj);
		objc_xywh(tree, pobj, &p);
		objc_xywh(tree, obj, &o);
		o.g_x = GET_X(tree, obj);
		o.g_y = GET_Y(tree, obj);

		switch (item)
		{
			case JLFTPOP:
				o.g_x = 0;
				break;
			case JRGTPOP:
				o.g_x = max(0, p.g_w - o.g_w);
				break;
			case JCENPOP:
				o.g_x = max(0, (p.g_w - o.g_w) / 2);
				break;
			case JTOPPOP:
				o.g_y = 0;
				break;
			case JBOTPOP:
				o.g_y = max(0, p.g_h - o.g_h);
				break;
			case JMIDPOP:
				o.g_y = max(0, (p.g_h - o.g_h) / 2);
				break;
			case VFILLPOP:
				o.g_h = p.g_h;
				o.g_y = 0;
				break;
			case HFILLPOP:
				o.g_w = p.g_w;
				o.g_x = 0;
				break;
			case SNAPPOP:
				break;
			default:
				return;
		}
		rcs_edited = true;
		if (rcs_state != PANL_STATE || item == SNAPPOP)
		{
			snap_xy(&o.g_x, &o.g_y);
			snap_wh(&o.g_w, &o.g_h);
		}
		obj_redraw(tree, obj);
		ob_setxywh(tree, obj, &o);
		obj_redraw(tree, obj);
	}
}

void do_swtch(long tree, short obj, short item)
{
	long	where;
	short	i, mask;

	if (item != HDDNPOP || obj != ROOT)
	{
		for (i = 0; rcs_bit2obj[i].objno && rcs_bit2obj[i].objno != item; i++)
			;
		if (!rcs_bit2obj[i].objno)
			return;
		else
		{
			mask = rcs_bit2obj[i].flagmask;
			if (mask)
				where = OB_FLAGS(obj);
			else
			{
				mask = rcs_bit2obj[i].statemask;
				if (mask)
					where = OB_STATE(obj);
				else
					return;
			}
		}
		if (!((*(short *)(where)) & mask))
			rcs_edited = true;
		(*(short *)(where) = ((*(short *)(where)) ^ mask));
	}
}
