 /*	RCSDATA.C	12/21/84 - 1/25/85  	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcslib.h"
#include "rcsintf.h"
#include "rcsdata.h"

/* variables used in icon edit mode */
unsigned short	loword, hiword;
GRECT	trans_area;
MFDB	trans_mfdb, tmp_mfdb, tmp2_mfdb, fat_mfdb;  
short	junk;	

  short icn_edited = false, clipped = false, paste_img = false;
	short	iconedit_flag = false, flags,pen_on, grid;
	short	selecton = false, inverted;
	long	save_tree, gl_icnspec;
	short	save_obj, save_state, gl_wimage, gl_himage, gl_datasize;
	GRECT	scrn_area,hold_area,fat_area,src_img,icn_img,dat_img,mas_img;
	GRECT	scroll_fat, gridbx;
	GRECT	clip_area, selec_area, flash_area, dispc_area;
	MFDB	undo_mfdb,und2_mfdb,hold_mfdb,hld2_mfdb,disp_mfdb,scrn_mfdb;
	MFDB	clip_mfdb, clp2_mfdb, save_mfdb, sav2_mfdb;
	long	src_mp, dst_mp;
	long	ibuff_size;
	short	is_mask, gl_isicon;
  short	out1, xwait, ywait, wwait, hwait,out2;
  short	mousex, mousey, bstate, kstate,kreturn, bclicks;
  short    colour,fgcolor, bgcolor, old_fc, deltax, deltay;
	short	gridw = 8, gridh = 8;


unsigned short	bit_mask[16] = { 0x8000, 0x4000, 0x2000, 0x1000,
			 0x0800, 0x0400, 0x0200, 0x0100,
			 0x0080, 0x0040, 0x0020, 0x0010,
			 0x0008, 0x0004, 0x0002, 0x0001 };

unsigned short	color_map[16] = { 0x0000, 0x000f, 0x0001, 0x0002, 0x0004,
		          0x0006, 0x0003, 0x0005, 0x0007, 0x0008,
		          0x0009, 0x000a, 0x000c, 0x000e, 0x000b,
		          0x000d };
  short    invert3[8] = {BLACK,WHITE,CYAN,MAGENTA,YELLOW,RED,BLUE,GREEN};
  short	invert4[16]= {BLACK, WHITE, LCYAN, LMAGENTA, LYELLOW, LRED,
			      LBLUE, LGREEN, LBLACK, LWHITE, CYAN, MAGENTA,
			      YELLOW, RED, BLUE, GREEN};
	char	sav_icnpath[80];
	char	sav_rcspath[80];
	char	rcs_rfile[80];
	char	rcs_dfile[80];
  char	rcs_app[80];
	char	rcs_infile[80];
  char    icn_file[80];
	FILE	*rcs_rhndl;
	FILE	*rcs_dhndl;     
  unsigned short	hard_drive;	/* what is hard drive configuration? */
	short	partp;		/* are parts shown? */
	short	toolp;		/* are tools shown? */
  long	ad_menu;
	long	ad_tools;
  long	ad_view;
	long	ad_pbx;
	long	ad_clip;
  long    ad_itool;
  long    ad_idisp;
	short	rcs_clipkind;
	short	rcs_view;
	GRECT	full;
	GRECT	view;
	GRECT	pbx;
	GRECT	tools;
  GRECT   itool;
  GRECT   idisp;
	short	rcs_trpan;	/* Tree row offset in window	*/
	short	rcs_xpan;	/* (Positive) offset of root	*/
	short	rcs_ypan;
	short	rcs_xtsave;	/* Tree x,y before being opened */
	short	rcs_ytsave;
	short	rcs_mform;	/* Current mouse form */
	short	rcs_hot;	/* Currently active tools */
	GRECT	wait;		/* Current mouse wait rectangle */
	short	wait_io;	/* Waiting for in or out? */

	short	rcs_svfstat;
  short	rcs_state = 0;
  short	icn_state = 0;
	char	rcs_title[80];
	short	rcs_nsel = 0;
	short	rcs_sel[MAXSEL];
	short	rcs_cflag = false;	/* emit .c file or not?	   */
	short	rcs_hflag = true;	/* emit .h file or not?	   */
	short	rcs_oflag = false;	/* emit .o file or not?	   */
	short	rcs_cbflag = false;	/* emit C-BASIC binding?   */
	short	rcs_f77flag = false;	/* emit FORTRAN binding?   */
	short	rcs_fsrtflag = false;   /* sort the binding file?  */
	short	rcs_lock = false;	/* no tree changes? */
	short	rcs_xpert = false;	/* omit warnings? */
	short	rcs_edited = false;	/* quit without warning?  */
	short	rcs_low = false;	/* low memory? */
	short	rcs_panic = false;	/* in deep trouble! */ 
	short	rcs_menusel;		/* only used in MENU_STATE */
  short	rcs_rmsg[8];
  long	ad_rmsg;

	OBJECT	rcs_work[VIEWSIZE+1];	/* Space for workbench objects	*/
	ICONBLK	rcs_icons[VIEWSIZE];	/* Space for workbench iconblks */
	short	rcs_typ2icn[NUM_TYP] = {
	UNKNICON, PANLICON, MENUICON, DIALICON, ALRTICON, FREEICON};
