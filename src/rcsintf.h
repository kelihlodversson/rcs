   /* external function declarations for rcsintf.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RSCINTF_H

#define RCSINTF_H

extern  short	hndl_dial(long tree, short def, GRECT *p);
extern  short	hndl_alert(short b_num, long addr);
extern	void	hndl_locked(void);
extern	void	mouse_form(short f_num);
extern	void	ini_panes(void);
extern	void	view_parts(void);
extern	void	set_menus(void);
extern	void 	menu_opts(long tree, short obj);
extern	void	clr_hot(void);
extern	void	set_hot(void);
extern	void	set_switch(void);
extern	void	new_state(short new);
extern	short	file_view(void);
extern	short	tree_view(void);
extern	void	clr_clip(void);
extern	void	set_title(char *new);
extern	void	get_defpath(char *path, char *spec);
extern	void	clr_title(void);
extern	void	hot_off(void);
extern	void	wait_tools(void);
extern	void	wait_obj(long tree, short obj);
extern	short	pane_find(long *tree, short *obj, short mx, short my);
extern	short	hndl_pop(short obj, short itool);
extern	void	hot_dragbox(GRECT *p, GRECT *pc, short *mx, short *my, short hot);
extern	void	clamp_rubbox(short x, short y, short wlow, short hlow, short whigh, short hhigh,
	long ahigh, short *w, short *h, GRECT *bound);
extern	void	obj_handle(long tree, short obj, GRECT *p);
extern	void	about_dial(void);
extern	void	info_dial(short obj);
extern	void	read_inf(short flag);
extern	void	wrte_inf(void);
extern	void	outp_dial(void);
extern	void	safe_dial(void);
extern	void	ini_prog(void);
extern	void	show_prog(short strno);

#endif
