/*	RCSMAIN.C	10/08/84 - 1/25/85  	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <stdbool.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcskeybd.h"
#include "rcslib.h"
#include "rcsinit.h"
#include "rcsfiles.h"
#include "rcsiinit.h"
#include "rcsiedit.h"
#include "rcsifile.h"
#include "rcsimain.h"
#include "rcsintf.h"
#include "rcsedit.h"
#include "rcsload.h"
#include "rcsobjs.h"
#include "rcstrees.h"
#include "rcsvdi.h"
#include "rcsasm.h"
#include "rcsmain.h"

extern	short	paste_img;
extern	unsigned short	mflags;
extern	short	scale;

static void set_filemenu(void)
{
	if ( rcs_state == NOFILE_STATE )
	{
		enab_obj( ad_menu, NEWITEM);
		enab_obj( ad_menu, SVASITEM);
		enab_obj( ad_menu, RVRTITEM);
	}
	else if ( rcs_state == FILE_STATE)
	{
		enab_obj( ad_menu, SAVEITEM);
		enab_obj( ad_menu, RVRTITEM);
	}
}

void do_redraw(short w_handle, GRECT *dirty, long tree, GRECT *area)
{
	GRECT		p, r;
	short		i;

	graf_mouse(M_OFF, NULL);
	wind_get(w_handle, WF_FIRSTXYWH, &p.g_x, &p.g_y, &p.g_w, &p.g_h);
	while ( p.g_w && p.g_h )
	{
		if ( rc_intersect(area, &p) )
	  		if ( rc_intersect(dirty, &p) )
			{
 				gsx_sclip(&p);
				if (tree ==  ad_view)	/* trivial accelerator */
				{   	 			
					objc_xywh(ad_view, ROOT, &r);
					rc_intersect(&p, &r);
					if (!rc_equal(&p, &r) || rcs_state == MENU_STATE)
						gr_rect(BLUE, IP_4PATT, &p);
				}
	    		objc_draw((OBJECT  *)tree, ROOT, MAX_DEPTH, p.g_x, p.g_y, p.g_w, p.g_h);
				if (tree == ad_view && tree_view() )
					for (i = rcs_nsel; i; )
					{
						objc_xywh(ad_view, rcs_sel[--i], &r);
						gsx_xbox(&r);
						if (rcs_state != ALRT_STATE)
						{
							obj_handle(ad_view, rcs_sel[i], &r);
							gr_rect(BLUE, IP_SOLID, &r);
						}
					}
			}
		  	wind_get(w_handle, WF_NEXTXYWH, &p.g_x, &p.g_y, &p.g_w, &p.g_h);
	}
	gsx_sclip(&p);
	graf_mouse(M_ON, NULL);
}

void hndl_redraw(short w_handle, GRECT *c)
{
	if(iconedit_flag)
	{
		do_redraw(w_handle, c, ad_itool, &itool);
		draw_clipb(true, c);	/*draw the clipping*/
	}
	else
	{
		do_redraw(w_handle, c, ad_tools, &tools); 
	}
	do_redraw(w_handle, c, ad_pbx, &pbx);
	if (iconedit_flag)
		outl_img();	/*draw the outline of the icon/data*/
	do_redraw(w_handle, c, ad_view, &view);
}

void obj_nowdraw(long tree, short obj)
{
	GRECT	p;

	objc_xywh(tree, obj, &p);
	hndl_redraw(rcs_view, &p);
}

static void cut_part(short sobj, short dup)
{
	long	tree;

	if (file_view())
		cut_tree(trpan_f(sobj), dup);
	else
		cut_obj(sobj, dup);
	tree = ad_tools;
	(*(long *)(OB_SPEC(CLIPBORD)) = (image_addr(CLIPFULL)));
	if( toolp) 
		obj_nowdraw(ad_tools, CLIPBORD);
	else
		enab_obj( ad_menu, PASTITEM);
	set_filemenu(); 		
	rcs_edited = true;
}

static void paste_part(short dobj, short dx, short dy, short dup)
{
	if (ad_clip)
	{
		if (file_view())
			paste_tree(trpan_f(dobj), dx, dy, dup);
		else
			paste_obj(dobj, dx, dy, dup);
		set_filemenu();		
		rcs_edited = true;
	}
}

static void del_part(short sobj)
{
	if (file_view())
		del_tree(trpan_f(sobj));
	else
		del_ob(sobj);
	set_filemenu();  	
	rcs_edited = true;
}