/* Variables used in write_file */
	unsigned short	rcs_wraddr;		/* Current offset in output file */
	unsigned short	wr_obnum;		/* Count of obj in tree		*/
	short	wr_obndx[TRACESIZE];	/* Tree trace index is built here */

  unsigned short	rcs_ndxno = 0;

	INDEX	rcs_index[NDXSIZE];
	DEFLTS	deflt_options[NOPTS];
  char	rsc_path[80];

	short	OK_NOFILE[] = { OPENITEM, MERGITEM, QUITITEM, INFITEM, 
				OUTPITEM, SAFEITEM, SVOSITEM, PARTITEM, TOOLITEM, 0};

	short	OK_FILE[] = {NEWITEM, OPENITEM, MERGITEM, SVASITEM, 
			QUITITEM, INFITEM, OUTPITEM, SAFEITEM, SVOSITEM,
			PARTITEM, TOOLITEM, MERGITEM, CLOSITEM,	0};

	short	OK_TREES[] = {CLOSITEM, QUITITEM, INFITEM, OUTPITEM,SAFEITEM,
			SVOSITEM, PARTITEM, TOOLITEM, 0};
		  
  short	OK_NOICN[] = {NEWITEM, OPENITEM, CLOSITEM, SVASITEM, OUTPITEM,
			SAFEITEM, SVOSITEM,INVITEM,SOLIDIMG,SIZEITEM,0};

  short	OK_ICN[] =  {NEWITEM, OPENITEM, CLOSITEM, SVASITEM, OUTPITEM,
			SAFEITEM, SVOSITEM,INVITEM,SOLIDIMG,SIZEITEM, 0};

  short    OK_EDITED[] = {NEWITEM, SVASITEM,RVRTITEM,0};
		
	short	ILL_LOCK[] = {MERGITEM, RNAMITEM, TYPEITEM, SRTITEM, 
			PASTITEM, CUTITEM, DELITEM, UNHDITEM, FLTITEM, 0};

	short	HOT_IBOX[] = {HOTBDCOL, HOTTHICK, 0};
	short	HOT_BOX[] = {HOTBDCOL, HOTTHICK, HOTBGCOL, HOTPATRN, 0};
	short	HOT_TEXT[] = {HOTFGCOL, HOTRULE, 0};
	short	HOT_IMAGE[] = {HOTFGCOL, 0};
	short	HOT_ICON[] = {HOTFGCOL, HOTBGCOL, 0};
	short	HOT_BTEXT[] = {HOTBDCOL, HOTTHICK, HOTBGCOL, HOTPATRN,
			HOTFGCOL, HOTRULE, 0};

	FILE	*rcs_hhndl;	/* used in write_files and c_ routines */
	char	rcs_hfile[100], hline[80];

	struct tally rcs_tally;

	short	c_obndx[MAPSIZE]; /* maps from disk address to string/image # */
	short	c_nstring;
	short	c_frstr;	/* first string referenced by freestr */
	short	c_nfrstr;
	short	c_nimage;
	short	c_frimg;	/* first image referenced by freebits */
	short	c_nbb;
	short	c_nfrbit;
	short	c_nib;
	short	c_nted;
	short	c_nobs;
	short	c_ntree;

	struct obitmap rcs_bit2obj[] = {
	SELECTABLE, 0, SBLEPOP,
	DEFAULT, 0, DFLTPOP,
	EXIT, 0, EXITPOP,
	RBUTTON, 0, RDIOPOP,
	0, DISABLED, DSBLPOP,
	EDITABLE, 0, EDBLPOP,
	TOUCHEXIT, 0, TCHXPOP,
	HIDETREE, 0, HDDNPOP,
	0, CROSSED, CROSPOP,
	0, CHECKED, CHEKPOP,
	0, OUTLINED, OUTLPOP,
	0, SHADOWED, SHADPOP,
	0, SELECTED, SLCTPOP,
	FL3DIND, 0, D3INDPOP,
	FL3DBAK, 0, D3BAKPOP,
	SUBMENU, 0, SUBMPOP,
	0, WHITEBAK, WBAKPOP,
	0, DRAW3D, EXT3DPOP,
	0, 0, 0 };

	struct popmap rcs_ht2pop[] = {
	HOTBGCOL, POPCOLOR, true,
	HOTPATRN, POPPATRN, true,
	HOTBDCOL, POPCOLOR, true,
	HOTTHICK, POPTHICK, true,
	HOTFGCOL, POPCOLOR, true,
	HOTRULE,  POPRULE,  true,
	HOTPOSN,  POPPOSN,  false,
	HOTSWTCH, POPSWTCH, false,
	0, 0 };
	struct popmap icn_ht2pop[] = {
	FCLORBOX, POPCOLOR, true,
	BCLORBOX, POPCOLOR, true,
	0, 0 };

	struct map rcs_it2thk[] = {
	OUT3POP, -3,
	OUT2POP, -2,
	OUT1POP, -1,
	NONEPOP,  0,
	IN1POP,   1,
	IN2POP,	  2,
	IN3POP,   3,
	0, 0 };

	LOOKUP	c_types[N_TYPES] = {
	G_BOX, "G_BOX",
	G_TEXT, "G_TEXT",
	G_BOXTEXT, "G_BOXTEXT",
	G_IMAGE, "G_IMAGE",
	G_USERDEF, "G_USERDEF",
	G_IBOX, "G_IBOX",
	G_BUTTON, "G_BUTTON",
	G_BOXCHAR, "G_BOXCHAR",
	G_STRING, "G_STRING",
	G_FTEXT, "G_FTEXT",
	G_FBOXTEXT, "G_FBOXTEXT",
	G_ICON, "G_ICON",
	G_TITLE, "G_TITLE" };

	LOOKUP	c_flags[N_FLAGS] = {
	NONE, "NONE",
	SELECTABLE, "SELECTABLE",
	DEFAULT, "DEFAULT",
	EXIT, "EXIT",
	EDITABLE, "EDITABLE",
	RBUTTON, "RBUTTON",
	LASTOB, "LASTOB",
	TOUCHEXIT, "TOUCHEXIT",
	HIDETREE, "HIDETREE",
	INDIRECT, "INDIRECT" };

	LOOKUP	c_states[N_STATES] = {
	NORMAL, "NORMAL",
	SELECTED, "SELECTED",
	CROSSED, "CROSSED",
	CHECKED, "CHECKED",
	DISABLED, "DISABLED",
	OUTLINED, "OUTLINED",
	SHADOWED, "SHADOWED" };

	short	str_types[] = {G_STRING, G_BUTTON};
	short	box_types[] = {G_BOX, G_IBOX, G_BOXCHAR};
	short	txt_types[] = {G_TEXT, G_FTEXT, G_BOXTEXT, G_FBOXTEXT};

	OBJECT	blank_obj = {
	-1, -1, -1, G_BOX, HIDETREE, NORMAL, 0x00FF1100L, 0, 0, 8, 1 };

	char	*hptns[] = {
	"#define %s %d  \t/* TREE */\n",
	"#define %s %d  \t/* STRING */\n",
	"#define %s %d  \t/* OBJECT in TREE #%d */\n",
	"#define %s %d	\t/* FREE STRING */\n",
	"#define %s %d	\t/* FREE IMAGE */\n" };

	char	*optns[] = {
	"      %s = %d;  \t(* TREE *)\n",
	"      %s = %d;  \t(* STRING *)\n",
	"      %s = %d;  \t(* OBJECT in TREE #%d *)\n",
	"      %s = %d;  \t(* FREE STRING *)\n",
	"      %s = %d;  \t(* FREE IMAGE *)\n" };

	char	*bptns[] = {
	"      %s = %d;  REM ---TREE---\n",
	"      %s = %d;  REM ---STRING---\n",
	"      %s = %d;  REM ---OBJECT in TREE #%d---\n",
	"      %s = %d;  REM ---FREE STRING---\n",
	"      %s = %d;  REM ---FREE IMAGE---\n" };

	char	*fptns[] = {
	"\tPARAMETER (%s = %d)\n",
	"\tPARAMETER (%s = %d)\n",
	"\tPARAMETER (%s = %d)\n",
	"\tPARAMETER (%s = %d)\n",
	"\tPARAMETER (%s = %d)\n" };

	long	rs_hdr;
	long	head;
	long	buff_size;
	long	rcs_free;

