   /* external function declarations for rcslib.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSLIB_H

#define RCSLIB_H

#include <stdio.h>
#include <gem.h>

typedef short (*fkt_parm)();

extern	void	set_slsize(short w_handle, short h_size, short v_size);
extern	void	set_slpos(short w_handle, short h_pos, short v_pos);
extern	short	ini_tree(long *tree, short which);
extern	long	image_addr(short which);
extern	long	string_addr(short which);
extern	void	arrange_tree(long tree, short wint, short hint,short *maxw, short *maxh);
extern	short	get_parent(long tree, short obj);
extern	short	nth_child(long tree, short nobj, short n);
extern	short	child_of(long tree, short nobj, short obj);
extern	void	table_code(long tree, short obj0, short *types, short ntypes, short type);
extern	short	encode(long tree, short ob1st, short num);
extern	void	map_tree(long tree, short this, short last, fkt_parm routine);
extern	void	snap_xy(short *x, short *y);
extern	void	snap_wh(short *w, short *h);
extern  void	text_wh(long taddr, short type, short *w, short *h);
extern  void	icon_wh(long taddr, short *w, short *h);
extern	void	icon_tfix(long taddr);
extern	void	trans_bitblk(long obspec);
extern	void	rast_op(short mode, GRECT *s_area, MFDB *s_mfdb, GRECT *d_area, MFDB *d_mfdb);
extern	void	outl_obj(long tree, short obj, GRECT *clprect);
extern	void	invert_obj(long tree, short obj, GRECT *clprect);
extern	void	trans_obj(long tree, short obj);
extern	void	unfix_chpos(long where, short x_or_y);
extern	void	rs_obfix(long tree, short curob);
extern	void	enab_obj(long tree, short which);
extern	void	disab_obj(long tree, short which);
extern	void	sel_obj(long tree, short which);
extern	void	desel_obj(long tre, short which);
extern	void	chek_obj(long tree, short which);
extern	void	unchek_obj(long tree, short which);
extern	void	hide_obj(long tree, short which);
extern	void	unhide_obj(long tree, short which);
extern	void	sble_obj(long tree, short which);
extern	void	unsble_obj(long tree, short which);
extern	void	objc_xywh(long tree, short obj, GRECT *p);
extern	void	ob_setxywh(long tree, short obj, GRECT *pt);
extern	void	ob_relxywh(long tree, short obj, GRECT *pt);
extern	short	min(short a, short b);
extern	short	max(short a, short b);
extern  void	strup(char *p1);
extern	void	LLSTRCPY(long src, long dest);
extern	short	LLSTRCMP(long l1, long l2);
extern	void	LWINC(long x);
extern  short	dmcopy(FILE *stream, long msrc, long mdest, short mln);
extern	short	GET_NEXT(long tree, short x);
extern	short	GET_HEAD(long TREE, short x);
extern	short	GET_TAIL(long tree, short x);
extern	short	GET_TYPE(long tree, short x);
extern	short	GET_FLAGS(long tree, short x);
extern	short	GET_STATE(long tree, short x);
extern	long	GET_SPEC(long tree, short x);
extern	short	GET_X(long tree, short x);
extern	short	GET_Y(long tree, short x);
extern	short	GET_WIDTH(long tree, short x);
extern	short	GET_HEIGHT(long tree, short x);
extern	void	SET_NEXT(long tree, short x, short val);
extern	void	SET_HEAD(long tree, short x, short val);
extern	void	SET_TAIL(long tree, short x, short val);
extern	void	SET_FLAGS(long tre, short x, short val);
extern	void	SET_STATE(long tree, short x, short val);
extern	void	SET_SPEC(long tree, short x, long val);
extern	void	SET_X(long tree, short x, short val);
extern	void	SET_Y(long tree, short x, short y);
extern  void	SET_WIDTH(long tree, short x, short y);
extern	void	SET_HEIGHT(long tree, short x, short y);

extern	void	rc_set(GRECT *r, short x, short y, short w, short h);
extern	void	rc_get(const GRECT *r, short *x, short *y, short *w, short *h);
extern	short	rc_inside(const GRECT *r, short x, short y);
extern	void	rc_union(const GRECT *src, GRECT *dst);

#endif
