  /* external function declarations for rcsfiles.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSFILES_H

#define RCSFILES_H

extern	void	get_path(char *tmp_path, char *spec);
extern	void	r_to_xfile(char *where, char *ext);
extern	short	get_file(short label);
extern	short	rvrt_files(void);
extern	short	ok_rvrt_files(void);
extern	void	rsc_title(void);
extern	short	abandon_button(void);
extern	short	new_rsc(void);
extern	short	open_rsc(void);
extern	short	clos_rsc(void);
extern	void	save_rsc(void);
extern	short	svas_rsc(void);
extern	short	rvrt_rsc(void);
extern	void	merge_rsc(void);
extern	short	quit_rsc(void);

#endif