static	long	d_frstr;		/* These are local to dcomp_free */
static	long	d_frimg;
static	short	d_nfrstr;
static	short	d_nfrimg;

void ini_buff(void)
{
	short	ii;

	for (ii = 0; ii < sizeof(RSHDR); ii++)
		(*(char *)(head + ii) = ('\0'));
	rcs_low = false;
	rcs_panic = false;
	rcs_free = head + sizeof(RSHDR);
}

long tree_ptr(short n)
{
	return (head + ((unsigned short)((*(unsigned short *)(RSH_TRINDEX(head))))) + (long) (n * sizeof(long)));
}  

long str_ptr(short n)
{
	return (head + ((unsigned short)((*(unsigned short *)(RSH_FRSTR(head))))) + (long) (n * sizeof(long)));
}

long img_ptr(short n)
{
	return (head + ((unsigned short)((*(unsigned short *)(RSH_FRIMG(head))))) + (long) (n * sizeof(long)));
}

long tree_addr(short n)
{
	return (*(long *)(tree_ptr(n)));
}  

long str_addr(short n)
{
	return (*(long *)(str_ptr(n)));
}

long img_addr(short n)
{
	return (*(long *)(img_ptr(n)));
}

void set_value(short key, char *val)
{
	rcs_index[key].val = val;
}

