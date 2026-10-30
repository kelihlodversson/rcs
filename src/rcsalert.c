/*	RCSALERT.C	 1/17/85 - 1/17/85	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <stdbool.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcslib.h"
#include "rcsalert.h"

void pt_parse(long tree, short obj0, short nobj, long where, short *here)
{
	short	iobj;
	char	tmp;

	tmp = 0;
	for (iobj = 0; tmp != ']' && iobj < nobj; iobj++)
	{
		SET_SPEC(tree, obj0 + iobj, where + *here);
		tmp = (*(char *)(where + *here));
		while (tmp != ']' && tmp != '|')
		{
			*here += 1;
			tmp = (*(char *)(where + *here));
		}
		(*(char *)(where + *here) = ('\0'));
		*here += 1;
	}
}

void parse_alert(long tree, long where)
{
	long	stree, obspec;
	short	icon, curr_id;

	icon = (*(char *)(where + 1)) - '0';
	if (icon < 1 || icon > 3)
		obspec = -1L;
	else
	{
		ini_tree(&stree, ALRTPBX);
		obspec = GET_SPEC(stree, APBXIMG0 + icon - 1);
	}
	SET_SPEC(tree, ALRTIMG, obspec);
	curr_id = 4;
	pt_parse(tree, ALRTSTR0, 5, where, &curr_id);
	curr_id++;
	pt_parse(tree, ALRTBTN0, 3, where, &curr_id);
}

short is_alert(long where)
{
	return ((*(char *)(where)) == '[' && (*(char *)(where + 2)) == ']');
}

void comp_alerts(long hdr)
{
	long	frstr, trindex, where, tree, maddr;
	short	nalert, nfrstr, istr, ndex;
	char	name[9];

	ini_tree(&maddr, NEWALRT);
	frstr = hdr + ((short)((*(short *)(RSH_FRSTR(hdr)))));
	nfrstr = (*(short *)(RSH_NSTRING(hdr)));
	for (nalert = istr = 0; istr < nfrstr; istr++)
	{
		where = (*(long *)(frstr + (unsigned short) (istr * sizeof(long))));
		if (is_alert(where))
			nalert++;
	}
	if (nalert)
	{
		trindex = mak_trindex(nalert);

		for (istr = 0; istr < (*(short *)(RSH_NSTRING(hdr))); istr++)
		{
			where = (*(long *)(frstr + (unsigned short) (istr * sizeof(long))));
			if (is_alert(where))
			{
				tree = copy_tree(maddr, ROOT, true);
				parse_alert(tree, where);
				(*(long *)(frstr + (unsigned short) (istr * sizeof(long))) = (-1L));
				fix_alert(tree);
				(*(long *)(trindex) = (tree));
				trindex += (unsigned short) sizeof(long);
				if ((ndex = find_value((char *) where)) != NIL)
					set_value(ndex, (char *) tree);
				else
				{
					unique_name(&name[0], "ALERT%hd", istr);
					ndex = new_index((char *) tree, ALRT, name);
				}
			}
		}
		if (nalert == (*(short *)(RSH_NSTRING(hdr))))
			(*(short *)(RSH_NSTRING(hdr)) = (0));
	} 
}

void pt_unparse(long tree, short obj0, short nobj, long where, short *here)
{
	long	obspec;
	short	iobj, lstr;

	(*(char *)(where + *here) = ('['));
	*here += 1;
	for (iobj = 0; iobj < nobj; iobj++)
	{
		if ( (obspec = GET_SPEC(tree, obj0 + iobj)) == -1L)
			break;
		else
		{
			lstr = (short)strlen((char *)(obspec));
			if ( !lstr )
				break;
			else
			{
				if (iobj)
				{
					(*(char *)(where + *here) = ('|'));
					*here += 1;
				}
				LLSTRCPY(obspec, where + *here);
				*here += lstr;
			}
		}
	}
	(*(char *)(where + *here) = (']'));
	*here += 1;
}

void unparse_alert(long tree, long where)
{
	long	stree, obspec;
	short	picnum, here;

	obspec = GET_SPEC(tree, ALRTIMG);
	if (obspec == -1L)
		picnum = 0;
	else
	{
		ini_tree(&stree, ALRTPBX);
		for (picnum = 2; picnum >= 0; picnum--)
			if (obspec == GET_SPEC(stree, APBXIMG0 + picnum))
				break;
		picnum++;
	}
	(*(char *)(where) = ('['));
	(*(char *)(where + 1) = ('0' + (char) picnum));
	(*(char *)(where + 2) = (']'));
	here = 3;
	pt_unparse(tree, ALRTSTR0, 5, where, &here);
	pt_unparse(tree, ALRTBTN0, 3, where, &here);
	(*(char *)(where + here) = ('\0'));
	get_mem(here + 1);
}

void dcomp_alerts(void)
{
	long	frstr, where, tree;
	short	nalert, ntrind, nfrstr, itree, ntree, ndex;

	ntrind = (*(short *)(RSH_NTREE(head)));
	for (nalert = itree = 0; itree < ntrind; itree++)
	{
		ndex = find_value((char *) tree_addr(itree));
		if (ndex != NIL && get_kind(ndex) == ALRT)
			nalert++;
	}

	if (nalert)
	{
		nfrstr = (*(short *)(RSH_NSTRING(head)));
		frstr = mak_frstr(nalert);

		for (ntree = itree = 0; itree < ntrind; itree++)
		{
			tree = tree_addr(itree);
			ndex = find_value((char *) tree);
			if (ndex == NIL || get_kind(ndex) != ALRT)
			{
				if (ntree != itree)
					(*(long *)(tree_ptr(ntree)) = (tree_addr(itree)));
				ntree++;
			}
			else
			{
				where = get_mem(0);
				unparse_alert(tree, where);
				(*(long *)(frstr) = (where));
				frstr += (unsigned short) sizeof(long);
				set_value(ndex, (char *) nfrstr++);
			}
		}
		(*(short *)(RSH_NTREE(head)) = (ntree));
	}
}

short pt_roomp(long tree, short obj0, short nobj)
{
	short	iobj;
	long	taddr;

	for (iobj = 0; iobj < nobj; iobj++)
	{
		taddr = GET_SPEC(tree, obj0 + iobj);
		if (taddr == -1L)
			return (obj0 + iobj);
		else if (!strlen((char *)(taddr)))
			return (obj0 + iobj);
	}
	return (0);
} 

void pt_count(long tree, short obj0, short nobj, short *n, short *mxl)
{
	long	taddr;
	short	iobj, slen;

	*n = *mxl = 0;
	for (iobj = 0; iobj < nobj; iobj++)
	{
		taddr = GET_SPEC(tree, obj0 + iobj);
		if (taddr != -1L)
		{
			slen = (short)strlen((char *)(taddr));
			if (slen)
			{
				if (*n != iobj)
				{
					SET_SPEC(tree, obj0 + *n, taddr);
					SET_SPEC(tree, obj0 + iobj, -1L);
				}
				*n += 1;
				*mxl = max(*mxl, slen);
			}
		}
		hide_obj(tree, obj0 + iobj);
	}
}

void al_count(long tree, short *pic, short *nmsg, short *mxlmsg, short *nbut, short *mxlbut)
{
	*pic = (GET_SPEC(tree, ALRTIMG) != -1L);
	hide_obj(tree, ALRTIMG);
	pt_count(tree, ALRTSTR0, 5, nmsg, mxlmsg);
	pt_count(tree, ALRTBTN0, 3, nbut, mxlbut);
} 

void al_space(long tree, short haveicon, short nummsg, short mlenmsg, short numbut, short mlenbut)
{
	short		i;
	GRECT		al, ic, bt, ms;

	rc_set(&al, 0, 0, 1+INTER_WSPACE, 1+INTER_HSPACE);
	rc_set(&ms, 1 + INTER_WSPACE, 1 + INTER_HSPACE, mlenmsg, 1);

	if (haveicon)
	{
		rc_set(&ic, 1+INTER_WSPACE, 1+INTER_HSPACE, 4, 4);
		al.g_w += ic.g_w + INTER_WSPACE;
		al.g_h += ic.g_h + INTER_HSPACE + 1;
		ms.g_x = ic.g_x + ic.g_w + INTER_WSPACE;
	}

	al.g_w += ms.g_w + INTER_WSPACE + 1;
	rc_set(&bt, al.g_w, 1 + INTER_HSPACE, mlenbut, 1);

	al.g_w += bt.g_w + INTER_WSPACE + 1;
	al.g_h = max(al.g_h, 2 + (2 * INTER_HSPACE) + nummsg );
	al.g_h = max(al.g_h, 2 + INTER_HSPACE + (numbut * 2) - 1);
	ob_setxywh(tree, ROOT, &al);		/* init. root object	*/
						/* add icon object	*/
	if (haveicon)
	{
		ob_setxywh(tree, 1, &ic);
		unhide_obj(tree, ALRTIMG);
	}
	/* add msg objects	*/
	for(i=0; i<nummsg; i++)
	{
		ob_setxywh(tree, ALRTSTR0+i, &ms);
		ms.g_y++;
		unhide_obj(tree, ALRTSTR0+i);
	}
	/* add button objects	*/
	for(i=0; i<numbut; i++)
	{
		SET_FLAGS(tree, ALRTBTN0 + i, SELECTABLE | EXIT);
		SET_STATE(tree, ALRTBTN0 + i, NORMAL);
		ob_setxywh(tree, ALRTBTN0+i, &bt);
		bt.g_y += 2;
	}
	/* set last object flag	*/
	SET_FLAGS(tree, ALRTBTN0 + numbut - 1, SELECTABLE | EXIT | LASTOB);
}

void fix_alert(long tree)
{
	short	pic, pnummsg, plenmsg, pnumbut, plenbut;

	al_count(tree, &pic, &pnummsg, &plenmsg, &pnumbut, &plenbut);
	al_space(tree, pic, pnummsg, plenmsg, pnumbut, plenbut);
	map_tree(tree, ROOT, NIL, (fkt_parm)rs_obfix);
	SET_X(tree, ROOT, view.g_x);
	SET_Y(tree, ROOT, view.g_y);
}
