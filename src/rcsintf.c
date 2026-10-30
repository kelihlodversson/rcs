  /*	RCSINTF.C	06/25/85  	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include <mint/osbind.h>
#include <stdio.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcslib.h"
#include "rcsvdi.h"
#include "rcsmain.h"
#include "rcsmenu.h"
#include "rcsiinit.h"
#include "rcstrees.h"
#include "rcsedit.h"
#include "rcsintf.h"
#include "rcsasm.h"

FILE	*r_infhndl;

static  long	ad_prog;
static	GRECT	prog;
static	GRECT	tools_tmp;

extern	MFDB	gl_tmp;		/* references to RCSVDI.C */

short hndl_dial(long tree, short def, GRECT *p)
{
	short	xdial, ydial, wdial, hdial, exitobj;

	form_center((OBJECT  *)tree, &xdial, &ydial, &wdial, &hdial);
	form_dial(0, p->g_x, p->g_y, p->g_w, p->g_h, 
		xdial, ydial, wdial, hdial);
	if (!rcs_xpert)  
		form_dial(1, p->g_x, p->g_y, p->g_w, p->g_h, 
			xdial, ydial, wdial, hdial);
	objc_draw((OBJECT  *)tree, ROOT, MAX_DEPTH, xdial, ydial, wdial, hdial);

	exitobj = form_do((OBJECT  *)tree, def) & 0x7FFF;

	if (!rcs_xpert)	 
		form_dial(2, p->g_x, p->g_y, p->g_w, p->g_h, 
			xdial, ydial, wdial, hdial);
	form_dial(3, p->g_x, p->g_y, p->g_w, p->g_h, 
		xdial, ydial, wdial, hdial);
	return (exitobj);
}

short hndl_alert(short b_num, long addr)
{
	short	ret;

	graf_mouse(ARROW, 0x0L);
	ret = form_alert(b_num, (const char *)addr);
	graf_mouse(rcs_mform, 0x0L);
	return (ret);
}

void hndl_locked(void)
{
	hndl_alert(1, string_addr(STLOCKED));
}

void mouse_form(short f_num)
{
	rcs_mform = f_num;
	graf_mouse(f_num, NULL);
}

void ini_panes(void)
{
	wind_get(rcs_view, WF_WORKXYWH, &view.g_x, &view.g_y,
		&view.g_w, &view.g_h);

	if (iconedit_flag)
	{
		rc_copy(&view, &itool);
		itool.g_w = idisp.g_w = 2 * ICON_W;
		view.g_x += itool.g_w;
		view.g_w -= itool.g_w;
		ob_setxywh(ad_itool, ROOT, &itool);  /*view_itool*/
		wait_io = 0x0;
		rc_copy(&itool, &wait);
	}
	else
	{
		rc_copy(&view, &tools);
		if (toolp)
		{
			if (GET_HEIGHT(ad_tools, ROOT) == 0)
				/*size of tree is not valid, take saved size*/
				rc_copy(&tools_tmp, &tools);
			else
			{
				/*else take size of tree*/
				tools.g_h = GET_HEIGHT(ad_tools, ROOT);
				tools.g_w = 2 * ICON_W;
			}
			view.g_x += tools.g_w;
			view.g_w -= tools.g_w;
		}
		else
		{
			if (GET_HEIGHT(ad_tools, ROOT) != 0)
			{
				/*size of tree is valid, save size*/
				rc_copy(&tools, &tools_tmp);
				tools_tmp.g_h = GET_HEIGHT(ad_tools, ROOT);
				tools_tmp.g_w = GET_WIDTH(ad_tools, ROOT);
			}
			tools.g_w = tools.g_h = 0;
		}
  		ob_setxywh(ad_tools, ROOT, &tools);
		wait_tools();		/* Reset rectangle wait */ 
	}
	rc_copy(&view, &pbx);  /*this should go into the else above*/
	if (iconedit_flag || partp)
	{
		pbx.g_h = umul_div(view.g_h, 6, 23);
		if (iconedit_flag &&
			((gl_hchar + MAX_ICON_H + 2) > pbx.g_h))
			pbx.g_h = gl_hchar + MAX_ICON_H + 2;
		view.g_h -= pbx.g_h;
		pbx.g_y += view.g_h;
	}
	else
		pbx.g_w = pbx.g_h = 0;
	view_parts();
	/*up to this point should be included in the else */
	if ( iconedit_flag )
		init_img();
}		 