void set_kind(short key, short kind)
{
	rcs_index[key].kind = kind;
}

static void set_name(short key, char *name)
{
	if (name == NULL)
		rcs_index[key].name[0] = '\0';
	else
		strcpy(&(rcs_index[key].name[0]), name);
}

short new_index(char *val, short kind, char *name)
{
	if (rcs_ndxno >= NDXSIZE)
		return (NIL);
	else
	{
		set_value(rcs_ndxno, val);
		set_kind(rcs_ndxno, kind);
		set_name(rcs_ndxno, name);
		return (rcs_ndxno++);
	}
}

static void del_index(short key)
{
	rcs_ndxno--;
	if (key < rcs_ndxno)
		memmove((char *)&rcs_index[key],
			(char *)&rcs_index[key+1],
			sizeof(INDEX) * (rcs_ndxno - key));
}

short get_kind(short key)
{
	return rcs_index[key].kind;
}

char *get_value(short key)
{
	return rcs_index[key].val;
}

char *get_name(short key)
{
	return &(rcs_index[key].name[0]);
}

short find_value(char *val)
{
	short	entno;

	for (entno = 0; entno < rcs_ndxno; entno++)
		if (rcs_index[entno].val == val)
			return(entno);
	return (NIL);
}

short find_tree(short n)
{
	return find_value((char *) tree_addr(n));
}

short find_obj(long tree, short obj)
{
	return find_value( (char *) (tree + (unsigned short) (obj * sizeof(OBJECT))) );
}

short find_name(char *name)
{
	short	entno;

	for (entno = 0; entno < rcs_ndxno; entno++)
		if (strcmp(name, &rcs_index[entno].name[0]) == 0 )
			return (entno);
	return (NIL);
}

