/* 	RCSDEFS.H	Tim Oren	10/08/84 - 05/21/85	*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSDEFS_H

#define RCSDEFS_H

#include <stddef.h>
#include <stdio.h>

/* variables used in icon edit mode */

extern	unsigned short	loword, hiword;
extern	GRECT	trans_area;
extern	MFDB	trans_mfdb, tmp_mfdb, tmp2_mfdb;  
extern	short	junk;	
extern	short	out1, xwait, ywait, wwait, hwait,out2;
extern	short	mousex, mousey, bstate, kstate,kreturn, bclicks;
extern	unsigned short bit_mask[];
extern	short	icn_edited, iconedit_flag, gl_isicon, is_mask, flags,pen_on;
extern	long	save_tree, gl_icnspec;
extern	short	save_obj, save_state, gl_wimage, gl_himage,  gl_datasize;
extern	GRECT	scrn_area, hold_area, src_img, fat_area;
extern	GRECT	scroll_fat, icon_area, data_area, mask_area, gridbx;
extern	MFDB	undo_mfdb,hold_mfdb, scrn_mfdb;
extern	long	src_mp, dst_mp;
extern	long	ibuff_size;
extern  short    fgcolor, bgcolor;
#define  MAX_DATA   256

/*use word length not long length*/
#define dos_read(x,y,z) read_piece(x,y,z)
#define dos_write(x,y,z) write_piece(x,y,z)

#define X_FWD 0x0100		/* extended object type codes */
#define ADJ3D 2		/* pixels the AES adds around 3D indicators and activators */
#define X_BAK 0x0200		/* for use with selector trees */
#define X_SEL 0x0300

#define THESCREEN 0
#define THEBAR 1
#define THEACTIVE 2

#define ARROW 0
#define HGLASS 2
#define FINGER 3
#define HAND 4
#define MAXSEL 100			/* Max objects selected at once		*/
#define NDXSIZE 1000		/* indices for tree/object names	*/
#define VIEWSIZE 60			/* maximum icons on workbench view	*/
#define TRACESIZE 1000		/* entries in the tree trace array	*/
#define MAPSIZE 3000		/* entries in the string/image map	*/
#define MAX_ICON_W 0x0040	/* largest icon to be displayed		*/
#define MAX_ICON_H 0x0040	/* is 64 X 64 pixels			*/
#define ICON_W 0x0030		/* icon width in pixels			*/
#define ICON_H 0x0018		/* icon height in pixels		*/
#define MIN_WINT 8			/* horiz. pixels between workbench icons */
#define MIN_HINT 12			/* vert. pixels between workbench icons  */
#define PBX_WINT 16			/* horiz. pixels between partbox objects */
#define PBX_HINT 6			/* vert. pixels between partbox objects */
#define INTER_HSPACE 0		/* alert spacing height in chars	*/
#define INTER_WSPACE 2		/* alert spacing width in chars		*/

#define	NOFILE_STATE 	0	/* defines the current editing procedure */
#define FILE_STATE 	1
#define	PANL_STATE	2
#define	DIAL_STATE	3
#define ALRT_STATE	4
#define MENU_STATE	5
#define FREE_STATE	6

#define NUM_TYP 6		/* the number of tree types */
#define UNKN	0		/* their names and numbers  */
#define PANL	1
#define MENU	2
#define DIAL	3
#define ALRT	4
#define FREE	5
#define OBJKIND 0x100
#define FRSTR	0x101
#define FRIMG	0x102

#define SORTX	0		/* sub-tree sort modes */
#define	SORTY	1
#define SORTXY	2
#define SORTYX	3

#define IB_PMASK(x)		(x)
#define IB_PDATA(x)		(x + 4)		/* definitions for direct access */
#define	IB_PTEXT(x)		(x + 8)		/* to resource structures	 */
#define IB_CHAR(x)		(x + 12)
#define IB_XCHAR(x)		(x + 14)
#define IB_YCHAR(x)		(x + 16)
#define IB_XICON(x)		(x + 18)
#define IB_WICON(x)		(x + 22)
#define IB_HICON(x)		(x + 24)
#define IB_XTEXT(x)		(x + 26)
#define IB_YTEXT(x)		(x + 28)
#define IB_WTEXT(x)		(x + 30)
#define TE_PTEXT(x)		(x)
#define	TE_PTMPLT(x)	(x + 4)
#define	TE_PVALID(x)	(x + 8)
#define TE_FONT(x) 		(x + 12)
#define TE_JUST(x)		(x + 16)
#define TE_FG(x)		(x + 18)
#define TE_THICK(x)		(x + 22)
#define TE_TXTLEN(x)	(x + 24)
#define TE_TMPLEN(x)	(x + 26)
#define BI_PDATA(x)		(x)
#define BI_WB(x)		(x + 4)
#define BI_HL(x)		(x + 6)
#define BI_COLOR(x)		(x + 12)