static void arrge_icntree(long tree, short wint, short hint)
{
	short	obj, x, y, rowh, wroot, offx, i;

	wroot = GET_WIDTH(tree, ROOT);
	if ( wroot )
	{
		x = wint;
		if ( (gl_himage + gl_hchar + hint) >= pbx.g_h )
			hint = 0;
		y = hint;
		rowh = 0;

		for (obj = ICONSTRG; obj <= MASKSTRG; obj++)
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
				rowh = max(rowh, GET_HEIGHT(tree, obj));
			}
		}
		offx = (GET_WIDTH(tree,ICONSTRG) - gl_wimage)/2;
		i = 0;
		x = wint;
		y += (rowh + hint);
		rowh = 0;

		for (obj = ICONWIND; obj <= MASKWIND; obj++)
		{			    
    		SET_X(tree, obj, GET_X(tree,ICONSTRG + i++) + offx);
			SET_Y(tree, obj, y);
			if ( !(GET_FLAGS(tree, obj) & HIDETREE) )
			{
				x += (GET_WIDTH(tree, obj) + wint); 
				rowh = max(rowh, GET_HEIGHT(tree, obj));
			}
		}
	}
}

void view_parts(void)
{
	short  foo;
	
	ob_setxywh(ad_pbx, ROOT, &pbx);	
	if (iconedit_flag)
		arrge_icntree(ad_pbx,PBX_WINT, PBX_HINT);
	else
      	arrange_tree(ad_pbx, PBX_WINT, PBX_HINT, &foo, &foo);
}

static void lock_menus(void)
{
	short	*i;

	for (i = ILL_LOCK; *i; i++)
		disab_obj(ad_menu, *i);
	unsble_obj(ad_tools, TRASHCAN);
	unsble_obj(ad_tools, CLIPBORD);
	unsble_obj(ad_tools, HOTSWTCH);
}

void set_menus(void)
{
	short	*ok_list, obj;

	obj = GET_NEXT(ad_menu, GET_HEAD(ad_menu, SCREEN));
	map_tree(ad_menu, obj, SCREEN, (fkt_parm)disab_obj);

	switch (rcs_state)
	{
		case NOFILE_STATE:
			ok_list = OK_NOFILE;
			break;
		case FILE_STATE:
			ok_list = OK_FILE;
			break;
		default:
			ok_list = OK_TREES;
			break;
	}
	for (; *ok_list; ok_list++)
		enab_obj(ad_menu, *ok_list);

	if ( ad_clip )
		enab_obj(ad_menu, PASTITEM);

	if (rcs_nsel > 1)
	{
		disab_obj(ad_menu, INFITEM);
	 	if (rcs_sel[0] != ROOT)
	 		enab_obj(ad_menu, DELITEM);
	}
   	else if (rcs_nsel == 1)
   		menu_opts(ad_view, rcs_sel[0]);

   	if (rcs_lock)
		lock_menus();
}

void menu_opts(long tree, short obj)
{
	short	type;

	type = ((GET_TYPE(tree, obj)) & 0xff);
	switch (rcs_state)
	{
		case PANL_STATE:
		case DIAL_STATE:
			if (GET_HEAD(tree, obj) != NIL)
				if (obj != ROOT)
					enab_obj(ad_menu, FLTITEM);
			if (type != G_ICON && type != G_IMAGE)
	 			enab_obj(ad_menu, TYPEITEM);
	 	case MENU_STATE:
		case FREE_STATE:
			if (type == G_IMAGE || type == G_ICON)
			{
				enab_obj(ad_menu, LOADITEM);
				enab_obj(ad_menu, ICNEDITM);
			}
			if (GET_HEAD(tree, obj) != NIL)
			{
				enab_obj(ad_menu, SRTITEM);
				enab_obj(ad_menu, UNHDITEM);
				enab_obj(ad_menu, FLTITEM);
			}
		case ALRT_STATE:
			enab_obj(ad_menu, OPENITEM);
			if ( obj != ROOT )
			{
				enab_obj(ad_menu, DELITEM);
				enab_obj(ad_menu, CUTITEM);
				enab_obj(ad_menu, COPYITEM);
			}
		default:
			return;
	}
}