short tree_kind(short kind)
{
	switch (kind)
	{
		case UNKN:
		case PANL:
		case DIAL:
		case MENU:
		case ALRT:
		case FREE:
			return (true);
		default:
			break;
	}
	return (false);
}

void unique_name(char *name, char *ptn, short n)
{
	do {
		sprintf(name, ptn, n);
		n++;
	} while (find_name(name) != NIL);
}

static short blank_name(char *name)
{
	short	blank, idx;

	blank = true;	  
	for (idx = 0;name[idx] != 0; idx++)
		if (name[idx] != ' ')
		{
			blank = false;
	  		break;
		} 
	return( blank );
}

short name_ok(char *name, short ok, short nilok)
{
	short	hit;

	if (name[0] == '@' || !name[0] || blank_name(name) )
	{
		if (nilok)
			return (true);
		else
		{
			hndl_alert(1, string_addr(STNONAM));
			return (false);
		}
	}
	else
	{
		hit = find_name(name);
		if (hit == NIL || hit == ok)
			return (true);
		else
		{
			hndl_alert(1, string_addr(STNMDUP));
			return (false);
		}
	}
}

short set_obname(long tree, short obj, char *name, long ntree, short nobj)
{
	long	taddr;
	short	where;

	taddr = GET_SPEC(tree, obj);
	(*(long *)(taddr) = (((long)(name))));
	(*(short *)(TE_TXTLEN(taddr)) = (9));
	(*(short *)(TE_TMPLEN(taddr)) = (9));

	where = find_value((char *) (ntree + nobj * sizeof(OBJECT)) );
	if (where == NIL)
		strcpy(name, "@");
	else
		strcpy(name, get_name(where));
	return (where);
}

void get_obname(char *name, long ntree, short nobj)
{
	short	where;
	long	addr;

	addr = ntree + nobj * sizeof(OBJECT);
	where = find_value((char *) addr);
	if (where != NIL)
	{
		if (!name[0] || name[0] == '@')
			del_index(where);
		else
			strcpy(get_name(where), name);
	}
	else
	{
		if (name[0] && name[0] != '@')
		{
			where = new_index((char *)addr, OBJKIND, name);
			if (where == NIL)
				hndl_alert(1, string_addr(STNFULL));
		}
	}
}

void del_objindex(long tree, short obj)
{
	short	where;

	where = find_obj(tree, obj);
	if (where != NIL)
		del_index(where);
}

void zap_objindex(long tree, short obj)
{
	map_tree(tree, obj, GET_NEXT(tree, obj), (fkt_parm)del_objindex);
}

long avail_mem(void)
{
	return (head + buff_size - rcs_free);
}

long get_mem(unsigned short bytes)
{
	long	ret;
	long	left;

	ret = rcs_free;
	if (bytes & 0x1)
		bytes++;
	rcs_free += bytes;
	left = avail_mem();
	if (left < 1000 && !rcs_low)
	{
		hndl_alert(1, string_addr(STLOMEM));
		rcs_low = true;
	}
	if (left <= 0 && !rcs_panic)
	{
		hndl_alert(1, string_addr(STPANIC));
		rcs_panic = true;
	}
	return (ret);
}

long  get_obmem(void)
{
	unsigned short	foo;

	foo = (unsigned short) (rcs_free - ( head + ((unsigned short)((*(unsigned short *)(RSH_OBJECT(head)))))));
	
	foo %= (unsigned short) sizeof(OBJECT);
 	/* synchronize to even OBJECT	*/
	/* boundary w.r.t. first OBJECT */
	if (foo)
		rcs_free += (unsigned short) sizeof(OBJECT) - foo;
	return get_mem((unsigned short) sizeof(OBJECT));
}

void update_if(long taddr, char *naddr)
{
	if (!LLSTRCMP((*(long *)(taddr)), (long)((long)(naddr))))
	{
		(*(long *)(taddr) = (get_mem( (unsigned short) strlen(naddr)+1)));
		LLSTRCPY((long)((long)(naddr)), (*(long *)(taddr)));
	}
}

static long copy_ti(long source)
{
	long	dest;

	dest = get_mem((unsigned short) sizeof(TEDINFO));
	memmove((void *)(dest), (void *)(source), sizeof(TEDINFO));
	return (dest);
}