static void new_part(short sobj, short dobj, short dx, short dy)
{
	if (file_view())
		new_tree(sobj, trpan_f(dobj), dx, dy);
	else
		new_obj(ad_pbx, sobj, dobj, dx, dy); 
	rcs_edited = true;
	set_filemenu();
}

static void dup_part(short sobj, short dobj, short dx, short dy)
{
	if (file_view())
		dup_tree(trpan_f(sobj), trpan_f(dobj), dx, dy);
	else
		new_obj(ad_view, sobj, dobj, dx, dy);
	rcs_edited = true;
	set_filemenu();
}

static void mov_part(short sobj, short dobj, short dx, short dy)
{
	if (file_view())
		mov_tree(trpan_f(sobj), trpan_f(dobj), dx, dy);
	else
		mov_obj(sobj, dobj, dx, dy);
	rcs_edited = true;
	set_filemenu();
}

static void type_part(short sobj)
{
	if (file_view())
		type_tree(trpan_f(sobj));
	else
		type_obj(sobj);
}

static short hndl_mouse(short mx, short my)
{
	hot_off();
	rcs_hot = objc_find((OBJECT  *)ad_tools, ROOT, MAX_DEPTH, mx, my);
	if (rcs_hot == NIL)
	{
		wait_tools();
	}
	else
	{
		wait_obj(ad_tools, rcs_hot);
		if (GET_FLAGS(ad_tools, rcs_hot) & SELECTABLE)
			invert_obj(ad_tools, rcs_hot, &tools);
		else
			rcs_hot = NIL;
	}
	return (false);
}

static void drop_part(long tree, short sobj, GRECT *d, short mx, short my, short dup)
{
	short	dobj;
	long	dtree;

	if (pane_find(&dtree, &dobj, d->g_x, d->g_y))
	{
		if (tree == ad_tools)		
		{
			if (dtree == ad_view)
				paste_part(dobj, d->g_x, d->g_y, dup);
		}
		else
		{
			if (tree == ad_view)
			{
				if (dtree == ad_tools)
				{
					dobj = objc_find((OBJECT  *)ad_tools, ROOT, NIL, d->g_x, d->g_y);
					if (dobj == CLIPBORD)
						cut_part(sobj, dup);
					else if (dobj == TRASHCAN)
						del_part(sobj);
				}
				else if (dtree == ad_view)
				{
					if (dup)
						dup_part(sobj, dobj, d->g_x, d->g_y);
					else
						mov_part(sobj, dobj, d->g_x, d->g_y);
					return;
				}
			}
			else if (tree == ad_pbx)
			{
				if (dtree == ad_view)
					new_part(sobj, dobj, d->g_x, d->g_y);
			}
		}
	}
}

static void drag_part(long tree, short obj, GRECT *d, short mx, short my)
{
	hot_off();	 
	if (tree == ad_pbx)
		invert_obj(tree,obj,&pbx);
	else if (tree == ad_tools)
		invert_obj(tree,obj,&tools);
	else
		invert_obj(tree, obj, &view);
	graf_mouse(HAND, 0x0L);
	objc_xywh(tree, obj, d);

	if (tree == ad_tools && obj == CLIPBORD && ad_clip 
		&& rcs_clipkind == OBJKIND)
	{
		d->g_w = GET_WIDTH(ad_clip, ROOT);
		d->g_h = GET_HEIGHT(ad_clip, ROOT);
	}
	hot_dragbox(d, &full, &mx, &my, tree == ad_view);
	graf_mouse(ARROW, 0x0L);
	if (tree == ad_pbx)
		invert_obj(tree, obj,&pbx);
	else if (tree == ad_tools)
		invert_obj(tree, obj, &tools);
	else
		invert_obj(tree, obj, &view);
	wait_tools();
}

static short handle_hit(long tree, short obj, short x, short y)
{
	GRECT	d;

	if (tree_view() && rcs_state != ALRT_STATE)
	{
		obj_handle(tree, obj, &d);
		return rc_inside(&d, x, y);
	}
	else
		return (false);
}

