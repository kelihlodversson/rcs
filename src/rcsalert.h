   /* external function declarations for rcsalert.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSALERT_H

#define RCSALERT_H

extern	void	pt_parse(long tree, short obj0, short nobj, long where, short *here);
extern	void	parse_alert(long tree, long where);
extern	short	is_alert(long where);
extern	void	comp_alerts(long hdr);
extern	void	pt_unparse(long tree, short obj0, short nobj, long where, short *here);
extern	void	unparse_alert(long tree, long where);
extern	void	dcomp_alerts(void);
extern	short	pt_roomp(long tree, short obj0, short nobj);
extern	void	pt_count(long tree, short obj0, short nobj, short *n, short *mxl);
extern	void	al_count(long tree, short *pic, short *nmsg, short *mxlmsg, short *nbut, short *mxlbut);
extern	void	al_space(long tree, short haveicon, short nummsg, short mlenmsg, short numbut, short mlenbut);
extern	void	fix_alert(long tree);

#endif