static long copy_ib(long source)
{
	long	dest;

	dest = get_mem((unsigned short) sizeof(ICONBLK));
	memmove((void *)(dest), (void *)(source), sizeof(ICONBLK));
	return (dest);
}

static long copy_bb(long source)
{
	long	dest;

	dest = get_mem((unsigned short) sizeof(BITBLK));
	memmove((void *)(dest), (void *)(source), sizeof(BITBLK));
	return (dest);
}

void copy_spec(long tree, short obj)
{
	short	type;
	long	obspec;

	type = ((GET_TYPE(tree, obj)) & 0xff);
	obspec = GET_SPEC(tree, obj);
	switch (type)
	{
		case G_TEXT:
		case G_BOXTEXT:
		case G_FTEXT:
		case G_FBOXTEXT:
			obspec = copy_ti(obspec);
			break;
		case G_ICON:
			obspec = copy_ib(obspec);
			break;
		case G_IMAGE:
			obspec = copy_bb(obspec);
			break;
		default:
			return;
	}
	SET_SPEC(tree, obj, obspec);
}

void indx_obs(long tree, short which)
{
	wr_obndx[wr_obnum++] = which;
}

short fnd_ob(short old)
{
	short	new;
	
	for (new = 0; new < wr_obnum && wr_obndx[new] != old; new++)
		;
	return (new);
}

short copy_objs(long stree, short obj, long dtree, short specopy)
{
	short	iobj, root, link;
	long	here, tree;
	unsigned short	thisobj;

	tree = stree;
	wr_obnum = 0;
	map_tree(tree, obj, GET_NEXT(tree, obj), (fkt_parm)indx_obs);

	tree = dtree;
	for (iobj = 0; iobj < wr_obnum; iobj++)
	{
		here = get_obmem();
		memmove((void *)(here), (void *)(stree + (unsigned short) (wr_obndx[iobj] * sizeof(OBJECT))), sizeof(OBJECT));
		thisobj = (unsigned short) ((here - tree) / sizeof(OBJECT));
		if (!iobj)
		{
			root = thisobj;
			SET_NEXT(tree, thisobj, NIL);
		}
		else
			if ( (link = GET_NEXT(tree, thisobj)) != -1L)
				SET_NEXT(tree, thisobj, fnd_ob(link) + root); 
		if ( (link = GET_HEAD(tree, thisobj)) != -1L)
			SET_HEAD(tree, thisobj, fnd_ob(link) + root); 
		if ( (link = GET_TAIL(tree, thisobj)) != -1L)
			SET_TAIL(tree, thisobj, fnd_ob(link) + root); 
	}
	if (specopy)
		map_tree(tree, root, NIL, (fkt_parm)copy_spec);
	return (root);
}

long copy_tree(long stree, short obj, short specopy)
{
	long	dtree;

	get_obmem();
	dtree = (rcs_free -= (unsigned short) sizeof(OBJECT) );	/* a hack which */
	copy_objs(stree, obj, dtree, specopy);			/* saves memory */
	return (dtree);
}

long mak_trindex(short nentry)
{
	long	trindex;
	short	nobj, iobj;
	unsigned short	indoff;

	nobj = (*(unsigned short *)(RSH_NTREE(head)));
	(*(unsigned short *)(RSH_NTREE(head)) = (nobj + nentry));

	trindex = get_mem((unsigned short) sizeof(long) * (nobj + nentry));
	indoff = (unsigned short) (trindex - head);

	for (iobj = 0; iobj < nobj; iobj++)
	{
		(*(long *)(trindex) = (tree_addr(iobj)));
		trindex += (unsigned short) sizeof(long);
	}

	(*(unsigned short *)(RSH_TRINDEX(head)) = (indoff));
	return (trindex);
}

void add_trindex(long tree)
{
	long	trindex;

	trindex = mak_trindex(1);
	(*(long *)(trindex) = (tree));
}