static void hndl_down(short mx, short my, long tree, short obj, short dup)
{
	GRECT	d;

	if (tree == ad_view)
	{
		if (handle_hit(tree, obj, mx, my))
		{
			size_obj(tree, obj);
			set_filemenu();
			rcs_edited = true;
			return;
		}
		if (rcs_state == MENU_STATE)	
			if (obj == THEBAR || GET_NEXT(tree, obj) == ROOT)
				return;		/* kludgy */
		if (obj == ROOT)
			return;		
	}

	if (tree == ad_tools)
		if (obj != CLIPBORD || ad_clip == 0)
			return;

	drag_part(tree, obj, &d, mx, my);
	drop_part(tree, obj, &d, mx, my, dup);
}

static short hndl_dblklik(long tree, short obj)
{
	if (tree == ad_view) 
	{
		if (tree_view())
			open_obj(obj);
		else if (obj != ROOT)
			open_tree(trpan_f(obj));
		if (rcs_edited)
			set_filemenu();
	}
	return(false);
}

void hndl_dsel(void)
{
	if (rcs_nsel)
	{
		if (file_view())
			dselct_tree(ad_view, rcs_sel[0]);
		else
			dslct_obj(ad_view, rcs_sel[0]);
	}
}	     

static void hndl_sel(long tree, short obj, short keystate)
{
	short	shift, alt, aobj;

	if (tree != ad_view)
	{
		hndl_dsel();
	}
	else if (file_view())
	{
		hndl_dsel();
		select_tree(tree, obj);
	}
	else
	{
		shift = keystate & (K_LSHIFT | K_RSHIFT); 
		alt = keystate & K_ALT;

		if (!shift || rcs_state == ALRT_STATE)	
			hndl_dsel();	/* Prevent delete hassles in alerts */

		if (alt && (aobj = GET_HEAD(tree, obj)) != NIL)
			for (; aobj != obj; aobj = GET_NEXT(tree, aobj))
				slct_obj(tree, aobj);
		else
			slct_obj(tree, obj);
	}
}

static void hndl_del(void)
{
	short	ii;
	for ( ii = rcs_nsel; ii ;del_part(rcs_sel[--ii]))
		;
}
	  
static void hndl_cut(void)
{
	if ( rcs_nsel )
		cut_part(rcs_sel[0], false);
}
				
static void hndl_copy(void)
{
	if ( rcs_nsel ) 
	{
		cut_part(rcs_sel[0], true);
		hndl_dsel();
	}
}

static void hndl_paste(void)
{
	GRECT	d;
	short	mx, my, keystate, dup,button;

	if ( ad_clip )
	{
		graf_mkstate(&mx, &my, &button, &keystate);
		dup = keystate & (K_LSHIFT | K_RSHIFT);
		drag_part(ad_tools, CLIPBORD, &d, mx, my);
		drop_part(ad_tools,CLIPBORD, &d, mx, my, dup);
		if ( !ad_clip ) disab_obj( ad_menu, PASTITEM);
	}
}

static short hndl_sclck(short mx, short my, short keystate, long *tree, short *obj, short *shifted)
{
	short	ctrled;

	*shifted = keystate & (K_LSHIFT | K_RSHIFT); 
	ctrled = keystate & K_CTRL; 

	if (!pane_find(tree, obj, mx, my))
		return (false);
	else if (*tree == ad_view)
	{
		if (*obj != ROOT && ctrled)
			*obj = get_parent(*tree, *obj);
		return (true);
	}
	else if (*obj == ROOT)
		return (false);
	else
		return (true);
}

void send_redraw(short w_handle, GRECT *p)
{
	rcs_rmsg[0] = WM_REDRAW;
	rcs_rmsg[1] = gl_apid;
	rcs_rmsg[2] = 0;
	rcs_rmsg[3] = w_handle;
	rcs_rmsg[4] = p->g_x;
	rcs_rmsg[5] = p->g_y;
	rcs_rmsg[6] = p->g_w;
	rcs_rmsg[7] = p->g_h;
	appl_write(gl_apid, 16, (void *)ad_rmsg);
} 

void obj_redraw(long tree, short obj)
{
	GRECT	p;

	objc_xywh(tree, obj, &p);
	rc_intersect(&view, &p);
	p.g_x -= 3; p.g_y -= 3; p.g_w += 6; p.g_h += 6;
	send_redraw(rcs_view, &p);
}

