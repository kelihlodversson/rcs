   /* external function declarations for rcs2 */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSMENU_H

#define RCSMENU_H

extern	short	in_bar(long tree, short obj);
extern	short	in_menu(long tree, short obj);
extern	short	is_menu(long tree, short obj);
extern  short	in_which_menu(long tree, short obj);
extern	short	menu_n(long tree, short n);
extern	short	bar_n(long tree, short n);
extern	short	get_active(long tree);
extern	short	blank_menu(long tree, short sobj);
extern	void	fix_menu_bar(long tree);
extern	short	menu_ok(long tree, short obj);
extern	void	desk_menufix(long tree);
extern	void	undo_menufix(long tree);

#endif
