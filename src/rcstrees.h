   /* external function declarations for rcstrees.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSTREES_H

#define RCSTREES_H

extern	short	trpan_f(short obj);
extern	short	fit_vtrees(void);
extern	short	fit_htrees(void);
extern	short	need_vtrees(void);
extern	short	snap_trs(short tr);
extern	short	clamp_trs(short tr);
extern	void	do_trsinc(short inc);
extern	void	view_trees(void);
extern  void    slid_trees(void);
extern	void	redo_trees(void);
extern	void	del_tree(short sobj);
extern	void	cut_tree(short sobj, short dup);
extern	void	paste_tree(short dobj, short dx, short dy, short dup);
extern	void	new_tree(short sobj, short dobj, short dx, short dy);
extern	void	dup_tree(short sobj, short dobj, short dx, short dy);
extern  short	mov_tree(short sobj, short dobj, short dx, short dy);
extern	void	open_tree(short obj);
extern	void	clos_tree(void);
extern	void	name_tree(short obj);
extern	void	type_tree(short obj);
extern  void	select_tree(long tree, short obj);
extern	void	dselct_tree(long tree, short obj);

#endif
