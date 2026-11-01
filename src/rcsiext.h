/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#ifndef RCSIEXT_H

#define RCSIEXT_H

#include <stddef.h>

extern	     short       flags;
extern	     short       out1;
extern	     short       xwait;
extern	     short       ywait;
extern	     short       wwait;
extern	     short       hwait;
extern	     short       out2;
extern	     long       ad_rmsg;
extern       short       rcs_rmsg[];
extern	     GRECT      wait;
extern	     short       mousex;
extern	     short       mousey;
extern	     short       bstate;
extern	     short       kstate;
extern	     short       kreturn;
extern	     short       bclicks;
extern	     short       rcs_edited;
extern       GRECT      tools;
extern       char 	icn_file[];
extern       short	rcs_xpert;
extern	     unsigned short      bit_mask[];
extern	     short       gl_tcolor;
extern	     short       gl_mode;
extern	     short       gl_lcolor;
extern	     GRECT      full;
extern	     short       rcs_xpan;
extern	     short       rcs_ypan;
extern	     long       src_mp;
extern	     long       dst_mp;
extern	     GRECT      pbx;

#define      FINGER      3
#define      ARROW       0
extern	     short       rcs_hot;
 
extern       long	ad_menu;
extern       short	icn_state;
#define      NOFILE_STATE	0
#define      FILE_STATE		1
extern       short	OK_NOICN[];
extern       short	OK_ICN[];
extern       short	icn_edited;
extern       short	OK_EDITED[];
extern       short	gl_isicon;
extern       long	ad_tools;
extern       long       ad_itool;
extern       long       ad_idisp;
extern       GRECT	fat_area;
extern       GRECT	 gridbx;
extern       long	ad_view;
extern       short	gl_wimage;
#define  gl_sqsize  8
extern       short	gl_himage;
extern       GRECT	view;
extern       GRECT      itool;
extern       GRECT      idisp;
extern       long	ad_pbx;
extern       GRECT	src_img;
extern       GRECT	dat_img;
extern       GRECT	mas_img;
extern       GRECT	icn_img;
extern       short	iconedit_flag;
extern       MFDB	hold_mfdb;
extern       MFDB	hld2_mfdb;
extern       MFDB	disp_mfdb;
extern       MFDB	undo_mfdb;
extern       MFDB	und2_mfdb;
extern       MFDB	clip_mfdb;
extern       MFDB	clp2_mfdb;
extern       MFDB	save_mfdb;
extern       MFDB	sav2_mfdb;
extern       MFDB	fat_mfdb;
extern       long	save_tree;
extern	     short	save_obj;
extern       short	rcs_sel[];
extern       short	rcs_view;
extern       char	rcs_title[];
extern       short	rcs_state;
extern       short	colour;
extern       short	fgcolor;
extern       short	bgcolor;
extern	     short	old_fc;
extern       GRECT	hold_area;  
extern       GRECT	selec_area;
extern	     GRECT	flash_area;
extern       GRECT	clip_area;  
extern       short	partp;
extern	     short	deltax;
extern	     short	deltay;
extern       long	gl_icnspec;  
extern       short	gl_datasize;  
extern       short	gl_nplanes;  
extern       short	gl_width;  
extern       GRECT	scrn_area;  
extern       short	gl_height;  
extern       MFDB	scrn_mfdb;  
extern       GRECT	scroll_fat;  
extern       short	is_mask;  
extern       short	rcs_nsel;  
#define     MAX_ICON_W	0x0040
#define     MAX_ICON_H  0x0040
extern       long	ibuff_size;  
extern       short	pen_on;  
extern	     short	grid;
extern	     short	clipped;
extern	     short	paste_img;
extern	     short	selecton;
extern	     short    inverted;
extern	     short	invert3[8];
extern	     short	invert4[16];

#define      BI_PDATA(x)  (x)
#define      IB_PDATA(x)  (x + 4)
#define      BI_WB(x)     (x + 4)
#define      BI_HL(x)     (x + 6)
#define      BI_COLOR(x)  (x + 12)
#define      IB_CHAR(x)   (x + 12)
#define      IB_PMASK(x)  (x)
#define	     IB_XICON(x)  (x + 18)
#define      IB_WICON(x)  (x + 22)   
#define      IB_HICON(x)  (x + 24)
#define      OB_STATE(x) (tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_state))

#endif
