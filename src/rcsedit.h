   /* external function declarations for rcsedit.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSEDIT_H

#define RCSEDIT_H

extern	void	set_text(long tree, short obj, long addr, short len);
extern	void	ted_set(long taddr, char *tmplt, char *valid, char *text);
extern	void	ted_get(long taddr, char *tmplt, char *valid, char *text);
extern	void	icon_set(long taddr, char *text, char *chr);
extern	void	icon_get(long taddr, char *text, char *chr);
extern	short	icon_cwhich(short xy, short wh, short iwh);
extern	short	icon_twhich(short xy, short iwh);
extern	short	icon_tapply(short rule, short wh, short iwh);
extern	short	icon_capply(short rule, short wh, short iwh);
extern	void	get_obf(long tree, short obj, short *type, long *obspec);
extern	void	get_fields(long tree, short sobj, short *type, long *obspec, GRECT *p);
extern	void	edit_str(long tree, short sobj);
extern	void	edit_box(long tree, short sobj);
extern	void	edit_text(long tree, short sobj);
extern	void	edit_bit(long tree, short sobj);
extern	void	edit_icon(long tree, short sobj);
extern	void	open_obj(short sobj);
extern	void	do_bdcol(long tree, short obj, short item);
extern	void	do_bgcol(long tree, short obj, short item);
extern	void	do_fgcol(long tree, short obj, short item);
extern	void	do_patrn(long tree, short obj, short item);
extern	void	do_thick(long tree, short obj, short item);
extern	void	do_rule(long tree, short obj, short item);
extern	void	do_posn(long tree, short obj, short item);
extern	void	do_swtch(long tree, short obj, short item);

#endif
