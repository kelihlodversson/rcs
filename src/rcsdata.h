   /* external function declarations for rcsdata.c */
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSDATA_H

#define RCSDATA_H

#include "rcsdefs.h"
#include "rcslib.h"

extern	void	del_objindex(long tree, short obj);
extern	void	count_free(long tree, short obj);
extern	void	dcomp_tree(long tree, short obj);
extern	void	ini_buff(void);
extern	void	clr_tally(void);
extern	void	tally_obj(long tree, short obj);
extern	void	map_all(fkt_parm routine);
extern	short	find_tree(short n);
extern	short	find_obj(long tree, short obj);
extern	char	*get_name(short key);
extern	long	tree_addr(short n);
extern	long	avail_mem(void);
extern	void	tally_free(void);
extern  short	new_index(char *val, short kind, char *name);
extern	void	set_value(short key, char *val);
extern	char	*get_value(short key);
extern	short	get_kind(short key);
extern	short	find_value(char *val);
extern	long	str_addr(short n);
extern	long	img_addr(short n);
extern	long	get_mem(unsigned short bytes);
extern	long	copy_tree(long stree, short obj, short specopy);
extern	short	copy_objs(long stree, short obj, long dtree, short specopy);
extern	void	add_trindex(long tree);
extern  void	set_kind(short key, short kind);
extern	short	find_name(char *name);
extern	short	tree_kind(short kind);
extern	void	unique_name(char *name, char *ptn, short n);
extern	long	tree_ptr(short n);
extern	long	str_ptr(short n);
extern	long	img_ptr(short n);
extern	char	*c_lookup(unsigned short what, LOOKUP *where, unsigned short n, char *punt);
extern	void	copy_spec(long tree, short obj);
extern	void	indx_obs(long tree, short which);
extern	short	fnd_ob(short old);
extern  void	dcomp_free(void);
extern	void	zap_objindex(long tree, short obj);
extern	short	name_ok(char *name, short ok, short nilok);
extern	short	set_obname(long tree, short obj, char *name, long ntree, short nobj);
extern	void	get_obname(char *name, long ntree, short nobj);
extern	long	mak_trindex(short nentry);
extern	long	mak_frstr(short nentry);
extern	long	get_obmem(void);		/* in RCSDATA.C */
extern  void	update_if(long taddr, char *naddr);

#endif