static void hndl_pmenu(short which, short item)
{
	short	i;
	long    tree;	 
		     
	tree = ad_view;
	for (i = 0; i < rcs_nsel; i++)
	{ 
		switch (which)
		{
			case HOTBGCOL:
				do_bgcol(tree, rcs_sel[i], item);
				break;
			case HOTPATRN:
				do_patrn(tree, rcs_sel[i], item);
				break;
			case HOTBDCOL:
				do_bdcol(tree, rcs_sel[i], item);
				break;
			case HOTTHICK:
				do_thick(tree, rcs_sel[i], item);
				break;
			case HOTFGCOL:
				do_fgcol(tree, rcs_sel[i], item);
				break;
			case HOTRULE:
				do_rule(tree, rcs_sel[i], item);
				break;
			case HOTPOSN:
				do_posn(tree, rcs_sel[i], item);
				break;
			case HOTSWTCH:
				do_swtch(tree, rcs_sel[i], item);
				break;
			default:
				return;
		}		     
		obj_redraw(ad_view, rcs_sel[i]);
	}

	if (which == HOTSWTCH)
	{
		if (item == HDDNPOP)
			dslct_obj(ad_view, rcs_sel[0]);
		set_switch();
	}

	rcs_edited = true;
	set_filemenu();
}

static void hndl_tlbx(long tree, short obj, short dup)
{
	short	pmenu, i;

	if ( (GET_FLAGS(tree, obj) & SELECTABLE))
	{
		if (rcs_nsel)
		{
			hot_off();
			if (obj == TRASHCAN )
				for (i = rcs_nsel; i; del_part(rcs_sel[--i]))
					;
			else if (obj == CLIPBORD )
				cut_part(rcs_sel[0], dup);
			else if ( (pmenu = hndl_pop(obj, false)) != NIL)
				hndl_pmenu(obj, pmenu);
			wait_tools();
		}
	}
}

static short hndl_button(short mx, short my, short button, short keystate, short clicks)
{
	short	obj_sel, dup_sel;
	long	tree_sel;

	graf_mkstate(&mx, &my, &button, &keystate);
	if ( !hndl_sclck( mx, my, keystate, &tree_sel, &obj_sel, &dup_sel) )
		hndl_dsel();
	else if (tree_sel == ad_tools && clicks == 1 && !(button & 0x1))
		hndl_tlbx(tree_sel, obj_sel, dup_sel);
	else if ( clicks == 1 )
	{
		if ( button & 0x0001 )
			hndl_down( mx, my, tree_sel, obj_sel, dup_sel );
		else
			hndl_sel(tree_sel, obj_sel, keystate);
	}
	else
		hndl_dblklik(tree_sel, obj_sel );
	return (false);
}

static void hndl_open(void)
{
	if (tree_view())
    	open_obj(rcs_sel[0]);
   	else if (!rcs_nsel)	      
	{
		if(open_rsc())
			rcs_edited = false;
	}
	else	 
		open_tree(trpan_f(rcs_sel[0]));
}

static void hndl_clos(void)
{
	if (tree_view())
 	{
		clos_tree();
	}
	else if (rcs_state == FILE_STATE)
   		if(clos_rsc())	 
			rcs_edited = false;
}

static short hndl_filemenu(short item)
{
	short	done;

	done = false;

	switch (item)
	{
		case NEWITEM:		  
			if ( iconedit_flag)
			{
				if (new_img() )
					icn_edited = false;
			}
			else
				if(new_rsc())
					rcs_edited = false;
			break;
		case OPENITEM:	 
			if ( iconedit_flag)
			{
	 			if(icnopen())
					icn_edited = false;	    
			}
			else
				hndl_open();
			break; 
		case MERGITEM:
			merge_rsc();
			rcs_edited = true;
			break;
		case CLOSITEM:	 
			if (iconedit_flag)
				done = true;
			else
				hndl_clos();
			break;
		case SAVEITEM:
			if (iconedit_flag)		
			{
				if(save_icn())
					icn_edited = false;	       
			}
			else		 
			{
				save_rsc();
				rcs_edited = false;
			}
			break;
		case SVASITEM:	 
			if( iconedit_flag)
			{
				if(svas_icn())
					icn_edited = false;
			}
			else
				if(svas_rsc())
					rcs_edited = false;
			break;
		case RVRTITEM:
			if (iconedit_flag)	       
			{
				if(rvrt_icn())
					icn_edited = false;
			}
			else
				if(rvrt_rsc())
					rcs_edited = false;
			break;
		case QUITITEM:				      
			done =	quit_rsc();
	}
	if ( rcs_edited )
		set_filemenu();
	if (iconedit_flag)
		set_icnmenus();
	return (done);													
}

