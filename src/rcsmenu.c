/*	RCSMENU.C	 1/27/85 - 1/25/85 	Tim Oren		*/
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
#include "rcslib.h"
#include "rcsdata.h"
#include "rcsmenu.h"

short in_bar(long tree, short obj)
{
	short	nobj;

	if (GET_HEAD(tree, ROOT) != get_parent(tree, nobj =
		get_parent(tree, obj)))
		return (false);
	else
		return (child_of(tree, nobj, obj));
}

short in_menu(long tree, short obj)
{
	short	nobj;

	if (GET_TAIL(tree, ROOT) != get_parent(tree, nobj =
		get_parent(tree, obj)))
		return (false);
	else
		return (child_of(tree, nobj, obj));
}

short is_menu(long tree, short obj)
{
	short	nobj;

	if (GET_TAIL(tree, ROOT) !=
		(nobj = get_parent(tree, obj)))
		return (false);
	else
		return (child_of(tree, nobj, obj));
}

short in_which_menu(long tree, short obj)
{
	return is_menu(tree, get_parent(tree, obj));
}

short menu_n(long tree, short n)
{
	short	nobj;

	nobj = GET_TAIL(tree, ROOT);
	return (nth_child(tree, nobj, n));
}

short bar_n(long tree, short n)
{
	short	nobj;

	nobj = GET_HEAD(tree, ROOT);
	return (nth_child(tree, GET_HEAD(tree, nobj), n));
}

short get_active(long tree)
{
	return GET_HEAD(tree, GET_HEAD(tree, ROOT));
}

short blank_menu(long tree, short sobj)
{
	long 	dobj_ptr;

	blank_obj.ob_width = GET_WIDTH(tree, sobj);
	blank_obj.ob_height = gl_hchar;
	dobj_ptr = get_obmem();
	memmove((void *)(dobj_ptr), (void *)(((long)(&blank_obj))), sizeof(OBJECT));
	return (short) ((dobj_ptr - tree) / sizeof(OBJECT));
}

void fix_menu_bar(long tree)
{
	short	the_active, the_menus, the_bar;
	short	bar_obj, menu_obj, x, x1, x2;

	the_menus = GET_TAIL(tree, ROOT);
	the_bar = GET_HEAD(tree, ROOT);
	SET_HEIGHT(tree, the_bar, gl_hchar + 2);
	the_active = get_active(tree);
	SET_Y(tree, the_active, 0);			/* I'll get you  */
	SET_HEIGHT(tree, the_active, gl_hchar + 3);	/* for this, Lee */

	bar_obj = GET_NEXT( tree, GET_HEAD(tree, the_active));/*skip desk title*/
	menu_obj =GET_NEXT( tree, GET_HEAD(tree, the_menus));/*skip acc items */
	x = 0;

	for (; bar_obj != the_active; )
	{
		SET_X(tree, bar_obj, x);
		SET_Y(tree, bar_obj, 0);
		SET_HEIGHT(tree, bar_obj, gl_hchar + 3);	/* be sure! */
		x1 = x + 2 * gl_wchar;
		x2 = full.g_w - GET_WIDTH(tree, menu_obj);
		SET_X(tree, menu_obj, min(x1, x2) );
		x += GET_WIDTH(tree, bar_obj);
		bar_obj = GET_NEXT(tree, bar_obj);
		menu_obj = GET_NEXT(tree, menu_obj);
	}

	SET_WIDTH(tree, the_active, x);
}

short menu_ok(long tree, short obj)
{
	short	n;

	n = in_bar(tree, obj);
	if (n)
		return (n == 1? false: true);
	else
	{
		n = in_menu(tree, obj);
		if (n)
		{
			if (in_which_menu(tree, obj) != 1)
				return (true);
			else
				return (n == 1? true: false);
		}
		else
			return (false);
	}
}

void desk_menufix(long tree)
{
	short	desktitle, offw, the_active,the_menus, drop_dwn1;
	GRECT	ta, dt, tmp;
      
	the_active = get_active(tree);
	desktitle = (*(short *)(OB_HEAD(the_active)));
	ob_relxywh(tree, desktitle, &dt);
	ob_relxywh(tree, the_active, &ta);
	/* if desk title not	*/
	/*   moved then move it	*/
	if (dt.g_x == 0x0)
	{
		offw = 10 * gl_wchar;
		dt.g_x = gl_width - offw  - ta.g_x -  ta.g_x;
		ob_setxywh(tree, desktitle, &dt);
		fix_menu_bar(tree);	
	}		   
	/* move desk menu drop down */	       
	
	the_menus = GET_TAIL(tree, ROOT);
	drop_dwn1 = (*(short *)(OB_HEAD(the_menus)));
	ob_relxywh( tree, drop_dwn1, &tmp);
	tmp.g_x = gl_width - tmp.g_w - ta.g_x;
	ob_setxywh(tree, drop_dwn1, &tmp);

	/* make root and bar longer if needed to cover desktitle */
	if ( GET_WIDTH(tree, ROOT) < gl_width )
		SET_WIDTH( tree, ROOT, gl_width );
	if ( GET_WIDTH(tree, THEBAR) < gl_width )
		SET_WIDTH( tree, THEBAR, gl_width);
}

void undo_menufix(long tree)
{
	short	the_active, the_menus, the_bar;
	short	bar_obj, menu_obj, x, x1, x2;

	the_menus = GET_TAIL(tree, ROOT);
	the_bar = GET_HEAD(tree, ROOT);
	SET_HEIGHT(tree, the_bar, gl_hchar + 2);
	the_active = get_active(tree);
	SET_Y(tree, the_active, 0);			/* I'll get you  */
	SET_HEIGHT(tree, the_active, gl_hchar + 3);	/* for this, Lee */

	bar_obj =  GET_HEAD(tree, the_active);/*skip desk title*/
	menu_obj = GET_HEAD(tree, the_menus);/*skip acc items */
	x = 0;

	for (; bar_obj != the_active; )
	{
		SET_X(tree, bar_obj, x);
		SET_Y(tree, bar_obj, 0);
		SET_HEIGHT(tree, bar_obj, gl_hchar + 3);	/* be sure! */
		x1 = x + 2 * gl_wchar;
		x2 = full.g_w - GET_WIDTH(tree, menu_obj);
		SET_X(tree, menu_obj, min(x1, x2) );
		x += GET_WIDTH(tree, bar_obj);
		bar_obj = GET_NEXT(tree, bar_obj);
		menu_obj = GET_NEXT(tree, menu_obj);
	}

	SET_WIDTH(tree, the_active, x);
}