/* 	adapted from RSRCLIB.H		*/

#define SMODE	0			/* seek mode is absolute offset	*/
#define F_ATTR	0			/* file attribute for creating	*/

#define RT_VRSN 		0	/* these must coincide w/ rshdr */
#define RT_OB 			1
#define RT_TEDINFO		2
#define RT_ICONBLK		3
#define RT_BITBLK		4
#define RT_FREESTR		5
#define RT_STRING		6
#define RT_IMAGEDATA	7
#define	RT_FREEIMG		8
#define RT_TRINDEX		9

#define NUM_RTYPES 10

#define R_NOBS		10
#define R_NTREE		11
#define R_NTED		12
#define R_NICON		13
#define R_NBITBLK	14
#define R_NSTRING	15
#define R_IMAGES	16

#define NUM_OBS		(*(short *)(rs_hdr + 2*R_NOBS))
#define NUM_TREE	(*(short *)(rs_hdr + 2*R_NTREE))
#define NUM_TI		(*(short *)(rs_hdr + 2*R_NTED))
#define NUM_IB		(*(short *)(rs_hdr + 2*R_NICON))
#define NUM_BB		(*(short *)(rs_hdr + 2*R_NBITBLK))
#define NUM_FRSTR	(*(short *)(rs_hdr + 2*R_NSTRING))
#define NUM_FRIMG	(*(short *)(rs_hdr + 2*R_IMAGES))

#define NUM_RN 7
#define RS_SIZE 17				/* NUM_RTYPES + NUM_RN	*/
#define HDR_LENGTH (RS_SIZE + 1) * 2		/* in bytes	*/

/*	end of RSRCLIB.H stuff 		*/

#define	NOPTS	8

#define	RSH_VRSN(x)		(x)
#define	RSH_OBJECT(x)	(x + 2)
#define	RSH_TEDINFO(x)	(x + 4)
#define	RSH_ICONBLK(x)	(x + 6)
#define	RSH_BITBLK(x)	(x + 8)
#define	RSH_FRSTR(x)	(x + 10)
#define	RSH_STRING(x)	(x + 12)
#define	RSH_IMDATA(x)	(x + 14)
#define	RSH_FRIMG(x)	(x + 16)
#define	RSH_TRINDEX(x)	(x + 18)
#define	RSH_NOBS(x)		(x + 20)
#define	RSH_NTREE(x)	(x + 22)
#define	RSH_NTED(x)		(x + 24)
#define	RSH_NIB(x)		(x + 26)
#define	RSH_NBB(x)		(x + 28)
#define	RSH_NSTRING(x)	(x + 30)
#define	RSH_NIMAGES(x)	(x + 32)
#define	RSH_RSSIZE(x)	(x + 34)

#define OB_NEXT(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_next))
#define OB_HEAD(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_head))
#define OB_TAIL(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_tail))
#define OB_TYPE(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_type))
#define OB_FLAGS(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_flags))
#define OB_STATE(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_state))
#define OB_SPEC(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_spec))
#define OB_X(x)			(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_x))
#define OB_Y(x)			(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_y))
#define OB_WIDTH(x)		(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_width))
#define OB_HEIGHT(x)	(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_height))


extern   short 	gl_mode;
extern   short 	gl_tcolor;
extern   short 	gl_lcolor;

extern	short	gl_nrows;
extern	short	gl_ncols;
extern	short	gl_wchar;
extern	short	gl_hchar;
extern	short	gl_wschar;
extern	short	gl_hschar;
extern	short	gl_wbox;
extern	short	gl_hbox;
extern	short	gl_handle;
extern	short	gl_nplanes;
extern	short	gl_apid;
extern  short	gl_width;
extern	short	gl_height;
extern	char	sav_rcspath[];
extern	char	sav_icnpath[];
extern	char	rcs_rfile[];
extern	char	rcs_dfile[];
extern	char	rcs_infile[];
extern  char    icn_file[];
extern	char	rcs_app[];
extern	unsigned short	hard_drive;
extern	FILE	*rcs_rhndl;
extern	FILE	*rcs_dhndl;
extern  short	rcs_edited;
extern	short	partp;
extern	short	toolp;
extern  long	ad_menu;
extern	long	ad_tools;
extern  long	ad_view;
extern	long	ad_pbx;
extern	long	ad_clip;
extern  long    ad_itool;
extern  long    ad_idisp;
extern	short	rcs_clipkind;
extern	short	rcs_view;
extern	GRECT	full;
extern	GRECT	view;
extern	GRECT	pbx;
extern	GRECT	tools;
extern  GRECT   itool;
extern  GRECT   idisp;
extern	short	rcs_trpan;	/* Tree row offset in window	*/
extern	short	rcs_xpan;	/* (Positive) offset of root	*/
extern	short	rcs_ypan;
extern	short	rcs_xtsave;	/* Tree x,y before being opened */
extern	short	rcs_ytsave;
extern	short	rcs_mform;	/* Current mouse form */
extern	short	rcs_hot;	/* Currently active tools */
extern	GRECT	wait;		/* Current mouse wait rectangle */
extern	short	wait_io;	/* Waiting for in or out? */