static short hndl_editmenu(short item)
{
	short	done, obmsb, new_h, new_w;
	long	tree;

	done = false;

	switch (item)
	{
		case DELITEM:
			if(iconedit_flag) del_img(true, 3);
			else hndl_del();
			break;
		case CUTITEM:
			if(iconedit_flag) cut_img();
			else hndl_cut();
			break; 
		case COPYITEM:
			if(iconedit_flag) copy_img(true);
			else hndl_copy();
			break;
		case PASTITEM:
			if(iconedit_flag){
			    paste_img = true;
			    rubrec_off();
			    mflags |= MU_M2;
			}
			else hndl_paste();
			break;
		case INVITEM:
			graf_mouse(M_OFF,0x0L);
			invrt_img();	       
			graf_mouse(M_ON,0x0L);
			break;
		case SOLIDIMG:		      
			graf_mouse(M_OFF,0x0L);
			solid_img();       
			fb_redraw();
			graf_mouse(M_ON,0x0L);
			break;		 
/*		case CLEARITM:
			clear_img();
			break;			
		case DATTOMSK:
			if (gl_isicon)	      
			{
				graf_mouse(M_OFF,0x0L);
				data_to_mask(TRUE);    
				graf_mouse(M_ON,0x0L);
			}
			break;
		case MSKTODAT:
			if (gl_isicon)
			{
				graf_mouse(M_OFF,0x0L);
				data_to_mask(FALSE);   
				graf_mouse(M_ON,0x0L);
			}
			break;
*/		case SIZEITEM:			
			icon_size(&new_w, &new_h);
			if ( new_w != gl_wimage || new_h != gl_himage)
			{
				tree = save_tree;			
				obmsb = 1;	  
				if (gl_isicon)
					(*(short *)(OB_TYPE(save_obj)) = ((obmsb<< 8)| G_ICON));
				else
					(*(short *)(OB_TYPE(save_obj)) = ((obmsb<<8)|G_IMAGE));
				if (gl_isicon)
				{
					(*(short *)(IB_WICON(gl_icnspec)) = (new_w));
					(*(short *)(IB_HICON(gl_icnspec)) = (new_h));
				}
				else
				{     
					(*(short *)(BI_WB(gl_icnspec)) = (new_w >> 3));
					(*(short *)(BI_HL(gl_icnspec)) = (new_h));
				}
				icn_init(true);
				icn_edited = false;
			}
			else if (scale == CLEARSZ) /*same size, but clear it*/
				clear_img();
			break;
		default:
			break;
	}
   	return( done );
}

static short hndl_omenu(short item)
{
	short	done;

	done = false;
	switch (item)
	{
		case RNAMITEM:
			name_tree(trpan_f(rcs_sel[0]));
			break;
		case TYPEITEM:
			type_part(rcs_sel[0]);
			break;
		case INFITEM:
			info_dial(rcs_nsel? rcs_sel[0]: NIL);
			break;
		case LOADITEM:
			load_part(rcs_sel[0]);
			break;
		case ICNEDITM:
			icon_edit();
			break;
	  	default:
			break;
	}
	if (rcs_edited )
		set_filemenu();
	return (done);
} 

static short hndl_hmenu(short item)
{
	short	done;

	done = false;
	switch (item)
	{
		case SRTITEM:
			sort_part(rcs_sel[0]);
			break;
		case UNHDITEM:
			unhid_part(rcs_sel[0]);
			break;
		case FLTITEM:
			flatten_part(rcs_sel[0]);
			break;
		default:
			break;
	}
	rcs_edited = true;
	return (done);
}

static void hndl_hidsho(short *flag, short item, short hide, short show)
{
	short	str;

	*flag = !(*flag);
	str = (*flag)? hide: show;
	menu_text((OBJECT  *)ad_menu, item, (const char *)string_addr(str));
	ini_panes();
	if (file_view())
		redo_trees();
	else
	{
		dslct_obj(ad_view, rcs_sel[0]);
		view_objs();
	}
	if ( !toolp && ad_clip)
		enab_obj( ad_menu, PASTITEM);
	send_redraw(rcs_view, &full);
} 