long mak_frstr(short nentry)
{
	long	frstr;
	short	nobj, iobj;
	unsigned short	indoff;

	nobj = (*(unsigned short *)(RSH_NSTRING(head)));
	(*(unsigned short *)(RSH_NSTRING(head)) = (nobj + nentry));

	frstr = get_mem((unsigned short) sizeof(long) * (nobj + nentry));
	indoff = (unsigned short) (frstr - head);

	for (iobj = 0; iobj < nobj; iobj++)
	{		
		(*(long *)(frstr) = (str_addr(iobj)));
		frstr += (unsigned short) sizeof(long);
	}

	(*(unsigned short *)(RSH_FRSTR(head)) = (indoff));
	return (frstr);
}

static long mak_frimg(short nentry)
{
	long	frimg;
	short	nobj, iobj;
	unsigned short	indoff;

	nobj = (*(unsigned short *)(RSH_NIMAGES(head)));
	(*(unsigned short *)(RSH_NIMAGES(head)) = (nobj + nentry));

	frimg = get_mem((unsigned short) sizeof(long) * (nobj + nentry));
	indoff = (unsigned short) (frimg - head);

	for (iobj = 0; iobj < nobj; iobj++)
	{
		(*(long *)(frimg) = (img_addr(iobj)));
		frimg += (unsigned short) sizeof(long);
	}

	(*(unsigned short *)(RSH_FRIMG(head)) = (indoff));
	return (frimg);
}

void count_free(long tree, short obj)
{
	switch (GET_TYPE(tree, obj) & 0xff)
	{
		case G_STRING:
			d_nfrstr++;
			break;
		case G_IMAGE:
			d_nfrimg++;
			break;
		default:
			break;
	}
}

void dcomp_tree(long tree, short obj)
{
	short	ndex;
	long	taddr;

	taddr = GET_SPEC(tree, obj);
	ndex = find_obj(tree, obj);

	switch (GET_TYPE(tree, obj) & 0xff)
	{
		case G_STRING:
			(*(long *)(d_frstr) = (taddr));
			d_frstr += (unsigned short) sizeof(long);
			d_nfrstr++;
			if (ndex == NIL)
				break;
			set_kind(ndex, FRSTR);
			set_value(ndex, (char *) (d_nfrstr - 1));
			break;
		case G_IMAGE:
			(*(long *)(d_frimg) = (taddr));
			d_frimg += (unsigned short) sizeof(long);
			d_nfrimg++;
			if (ndex == NIL)
				break;
			set_kind(ndex, FRIMG);
			set_value(ndex, (char *) (d_nfrimg - 1));
			break;
		default:
			break;
	}
}

void dcomp_free(void)
{
	long	tree;
	short	ntrind, nfrstr, nfrimg, itree, ndex, ntree;

	ntrind = (*(unsigned short *)(RSH_NTREE(head)));
	for (d_nfrimg = d_nfrstr = itree = 0; itree < ntrind; itree++ )
	{
		tree = tree_addr(itree);
		ndex = find_value( (char *) tree);
		if ( get_kind(ndex) == FREE )
			map_tree(tree, ROOT, NIL, (fkt_parm)count_free);
	}
	if (d_nfrstr)
	{
		nfrstr = (*(unsigned short *)(RSH_NSTRING(head)));
		d_frstr = mak_frstr(d_nfrstr);
		d_nfrstr = nfrstr;
	}
	if (d_nfrimg)
	{
		nfrimg = (*(unsigned short *)(RSH_NIMAGES(head)));
		d_frimg = mak_frimg(d_nfrimg);
		d_nfrimg = nfrimg;
	} 
	for (ntree = itree = 0; itree < ntrind; itree++ )
	{
		tree = tree_addr(itree);
		ndex = find_value( (char *) tree);
		if ( get_kind(ndex) == FREE )
		{
			map_tree(tree, ROOT, NIL, (fkt_parm)dcomp_tree);
			del_objindex(tree, ROOT);
		}
		else
		{
			if (ntree != itree)
				(*(long *)(tree_ptr(ntree)) = (tree_addr(itree)));
			ntree++;
		}
	}

	(*(unsigned short *)(RSH_NTREE(head)) = (ntree));
}

char *c_lookup(unsigned short what, LOOKUP *where, unsigned short n, char *punt)
{
	unsigned short	i;

	for (i = 0; i < n; i++)
		if (what == where->l_val)
			return (&where->l_name[0]);
		else
			where++;
	sprintf(punt, "0x%X", (unsigned short)(long)&what);
	return (punt);
}

