   /* external function declarations for rcsobjs.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSOBJS_H

#define RCSOBJS_J

extern	short	snap_xs(short x);
extern	short	snap_ys(short y);
extern	short	clamp_xs(long tree, short x);
extern	short	clamp_ys(long tree, short y);
extern	void	do_hsinc(short inc);
extern	void	do_vsinc(short inc);
extern	void	set_rootxy(long tree);
extern  short	newsize_obj(long tree, short obj, short neww, short newh, short ok);
extern  void	slid_objs(void);
extern	void	view_objs(void);
extern	void	type_obj(short obj);
extern	void	del_ob(short sobj);
extern  short	posn_obj(long tree, short obj, short *px, short *py, short w, short h, short exact_ok);
extern	void	cut_obj(short sobj, short dup);
extern	void	paste_obj(short dobj, short dx, short dy, short dup);
extern	short	new_obj(long stree, short sobj, short dobj, short dx, short dy);
extern	void	mov_obj(short sobj, short dobj, short dx, short dy);
extern  void	size_obj(long tree, short obj);
extern	void	slct_obj(long tree, short obj);
extern	void	mslct_obj(long tree, short obj);
extern	void	dslct_1obj(long tree, short obj);
extern	void	map_dslct(long tree, short obj);
extern	void	dslct_obj(long tree, short obj);
extern	void	unhid_part(short obj);
extern	void	sort_part(short obj);
extern	void	flatten_part(short obj);

#endif
