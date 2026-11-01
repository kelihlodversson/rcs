   /* external function declarations for rcsimain.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSIMAIN_H

#define RCSIMAIN_H

extern	void	fat2sml(short x[], short y[], short count, short isdata);
extern	void	set_pix(void);
extern	void	set_scroll(void);
extern	void	copy_colplanes(long longaddr, short color, short isdata);
extern	void	dr_image(void);
extern	void	hold_to_undo(void);
extern	void	save_fat(short undo);
extern	void	fattify(GRECT *area);
extern	short	__CDECL	dr_code(PARMBLK *pparmbk);
extern	void	fb_redraw(void);
extern	void	set_bgcol(short item, short invert);
extern	void	set_fgcol(short item,short invert);
extern	void	save_img(void);
extern	void	rubrec_off(void);
extern	void	undo_img(void);
extern	void	iconmain(void);

#endif