void clr_hot(void)
{
	if(iconedit_flag)
	{
		map_tree(ad_itool, ROOT, NIL, (fkt_parm)unsble_obj);
		map_tree(ad_itool, ROOT, NIL, (fkt_parm)desel_obj);
	}
	else
		map_tree(ad_tools, ROOT, NIL, (fkt_parm)unsble_obj);
}

void set_hot(void)
{
	short	j, obj, *i, type, *null = 0;

	if (rcs_nsel)
	{
       	clr_hot();

		for (j = rcs_nsel; j; )
		{
			obj = rcs_sel[--j];
			type = ((GET_TYPE(ad_view, obj)) & 0xff);

			switch (type)
			{
				case G_IBOX:
					i = HOT_IBOX;
					break;
				case G_BOX:
					i = HOT_BOX;
					break;
				case G_TEXT:
				case G_FTEXT:
					i = HOT_TEXT;
					break;
				case G_BOXCHAR:
				case G_BOXTEXT:
				case G_FBOXTEXT:
					i = HOT_BTEXT;
					break;
				case G_IMAGE:
					i = HOT_IMAGE;
					break;
				case G_ICON:	
					i = HOT_ICON;
					break;
				default:
					i = null;
					break;
			}

			if (i != null)
				for (; *i; i++)
					sble_obj(ad_tools, *i);
		}

		if (rcs_nsel > 1 || rcs_sel[0] != ROOT)
		{
			if (rcs_state != ALRT_STATE)
				sble_obj(ad_tools, HOTPOSN);
			sble_obj(ad_tools, TRASHCAN);
			if (rcs_nsel == 1)
				sble_obj(ad_tools, CLIPBORD);
		}
		if (rcs_state != ALRT_STATE)
			sble_obj(ad_tools, HOTSWTCH);
	}
}

void set_switch(void)
{
	long	tree;
	short	obj, j, flags, state;

	obj = rcs_sel[0];
	flags = GET_FLAGS(ad_view, obj);
	state = GET_STATE(ad_view, obj);

	ini_tree(&tree, POPSWTCH);
	map_tree(tree, ROOT, NIL, (fkt_parm)unchek_obj);
	if (rcs_nsel == 1)
	{
		for (j = 0; rcs_bit2obj[j].objno; j++)
			if (flags & rcs_bit2obj[j].flagmask ||
				state & rcs_bit2obj[j].statemask)
				chek_obj(tree, rcs_bit2obj[j].objno);
	}
}

void new_state(short new)
{
	short	pbxno;

	rcs_state = new;
	map_tree(ad_menu, ROOT, NIL, (fkt_parm)enab_obj);
	disab_obj(ad_menu, ACCLINE);
	switch (new)
	{
		case NOFILE_STATE:
			pbxno = FILEPBX;
			break;
		case FILE_STATE:
			pbxno = FILEPBX;
			break;
		case PANL_STATE:
			pbxno = DIALPBX;
			break;
		case DIAL_STATE:
			pbxno = DIALPBX;
			break;
		case ALRT_STATE:
			pbxno = ALRTPBX;
			break;
		case MENU_STATE:
			pbxno = MENUPBX;
			break;
		case FREE_STATE:
			pbxno = FREEPBX;
			break;
	}

	ini_tree(&ad_pbx, pbxno);
	set_menus();
	clr_hot();
	view_parts();
	send_redraw(rcs_view, &pbx);
} 

short file_view(void)		/* Are we showing tree icons */
{
	return (rcs_state == FILE_STATE || rcs_state == NOFILE_STATE);
}

short tree_view(void)		/* Or the inside of a tree ? */
{
	return !file_view();
}

void clr_clip(void)
{
	ad_clip = 0;
	SET_SPEC(ad_tools, CLIPBORD, image_addr(CLIPMPTY));
	obj_nowdraw(ad_tools, CLIPBORD);
}

