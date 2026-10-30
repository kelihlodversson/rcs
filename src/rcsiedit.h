   /* external function declarations for rcsiedit.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSIEDIT_H

#define RCSIEDIT_H

extern	void	del_img(short mouse, short which);
extern	void	clear_clipb(void);
extern	void	draw_outline(GRECT *area);
extern	void	draw_clipb(short mouse, GRECT *dirty);
extern	void	copy_img(short mouse);
extern	void	cut_img(void);
extern	void	do_paste_img(void);
extern	void	solid_img(void);
extern	void	invrt_img(void);
extern	void	icon_size(short *pwidth, short *pheight);
extern	void	horiz_scale(char *srcpt, short srcw, char *despt);
extern	void	scale_img(long srcpt, short srcw, short srch, long despt);
extern	void	keep_img(long srcpt, short srcw, short srch, long despt);
extern	void	resize(long src_pt, short src_w, short src_h, long des_pt);

#endif
