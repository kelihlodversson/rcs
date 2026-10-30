  /* external function declarations for rcsinit.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSINIT_H

#define RCSINIT_H

extern	short	ini_rsrc(void);
extern	short	ini_windows(short view_parts);
extern	void	rd_inf(short flag);
extern	short	rcs_init(void);
extern	short	cont_rcsinit(short flag);
extern	void	rcs_exit(short term_type);

#endif