void set_title(char *new)
{
	rcs_title[0] = ' ';
	strcpy(&rcs_title[1], new);
	strcat(&rcs_title[0], " ");
	wind_set_str(rcs_view, WF_NAME, &rcs_title[0]);
}

void get_defpath(char *path, char *spec)
{
	if(rsc_path[0])
		strcpy(path, &rsc_path[1]);
	else
	{
		path[0] = Dgetdrv() + 'A';
		path[1] = ':';
		path[2] = '\\';
		path[3] = '\0';
	}
	strcat(path, spec);
}

void clr_title(void)
{
	char	temp[20];

	rcs_title[0] = ' ';
	LLSTRCPY(string_addr(STTITLE), (long)((long)(temp)));
	get_defpath(&rcs_title[1], temp);
	wind_set_str(rcs_view, WF_NAME, &rcs_title[0]);
}

void hot_off(void)
{
	if (rcs_hot != NIL)
	{
		if(iconedit_flag)
			invert_obj(ad_itool, rcs_hot, &itool);
		else
		{
			invert_obj(ad_tools, rcs_hot, &tools);
			rcs_hot = NIL;
		}
	}
}

void wait_tools(void)
{
	wait_io = 0x0;		/* wait to enter */
	rc_copy(&tools, &wait);	/* the toolbox	*/
}

void wait_obj(long tree, short obj)
{	
	wait_io = 0x1;			/* wait to exit */
	objc_xywh(tree, obj, &wait);	/* an object's rectangle */
}

short pane_find(long *tree, short *obj, short mx, short my)
{
	GRECT	dt;
	short	desktitle, the_active;
	*obj = objc_find((OBJECT  *)(*tree = ad_pbx), 0, MAX_DEPTH, mx, my);
	if (*obj != NIL)
		return (true);
	else
	{
		*obj = objc_find((OBJECT  *)(*tree = ad_tools), 0, MAX_DEPTH, mx, my);
		if (*obj != NIL)
			return (true);
		else
		{
			*obj = objc_find((OBJECT  *)(*tree = ad_view), 0, MAX_DEPTH, mx, my);
			if ( rcs_state == MENU_STATE && *obj == THEBAR)
			/* special case desk menu */
			{
				the_active = get_active(*tree);
				desktitle = GET_HEAD(*tree, the_active);
				ob_relxywh(*tree, desktitle, &dt);
				if( mx >= dt.g_x)
					*obj = desktitle;
			}
			if (*obj != NIL)
				return (true);
			else
				return(false);
		}
	}
}

