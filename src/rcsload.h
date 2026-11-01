   /* external function declarations for rcsload.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSLOAD_H

#define RCSLOAD_H

extern	void	just_name(char *pname);
extern	char	dig_to_char(short number);
extern	short	writ_icon(long praster, short icon_w, short icon_h, short data_size, short data_field);
extern	void	get_icnfile(char *full_path, short label);
extern	short	char_to_dig(char chr);
extern	void	get_number(char *ptr, short *result);
extern	long	read_icon(char *buffer, short *icon_w, short *icon_h, short *data_size, char *icon_name, short mask, short *icn_size);
extern	short	bb_get(long taddr, short *icon_w, short *icon_h);
extern	short	ib_get(long taddr, short *icon_w, short *icon_h, short maskp, short *file_size);
extern	void	load_part(short obj);

#endif
