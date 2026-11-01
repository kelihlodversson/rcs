   /* external function declarations for rcsvdi.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSVDI_H

#define RCSVDI_H

typedef struct wsstr
{
	short		ws_xres;
	short		ws_yres;
	short		ws_noscale;
	short		ws_wpixel;
	short		ws_hpixel;
	short		ws_ncheights;
	short		ws_nlntypes;
	short		ws_nlnwidths;
	short		ws_nmktypes;
	short		ws_nmksizes;
	short		ws_nfaces;
	short		ws_npatts;
	short		ws_nhatchs;
	short		ws_ncolors;
	short		ws_ngdps;
	short		ws_supgdps[10];
	short		ws_attgdps[10];
	short		ws_color;
	short		ws_rotate;
	short		ws_fill;
	short		ws_cell;
	short		ws_npals;
	short		ws_nloc;
	short		ws_nval;
	short		ws_nchoice;
	short		ws_nstring;
	short		ws_type;
	short		ws_pts0;
	short		ws_chminh;
	short		ws_pts2;
	short		ws_chmaxh;
	short		ws_lnminw;
	short		ws_pts5;
	short		ws_lnmaxw;
	short		ws_pts7;
	short		ws_pts8;
	short		ws_mkminw;
	short		ws_pts10;
	short		ws_mkmaxw;
} WS;

extern	void	gr_rect(short icolor, short ipattern, GRECT *pt);
extern	void	gsx_sclip(GRECT *pt);
extern	void	gsx_bxpts(GRECT *pt);
extern	void	gsx_attr(short text, short mode, short color);
extern	void	gsx_xbox(GRECT *pt);
extern	void	gsx_untrans(long saddr, short swb, long daddr, short dwb, short h);
extern	void	gsx_trans(long saddr, short swb, long daddr, short dwb, short h);
extern	void	gsx_outline(GRECT *pt);
extern	void	gsx_invert (GRECT *pt);
extern	short	ch_width(short fn);
extern	short	ch_height(short fn);
extern	short	gsx_start(void);
extern	void	gsx_vopen(void);
extern	void	bb_save(GRECT *ps);
extern	void	bb_restore(GRECT *pr);

#endif