short hndl_pop(short obj, short itool)
{
	short	i, event, mx, my, sel_obj, disab, outline, foo;
	unsigned short	buf_seg, buf_off;
	long	tree;
	GRECT	poparea, bltarea;

	if (itool)
	{
		for (i = 0; icn_ht2pop[i].hotobj && icn_ht2pop[i].hotobj != obj; i++)
			;
		if (!icn_ht2pop[i].hotobj || !icn_ht2pop[i].popmenu)
			return (NIL);
	}
	else
	{
		for (i = 0; rcs_ht2pop[i].hotobj && rcs_ht2pop[i].hotobj != obj; i++)
			;
		if (!rcs_ht2pop[i].hotobj || !rcs_ht2pop[i].popmenu)
			return (NIL);
	}
	ini_tree(&tree, itool ? icn_ht2pop[i].popmenu: rcs_ht2pop[i].popmenu);
	objc_xywh(tree, ROOT, &poparea);
	objc_offset((OBJECT  *)(itool ? ad_itool:ad_tools), obj, &poparea.g_x, &poparea.g_y);
	ob_setxywh(tree, ROOT, &poparea);

	rc_copy(&poparea, &bltarea);
	bltarea.g_x--;
	bltarea.g_y--;
	bltarea.g_w += 2;
	bltarea.g_h += 2;
	gl_tmp.fd_nplanes = gl_nplanes;
	wind_get(0, 17, &buf_off, &buf_seg, &foo, &foo);
	if (!buf_off && !buf_seg)
		gl_tmp.fd_addr = (void *)0xffffffffl;
	else
	{
		gl_tmp.fd_addr = (void *)(((long)buf_off << 16) | ((short)(buf_seg)));
	}
	wind_update(BEG_MCTRL);		/* protection vs. buffer collisions! */

	bb_save(&bltarea);
	objc_draw((OBJECT  *)tree, ROOT, MAX_DEPTH, 
		bltarea.g_x, bltarea.g_y, bltarea.g_w, bltarea.g_h);
	outline = itool ? icn_ht2pop[i].outline : rcs_ht2pop[i].outline; 

	for (;;)
	{
		sel_obj = NIL;
		objc_xywh(tree, ROOT, &poparea);

		event = evnt_multi(MU_BUTTON | MU_M1, 0x01, 0xff, 0x1,
			false, poparea.g_x, poparea.g_y, 
				poparea.g_w, poparea.g_h,
			0, 0, 0, 0, 0, 0, 0,
			&mx, &my, &foo, &foo, &foo, &foo);

		if ( !(event & MU_M1))
			break;
		sel_obj = objc_find((OBJECT  *)tree, ROOT, MAX_DEPTH, mx, my);
		if (event & MU_BUTTON)
			break;

		disab = GET_STATE(tree, sel_obj) & DISABLED;
		if (!disab)
		if (outline)
			outl_obj(tree, sel_obj, &poparea);
		else
			invert_obj(tree, sel_obj, &poparea);

		objc_xywh(tree, sel_obj, &poparea);
		event = evnt_multi(MU_BUTTON | MU_M1, 0x01, 0xff, 0x1,
			true, poparea.g_x, poparea.g_y, 
				poparea.g_w, poparea.g_h,
			0, 0, 0, 0, 0, 0, 0,
			&mx, &my, &foo, &foo, &foo, &foo);

		if (disab)
			sel_obj = NIL;
		else if (outline)
			outl_obj(tree, sel_obj, &poparea);
		else
			invert_obj(tree, sel_obj, &poparea);

		if (event & MU_BUTTON)
			break;
	}

	bb_restore(&bltarea);
	wind_update(END_MCTRL);
	return (sel_obj);
} 

static short rub_wait(GRECT *po, short *mx, short *my)
{
	short	down, rets[6];

	graf_mouse(M_OFF, NULL);
	gsx_xbox(po);
	graf_mouse(M_ON, NULL);

	down = !(MU_BUTTON & evnt_multi(MU_BUTTON | MU_M1, 
		0x01, 0xff, 0x00, true, *mx, *my, 1, 1,
		0, 0, 0, 0, 0, 0, 0,
		&rets[0], &rets[1], &rets[2],
		&rets[3], &rets[4], &rets[5]));

	*mx = rets[0];
	*my = rets[1];

	graf_mouse(M_OFF, NULL);
	gsx_xbox(po);
	graf_mouse(M_ON, NULL);

	return (down);
}

void hot_dragbox(GRECT *p, GRECT *pc, short *mx, short *my, short hot)
{
	short	offx, offy, down, over_obj;
	GRECT	o;

	gsx_attr(false, MD_XOR, BLUE);
	o.g_w = p->g_w;
	o.g_h = p->g_h;
	graf_mkstate(mx, my, &down, &down);
	offx = min(p->g_w, max(0, *mx - p->g_x));
	offy = min(p->g_h, max(0, *my - p->g_y));

	do {
		o.g_x = *mx - offx;
		o.g_y = *my - offy;
/*		rc_constrain(pc, &o);  */
		gsx_sclip(pc);
		down = rub_wait(&o, mx, my);
		if (hot)
		{
			over_obj = objc_find((OBJECT  *)ad_tools, ROOT, NIL, *mx, *my);
			if (over_obj != CLIPBORD && over_obj != TRASHCAN)
				hot_off();
			else if (over_obj != rcs_hot)
			{
				hot_off();
				invert_obj(ad_tools, rcs_hot = over_obj,&tools);
			}
		}
	} while (down);

	hot_off();
	p->g_x = *mx - offx;
	p->g_y = *my - offy;
}