static short hndl_globmenu(short item)
{
	short	done;

	done = false;
	switch (item)
	{
		case OUTPITEM:
			outp_dial();
			break;
		case SAFEITEM:
			safe_dial();
			break;
		case SVOSITEM:
			wrte_inf();
			break;
		case PARTITEM:
	        if( iconedit_flag)
			     save_fat(false);
			hndl_hidsho(&partp, PARTITEM, HIDEPART, SHOWPART);
			break;
		case TOOLITEM:		
			if (iconedit_flag)
			     save_fat(false);	
			hndl_hidsho(&toolp, TOOLITEM, HIDETOOL, SHOWTOOL);
	 		break;
		default:
			break;
	}
	return (done);
} 

short hndl_menu(short title, short item)
{
	short	done;

	done = false;
	paste_img = false;
	switch (title)
	{
		case DESKMENU:
			if (item == ABOUITEM)
				about_dial();
			break;
		case FILEMENU:
			done = hndl_filemenu(item);
			break;
		case GLOBMENU:
			done = hndl_globmenu(item);
			break;		
		case EDITMENU:
			done = hndl_editmenu(item);
			break;
		case OPTNMENU:
			done = hndl_omenu(item);
			break;
		case ARRMENU:
			done = hndl_hmenu(item);
			break;
	}
	menu_tnormal((OBJECT  *)ad_menu,title,true);
	if ( iconedit_flag)
		set_icnmenus();
	return (done);
}

void hndl_arrowed(short w_handle, short kind)
{
	if (file_view())
	{
		switch (kind)
		{
			case WA_LFPAGE:
			case WA_RTPAGE:
			case WA_LFLINE:
			case WA_RTLINE:
				return;
			case WA_UPPAGE:
				do_trsinc(-fit_vtrees() * fit_htrees());
				break;
			case WA_DNPAGE:
				do_trsinc(fit_vtrees() * fit_htrees());
				break;
			case WA_UPLINE:
				do_trsinc(-fit_htrees());
				break;
			case WA_DNLINE:
				do_trsinc(fit_htrees());
				break;
		}
		redo_trees();
	}
	else
	{
		switch (kind)
		{
			case WA_LFPAGE:
				do_hsinc(-view.g_w);
				break;
			case WA_RTPAGE:
				do_hsinc(view.g_w);
				break;
			case WA_LFLINE:
				do_hsinc(-2 * gl_wchar);
			 	break;
			case WA_RTLINE:
				do_hsinc(2 * gl_wchar);
				break;
			case WA_UPPAGE:
				do_vsinc(-view.g_h);
				break;
			case WA_DNPAGE:
				do_vsinc(view.g_h);
				break;
			case WA_UPLINE:
				do_vsinc(-gl_hchar);
				break;
			case WA_DNLINE:
				do_vsinc(gl_hchar);
				break;
		}
		dslct_obj(ad_view, rcs_sel[0]);
		view_objs();
	}
}

void hndl_hslid(short w_handle, short h_set)
{
	short	x;

	if (!file_view())
	{
		x = max(0, GET_WIDTH(ad_view, ROOT) - view.g_w); 
		x = (short) umul_div(x, h_set, 1000);
		rcs_xpan = clamp_xs(ad_view, snap_xs(x));
		dslct_obj(ad_view, rcs_sel[0]);
		view_objs();
	}
}

void hndl_vslid(short w_handle, short v_set)
{
	short	y;

	if (file_view())
	{
		y = max(0, fit_htrees() * (need_vtrees() - fit_vtrees()));
		y = mul_div(y, v_set, 1000);
		rcs_trpan = clamp_trs(snap_trs(y));
		redo_trees();
	}
	else
	{
		y = max(0, GET_HEIGHT(ad_view, ROOT) - view.g_h); 
		y = (short) umul_div(y, v_set, 1000);
		rcs_ypan = clamp_ys(ad_view, snap_ys(y));
		dslct_obj(ad_view, rcs_sel[0]);
		view_objs();
	}
} 		