void map_all(short (*routine)())
{
	short	ntree, itree;

	ntree = (*(unsigned short *)(RSH_NTREE(head)));

	for (itree = 0; itree < ntree; itree++)
		map_tree( tree_addr(itree), 0, -1, routine);
}

void clr_tally(void)
{
	rcs_tally.nobj = 0;
	rcs_tally.nib = 0;
	rcs_tally.nbb = 0;
	rcs_tally.nti = 0;
	rcs_tally.nimg = 0;
	rcs_tally.nstr = 0;
	rcs_tally.nbytes = 0;
}

static void tally_str(long addr)
{
	if (addr != -1L)
	{
		rcs_tally.nstr++;
		rcs_tally.nbytes += (unsigned short) (strlen((char *)(addr)) + 1);
	}
}

static void tally_bb(long addr)
{
	rcs_tally.nbb++;
	rcs_tally.nbytes += (unsigned short) sizeof(BITBLK);
	if ((*(long *)(BI_PDATA(addr))) != -1L)
	{
		rcs_tally.nimg++;
		rcs_tally.nbytes += (unsigned short) ((*(short *)(BI_WB(addr))) * (*(short *)(BI_HL(addr))));
	}
}

void tally_free(void)
{
	long	*free;
	short	i;

	rcs_tally.nstr += (*(unsigned short *)(RSH_NSTRING(head)));
	rcs_tally.nbytes += (unsigned short) ((*(unsigned short *)(RSH_NSTRING(head))) * sizeof(long));
	free = (long *) ( head + ((unsigned short)((*(unsigned short *)(RSH_FRSTR(head))))) );
	for (i = 0; i < (*(unsigned short *)(RSH_NSTRING(head))); i++)
		tally_str( *(free + i));

	rcs_tally.nbb += (*(unsigned short *)(RSH_NIMAGES(head)));
	rcs_tally.nbytes += (unsigned short) ((*(unsigned short *)(RSH_NIMAGES(head))) * 
		(sizeof(long) + sizeof(BITBLK)));
  	free = (long *) ( head + ((unsigned short)((*(unsigned short *)(RSH_FRIMG(head))))) );
	for (i = 0; i < (*(unsigned short *)(RSH_NIMAGES(head))); i++)
		tally_bb( *(free + i));
}

void tally_obj(long tree, short obj)
{
	long 	taddr; 
	short	size;

	rcs_tally.nobj++;
	rcs_tally.nbytes += (unsigned short) sizeof(OBJECT);
	taddr = GET_SPEC(tree, obj);
	switch (((GET_TYPE(tree, obj)) & 0xff))
	{
		case G_STRING:
		case G_BUTTON:
		case G_TITLE:
			if (taddr == -1L)
				return;
			tally_str(taddr);
			return;
		case G_TEXT:
		case G_BOXTEXT:
		case G_FTEXT:
		case G_FBOXTEXT:
			if (taddr == -1L)
				return;
			rcs_tally.nti++;
			rcs_tally.nbytes += (unsigned short) sizeof(TEDINFO);
			tally_str((*(long *)(TE_PTEXT(taddr))));
			tally_str((*(long *)(TE_PVALID(taddr))));
			tally_str((*(long *)(TE_PTMPLT(taddr))));
			return;
		case G_IMAGE:
			if (taddr == -1L)
				return;
			tally_bb(taddr);
			return;
		case G_ICON:
			if (taddr == -1L)
				return;
			rcs_tally.nib++;
			rcs_tally.nbytes += (unsigned short) sizeof(ICONBLK);
			size = ((*(short *)(IB_WICON(taddr))) + 7) / 8;
			size *= (*(short *)(IB_HICON(taddr)));
			if ((*(long *)(IB_PDATA(taddr))) != -1L)
			{
				rcs_tally.nimg++;
				rcs_tally.nbytes += (unsigned short) size;
			}
			if ((*(long *)(IB_PMASK(taddr))) != -1L)
			{
				rcs_tally.nimg++;
				rcs_tally.nbytes += (unsigned short) size;
			}
			tally_str((*(long *)(IB_PTEXT(taddr))));
			return;
		default:
			return;
	}
}