void clamp_rubbox(short x, short y, short wlow, short hlow, short whigh, short hhigh,
	long ahigh, short *w, short *h, GRECT *bound)
{
	GRECT	o;
	short	mx, my, down; 

	gsx_sclip(bound);
	gsx_attr(false, MD_XOR, BLUE);
	rc_set(&o, x, y, 0, 0);

	do {
		graf_mkstate(&mx, &my, &down, &down);
		o.g_w = max(mx - o.g_x + 1, wlow);
		o.g_h = max(my - o.g_y + 1, hlow);
		o.g_w = min(o.g_w, whigh);
		o.g_h = min(o.g_h, hhigh);
		if (ahigh)
			o.g_h = min(o.g_h, 
				(short) ((ahigh + o.g_w - 1) / o.g_w) );
		down = rub_wait(&o, &mx, &my);
	} while (down);

	*w = o.g_w;
	*h = o.g_h;
}

void obj_handle(long tree, short obj, GRECT *p)
{
	short	t;

	objc_xywh(tree, obj, p);
	t = max(p->g_w >> 3, gl_wchar);
	p->g_x += p->g_w - t;
	p->g_w = t;
	t = max(p->g_h >> 3, gl_hchar >> 1);
	p->g_y += p->g_h - t;
	p->g_h = t;
}

void about_dial(void)
{
	long	tree;
	GRECT	p;

	objc_xywh(ad_menu, DESKMENU, &p);
	ini_tree(&tree, ABOUTREE);
	hndl_dial(tree, 0, &p);
	desel_obj(tree, AOKITEM);
}

void info_dial(short obj)
{
	long	savname, name, tree, space;
	GRECT	p;
	short	temp, where; 
	char	nobj[6], nib[6], nbb[6], nti[6], nimg[6], nstr[6];
	char	ntree[6], ntot[8], nleft[8];

	ini_tree(&tree, INFODIAL);
	savname = GET_SPEC(tree, INFONAME);
	if (obj == NIL)
	{
		objc_xywh(ad_menu, OPTNMENU, &p);
		if (file_view())
		{
			if (rcs_state == NOFILE_STATE)
				name = string_addr(STTITLE);
			else 
				name = (long)((long)(rcs_title));
			mouse_form(HGLASS);	/* This could take a while..*/
			clr_tally();
			map_all((fkt_parm)tally_obj);
			tally_free();
			rcs_tally.nbytes += (*(short *)(RSH_NTREE(head))) * sizeof(long);
			rcs_tally.nbytes += sizeof(RSHDR);
			mouse_form(ARROW);
			temp = (*(short *)(RSH_NTREE(head)));
			sprintf(ntree, "%hd", temp);
			SET_SPEC(tree, INFOTREE, (long)((long)(ntree)));
		}
		else
		{
			name = (long)((long)(rcs_title));
			hide_obj(tree, INFOPANL);
			clr_tally();
			map_tree(ad_view, ROOT, NIL, (fkt_parm)tally_obj);
		} 
	}
	else
	{
		objc_xywh(ad_view, obj, &p);
		hide_obj(tree, INFOPANL);
		if (file_view())
		{
			where = find_tree(trpan_f(obj - 1));
			name = (long)((long)(get_name(where)));
			clr_tally();
			map_tree( tree_addr(trpan_f(obj - 1)), ROOT, NIL, (fkt_parm)tally_obj);
		}
		else
		{
			where = find_obj(ad_view, obj);
			if (where == NIL)
				name = savname;
			else
				name = (long)((long)(get_name(where)));
			clr_tally();
			tree = ad_view;
			map_tree(tree, obj, GET_NEXT(tree, obj), (fkt_parm)tally_obj);
			ini_tree(&tree, INFODIAL);
		} 
	}

	SET_SPEC(tree, INFONAME, name);
	SET_X(tree, INFONAME, (short)(gl_wchar * (25 - strlen((char *)(name)) / 2)));
	sprintf(nobj, "%hd", rcs_tally.nobj);
	SET_SPEC(tree, INFOOBJ, (long)((long)(nobj)));
	sprintf(nib, "%hd", rcs_tally.nib);
	SET_SPEC(tree, INFOIB, (long)((long)(nib)));
	sprintf(nbb, "%hd", rcs_tally.nbb);
	SET_SPEC(tree, INFOBB, (long)((long)(nbb)));
	sprintf(nti, "%hd", rcs_tally.nti);
	SET_SPEC(tree, INFOTI, (long)((long)(nti)));
	sprintf(nimg, "%hd", rcs_tally.nimg);
	SET_SPEC(tree, INFOIMG, (long)((long)(nimg)));
	sprintf(nstr, "%hd", rcs_tally.nstr);
	SET_SPEC(tree, INFOSTR, (long)((long)(nstr)));
	sprintf(ntot, "%ld", rcs_tally.nbytes);
	SET_SPEC(tree, INFOTOT, (long)((long)(ntot)));
	space = avail_mem();
	sprintf(nleft, "%ld", space);
	SET_SPEC(tree, INFOLEFT, (long)((long)(nleft)));
	hndl_dial(tree, 0, &p);

	desel_obj(tree, INFOOK);
	unhide_obj(tree, INFOPANL);
	SET_SPEC(tree, INFONAME, savname);
	if (file_view())
		dselct_tree(ad_view, obj);
}

