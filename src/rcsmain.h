  /* external function declarations for rcsmain */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSMAIN_H

#define RCSMAIN_H

extern	short	hndl_menu(short title, short item);
extern	void	send_redraw(short w_handle, GRECT *p);
extern	void	hndl_dsel(void);
extern	void	obj_redraw(long tree, short obj);
extern	void	obj_nowdraw(long tree, short obj);
extern	void	do_redraw(short w_handle, GRECT *dirty, long tree, GRECT *area);
extern	void	hndl_redraw(short w_handle, GRECT *c);
extern	void	hndl_arrowed(short w_handle, short kind);
extern	void	hndl_hslid(short w_handle, short v_set);
extern	void	hndl_vslid(short w_handle, short v_set);
extern	short	hndl_keybd(short key);
extern	void	GEMAIN(void);

#endif