static short hndl_msg(void)
{
	short 	done;

	done = false;
	switch ( rcs_rmsg[0] )
	{
		case MN_SELECTED:
			done = hndl_menu(rcs_rmsg[3], rcs_rmsg[4]);
			break;
		case WM_REDRAW:
			hndl_redraw(rcs_rmsg[3], (GRECT *) &rcs_rmsg[4]); 
			break;
		case WM_CLOSED:
			hndl_clos();
			if( rcs_edited )
				set_filemenu();
   			break;
		case WM_TOPPED:
			wind_set(rcs_rmsg[3], WF_TOP, 0, 0, 0, 0);
			break;
		case WM_ARROWED:
			hndl_arrowed(rcs_rmsg[3], rcs_rmsg[4]);
			break;
		case WM_HSLID:
			hndl_hslid(rcs_rmsg[3], rcs_rmsg[4]);
			break;
		case WM_VSLID:
			hndl_vslid(rcs_rmsg[3], rcs_rmsg[4]);
			break;
		default:
			break;
	}
	rcs_rmsg[0] = 0;
	return(done);
} 

short hndl_keybd(short key)
{
	short		title, item, done ;

    done = false ;
    title = FILEMENU ;
    switch ( key )
	{
		case ALT_O :title = GLOBMENU ; 
		    item = OUTPITEM ;
		    break ;
		case ALT_S :title = GLOBMENU ; 
		    item = SAFEITEM ;
		    break ;
		case ALT_R :title = GLOBMENU;
		    item = SVOSITEM;
		    break;	
		case ALT_P :title = GLOBMENU ; 
		    item = PARTITEM ;
		    break ;
		case ALT_H :title = GLOBMENU;
		    item = TOOLITEM ;
		    break ;
		case ALT_I :title = OPTNMENU;
		    item = INFITEM ;
		    break ;
		case ALT_N :title = OPTNMENU;
		    item = RNAMITEM ;
		    break ;    
		case ALT_T :title = OPTNMENU;
		    item = TYPEITEM;
		    break;
		case ALT_L :title = OPTNMENU;
		    item = LOADITEM;
		    break;
		case ALT_E :title = OPTNMENU;
		    item = ICNEDITM;
		    break;
		case ALT_F :title = ARRMENU;
		    item = SRTITEM;
		    break;
		case ALT_U :title = ARRMENU;
		    item = UNHDITEM;
		    break;
		case CTL_W :item = NEWITEM;
		    break;
		case CTL_O :item = OPENITEM;
		    break;
		case CTL_N :item = MERGITEM;
		    break;
		case CTL_C :item = CLOSITEM;
		    break;
		case CTL_V :item = SAVEITEM;
		    break;
		case CTL_M :item = SVASITEM;
		    break;
		case CTL_A :item = RVRTITEM;
		    break;
		case ALT_C :title = EDITMENU;
		    item = CUTITEM;
		    break;
		case ALT_Y :title = EDITMENU;
		    item = COPYITEM;
		    break;
		case ALT_A :title = EDITMENU;
		    item = PASTITEM;
		    break;
		case ALT_D :title = EDITMENU;
		    item = DELITEM;
		    break;
		case CTL_Q :item = QUITITEM;
		    break;	
		default :
			return( done ) ;
	} /* switch */
    if ( ! ( DISABLED & GET_STATE( ad_menu, item ) ) ) 
		done = hndl_menu( title, item ) ;
    return( done ) ;
} /* hndl_keybd */

static void rcs_main(void)
{
	short flags, done; 
	short ev_which, rets[6];

	/* initialize for event loop */
	flags = MU_BUTTON | MU_MESAG | MU_M1 | MU_KEYBD;  
	ad_rmsg = (long)((long)(rcs_rmsg));
	done = false;
	do	
	{
		ev_which = evnt_multi(flags, 0x02, 0xff, 0x01,
			wait_io, wait.g_x, wait.g_y, wait.g_w, wait.g_h,
			0, 0, 0, 0, 0,
			(short *)ad_rmsg, 0,
			&rets[0], &rets[1], &rets[2],
			&rets[3], &rets[4], &rets[5]);

		wind_update(BEG_UPDATE);
		if (ev_which & MU_M1)
			done = (hndl_mouse(rets[0], rets[1]));
		if (ev_which & MU_BUTTON)
			done = (hndl_button(rets[0], rets[1], rets[2], rets[3], rets[5]));
		if (ev_which & MU_MESAG)
			done = (hndl_msg());		       
		if ( ev_which & MU_KEYBD )
			done = hndl_keybd( rets[ 4 ] ) ;
		wind_update(END_UPDATE);
	}
	while (!done) ;
}

void GEMAIN(void)
{
	short	rcs_term;

	rcs_term = rcs_init();
	if ( !rcs_term )
		rcs_main();
	rcs_exit(rcs_term);
}