static short find_opt(short opt)
{
	short 	idx;
		    
	for ( idx = 0; idx < NOPTS; idx++ )
	    if ( deflt_options[idx].option == opt )
	    	return( idx );
	return (NIL);
}				  

static short get_opstate(short index)
{
	return( deflt_options[index].state );
}

static void set_opstate(short index)
{
	deflt_options[index].state = true;
}

void read_inf(short flag)
{
	short total_opt;

	r_infhndl = fopen(&rcs_infile[0], "rb");
	if (r_infhndl == NULL)
	{
		/* no in file so set output and safety options to standard defaults */
		rcs_fsrtflag = false;
		rcs_cflag = false;
		rcs_hflag = true;
		rcs_oflag = false;
		rcs_cbflag = false;
		rcs_f77flag = false;
		rcs_lock = false;
		rcs_xpert = false;
		rsc_path[0] = '\0';
	}
	else
	{
		total_opt = (short)sizeof(DEFLTS) * NOPTS;
		dmcopy(r_infhndl, 0x0L, (long)((long)(&deflt_options)), total_opt);
		rcs_fsrtflag = get_opstate( find_opt( FSRTITEM ));
		rcs_cflag = get_opstate( find_opt( SRCITEM));
		rcs_hflag = get_opstate( find_opt( CITEM));
		rcs_oflag = get_opstate( find_opt( PASCITEM));
		rcs_cbflag = get_opstate( find_opt( CBITEM));
		rcs_f77flag = get_opstate( find_opt(F77ITEM));
		rcs_lock = get_opstate( find_opt( SAFELOCK ));	 
		rcs_xpert = get_opstate( find_opt( SAFEXPRT ));
		if (flag)
		{
			if (!dmcopy(r_infhndl, (long) total_opt, (long)((long)(rsc_path)), 1))
				rsc_path[0] = '\0';  /*rsc_path is not in old .inf */
			else if(rsc_path[0])
				dmcopy(r_infhndl,(long) (total_opt+1), (long)((long)(&rsc_path[1])),
				(short) rsc_path[0] + 1); /*copy the 0 byte at the end of the path*/
		}
		fclose( r_infhndl );
	}
}	 

static void init_deflts(void)
{
	deflt_options[0].option =  FSRTITEM;
	deflt_options[1].option =  SRCITEM;
	deflt_options[2].option =  CITEM;
	deflt_options[3].option =  PASCITEM;
	deflt_options[4].option =  CBITEM;
	deflt_options[5].option =  F77ITEM;
	deflt_options[6].option =  SAFELOCK;	 
	deflt_options[7].option =  SAFEXPRT;
	rsc_path[0] = '\0';
}