extern	short	rcs_svfstat;
extern  short	rcs_state;
extern  short	icn_state;
extern	char	rcs_title[];
extern	short	rcs_nsel;
extern	short	rcs_sel[];
extern  short    rcs_fsrtflag;
extern	short	rcs_cflag;
extern	short	rcs_hflag;
extern	short	rcs_oflag;
extern	short	rcs_cbflag;
extern	short	rcs_f77flag;
extern	short	rcs_lock;
extern	short	rcs_xpert;
extern	short	rcs_low;		/* in memory trouble ? */
extern	short	rcs_panic;		/* in deep memory trouble! */
extern	short	rcs_menusel;		/* only used in MENU_STATE */
extern  short	rcs_rmsg[];
extern  long	ad_rmsg;

extern	OBJECT	rcs_work[];		/* Space for workbench objects	*/
extern	ICONBLK	rcs_icons[];	/* Space for workbench iconblks */
extern	short	rcs_typ2icn[];
/* Variables used in write_file */
extern	unsigned short	rcs_wraddr;		/* Current offset in output file */
extern	unsigned short	wr_obnum;		/* Count of obj in tree		*/
extern	short	wr_obndx[];		/* Tree trace index is built here */

extern  unsigned short	rcs_ndxno;

#define INDEX struct index

extern	INDEX	{
	char	*val;
	short	kind;
	char	name[10];
} rcs_index[];

#define DEFLTS struct deflts

extern	DEFLTS  {
	short	option;
	short	 state;
} deflt_options[];
extern  char	rsc_path[];

extern  short    OK_ICN[];
extern  short    OK_NOICN[];
extern  short    OK_EDITED[];
extern	short	OK_NOFILE[]; 
extern	short	OK_FILE[];
extern	short	OK_TREES[];
extern	short	ILL_LOCK[];
extern	short	HOT_IBOX[]; 
extern	short	HOT_BOX[];
extern	short	HOT_TEXT[];
extern	short	HOT_IMAGE[];
extern	short	HOT_ICON[];
extern	short	HOT_BTEXT[];

extern	FILE	*rcs_hhndl;	/* used in write_files and c_ routines */
extern	char	rcs_hfile[], hline[];

extern	struct tally {
	short	nobj;
	short	nib;
	short	nbb;
	short	nti;
	short	nimg;
	short	nstr;
	long	nbytes;
} rcs_tally;

extern	short	c_obndx[];	/* maps from disk address to string/image # */
extern	short	c_nstring;
extern	short	c_frstr;	/* first string referenced by freestr */
extern	short	c_nfrstr;
extern	short	c_nimage;
extern	short	c_frimg;	/* first image referenced by freebits */
extern	short	c_nbb;
extern	short	c_nfrbit;
extern	short	c_nib;
extern	short	c_nted;
extern	short	c_nobs;
extern	short	c_ntree;

extern	struct popmap {
	short	hotobj;
	short	popmenu;
	short	outline;
} rcs_ht2pop[], icn_ht2pop[];

extern	struct map {
	short	mfrom;
	short	mto;
} rcs_it2thk[], grid_patrn[];

extern	struct obitmap {
	short	flagmask;
	short	statemask;
	short	objno;
} rcs_bit2obj[]; 
		     
#define	N_TYPES	13
#define	N_FLAGS	10
#define	N_STATES 7
#define LOOKUP struct looktbl
extern	LOOKUP {
	unsigned short	l_val;
	char	l_name[11];
} c_types[];
extern 	LOOKUP	c_flags[];
extern	LOOKUP	c_states[];

#define N_COLORS 16L
#define N_PTNS 8L
#define N_BTHKS 7L
#define N_CHCOLS 16L

extern	short	str_types[];
extern	short	box_types[];
extern	short	txt_types[];

extern	OBJECT	blank_obj;

extern	char	*hptns[];
extern	char	*optns[];
extern	char	*bptns[];
extern	char	*fptns[];

extern	long	rs_hdr;
extern	long	head;
extern	long	buff_size;
extern	long	rcs_free;

#endif