void wrte_inf(void)
{	 
	short	idx;

	r_infhndl = fopen( &rcs_infile[0], "rb");
	if (r_infhndl == NULL)
		init_deflts();
	else
		fclose(r_infhndl);
	r_infhndl = fopen(&rcs_infile[0], "wb");
	if (r_infhndl != NULL)
	{
		for ( idx = 0; idx < NOPTS; idx++ )
			deflt_options[idx].state = false;
		if (rcs_fsrtflag )
			set_opstate(find_opt(FSRTITEM) );
		if (rcs_cflag )
			set_opstate(find_opt(SRCITEM));
		if (rcs_hflag )
			set_opstate(find_opt(CITEM));
		if (rcs_oflag )
			set_opstate(find_opt(PASCITEM));
		if (rcs_cbflag)
			set_opstate(find_opt(CBITEM));
		if (rcs_f77flag)
			set_opstate(find_opt(F77ITEM));
		if (rcs_lock)
			set_opstate(find_opt(SAFELOCK));
		if (rcs_xpert)
			set_opstate(find_opt(SAFEXPRT));
		fwrite( deflt_options, sizeof(DEFLTS), NOPTS, r_infhndl );
		fwrite( rsc_path, 1, 1, r_infhndl );
		if (rsc_path[0])
			fwrite( &rsc_path[1], 1, (size_t)(rsc_path[0]+1), r_infhndl );
		fclose( r_infhndl );
	}
}

void outp_dial(void)
{
	long	tree;
	GRECT	p;
	short	exitobj;

	ini_tree(&tree, OUTPTREE);
	if (rcs_cflag)
		sel_obj(tree, SRCITEM);
	if (rcs_hflag)
		sel_obj(tree, CITEM);
	if (rcs_oflag)
		sel_obj(tree, PASCITEM);
	if (rcs_cbflag)
		sel_obj(tree, CBITEM);
	if (rcs_f77flag)
		sel_obj(tree, F77ITEM);
	if (rcs_fsrtflag) 
		sel_obj(tree, FSRTITEM );
	objc_xywh(ad_menu, GLOBMENU, &p);
	exitobj = hndl_dial(tree, 0, &p);
	if (exitobj == OUTPOK)
	{			 
		rcs_fsrtflag = GET_STATE(tree, FSRTITEM) & SELECTED;
		rcs_cflag = GET_STATE(tree, SRCITEM) & SELECTED;
		rcs_hflag = GET_STATE(tree, CITEM) & SELECTED;
		rcs_oflag = GET_STATE(tree, PASCITEM) & SELECTED;
		rcs_cbflag = GET_STATE(tree, CBITEM) & SELECTED;
		rcs_f77flag = GET_STATE(tree, F77ITEM) & SELECTED;
	}
	map_tree(tree, ROOT, NIL, (fkt_parm)desel_obj);
}

void safe_dial(void)
{
	long	tree;
	GRECT	p;
	short	exitobj;

	ini_tree(&tree, SAFEDIAL);
	if (rcs_lock)
		sel_obj(tree, SAFELOCK);
	else if (rcs_xpert)
		sel_obj(tree, SAFEXPRT);
	else
		sel_obj(tree, SAFENORM);
	objc_xywh(ad_menu, GLOBMENU, &p);

	exitobj = hndl_dial(tree, 0, &p);

	if (exitobj == SAFEOK)
	{
		rcs_lock = SELECTED & GET_STATE(tree, SAFELOCK);
		if (rcs_lock)
		{
			clr_clip();
			lock_menus();
		}
		rcs_xpert = SELECTED & GET_STATE(tree, SAFEXPRT);
	}
	map_tree(tree, ROOT, NIL, (fkt_parm)desel_obj);
}

void ini_prog(void)
{
	GRECT 	p;

	ini_tree(&ad_prog, PROGRESS);	/* set up progress indicator */
	form_center((OBJECT  *)ad_prog, &prog.g_x, &prog.g_y, &prog.g_w, &prog.g_h);
	objc_draw((OBJECT  *)ad_prog, ROOT, MAX_DEPTH, 
		prog.g_x, prog.g_y, prog.g_w, prog.g_h);
	objc_xywh(ad_prog, WRITING, &p);
	prog.g_y = p.g_y;  prog.g_h = p.g_h;
}

void show_prog(short strno)
{
	long	taddr;

	SET_SPEC(ad_prog, WRITING, taddr = string_addr(strno));
	SET_WIDTH(ad_prog, WRITING, (short)(gl_wchar * strlen((char *)(taddr))));
	do_posn(ad_prog, WRITING, JCENPOP);	/* Use our own editing */
	objc_draw((OBJECT  *)ad_prog, ROOT, MAX_DEPTH, 
		prog.g_x, prog.g_y, prog.g_w, prog.g_h);
}
