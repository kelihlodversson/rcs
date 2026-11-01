/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <stdbool.h>
#include "rcs.h"
#include "rcsdefs.h"
#include "rcsiext.h"
#include "rcsimain.h"
#include "rcslib.h"
#include "rcsintf.h"
#include "rcsvdi.h"
#include "rcsiedit.h"

extern	GRECT	dispc_area;
extern  short	ptsin[];
extern  unsigned short	color_map[];

	short	scale;	/*indicating if size is changed, image will be scaled,
						keeped/croped, or cleared*/
short    stemp[256], dtemp[256];
GRECT   orign_area;

void del_img(short mouse, short which)
{
	unsigned short	t2pxy[4];

	t2pxy[0] = selec_area.g_x;
	t2pxy[1] = selec_area.g_y;
	t2pxy[2] = selec_area.g_x + selec_area.g_w - 1;
	t2pxy[3] = selec_area.g_y + selec_area.g_h - 1;
/*	gsx_attr(FALSE, 1, WHITE);	*/
	vsf_color(gl_handle, WHITE);
	if (which != 2)
	{
		hold_to_undo();
		if (mouse)
			graf_mouse(M_OFF, 0x0L);
		if (!gl_isicon)
			rubrec_off();
		gsx_sclip(&dat_img);
		v_bar(gl_handle, (short *)t2pxy);
		if (which == 1)
			return;
	}
	if (gl_isicon)  	/*mask area*/
	{
		if(which != 1)
		{
			t2pxy[0] += deltax;
			t2pxy[1] += deltay;
			t2pxy[2] += deltax;
			t2pxy[3] += deltay;
			rubrec_off();
			gsx_sclip(&mas_img);
			v_bar(gl_handle,(short *)t2pxy);
		}
		if (which != 3)
			return;
    }
	save_img();
	graf_mouse(M_ON, 0x0L);
}

void clear_clipb(void)
{
    GRECT  temp_area;
    if (orign_area.g_w && orign_area.g_h)	/*clear the old stuff in clip board*/
    {
		orign_area.g_x -= 1;	/*include the outline*/
		orign_area.g_y -= 1;
		orign_area.g_w += 2;
		orign_area.g_h += 2;
		gsx_sclip(&orign_area);
		rast_op(0, &orign_area, &scrn_mfdb, &orign_area, &scrn_mfdb);
		if (orign_area.g_w > dispc_area.g_w)
		{
			objc_xywh(ad_itool, CLIPBOX, &temp_area);
			gsx_sclip(&temp_area);
			objc_draw((OBJECT  *)ad_itool, CLIPBOX, 1, temp_area.g_x, temp_area.g_y, 
			temp_area.g_w, temp_area.g_h);
		}
		orign_area.g_w = orign_area.g_h = 0;
	}
}

/* this routine draw an rectangle outline specified by area	*/
void draw_outline(GRECT *area)
{
	short   mode, colr;

	graf_mouse(M_OFF, 0x0L);
	mode = gl_mode;
	colr = gl_lcolor;
	gsx_attr(false, 1, BLACK);
	gsx_bxpts(area);  /*draw an outline of the clipping content*/
	v_pline(gl_handle, 5, &ptsin[0]);
	gsx_attr(false, mode, colr);	/*restore the mode and color*/
	graf_mouse(M_ON, 0x0L);
}    

void draw_clipb(short mouse, GRECT *dirty)
{
	GRECT  temp_area;

	if (clipped && orign_area.g_w && orign_area.g_h)
	{
		rc_copy(dirty, &temp_area);
		if (rc_intersect(&orign_area, &temp_area))
		{
			temp_area.g_x -= 1;	/*need to draw the outline*/
			temp_area.g_y -= 1;
			temp_area.g_w += 2;
			temp_area.g_h += 2;
			if (!rc_equal(dirty, &orign_area))
				rc_intersect(dirty, &temp_area);
			gsx_sclip(&temp_area);
			if (mouse)
				graf_mouse(M_OFF, 0x0L);
			rc_copy(&clip_area, &temp_area);
			if (gl_isicon)
			{
				temp_area.g_x += icn_img.g_x ;
				temp_area.g_y += icn_img.g_y ;
				rast_op(3, &temp_area, &scrn_mfdb, &orign_area, &scrn_mfdb);
			}
			else
			{
				/* the image in the clip board, in the colour of the image */
				rast_op(3, &clip_area, &clip_mfdb, &orign_area, &scrn_mfdb);
			}
			rc_copy(&orign_area, &temp_area);
			temp_area.g_x -= 1;
			temp_area.g_y -= 1;
			temp_area.g_w += 2;
			temp_area.g_h += 2;
			draw_outline(&temp_area);
			if(mouse)
				graf_mouse(M_ON, 0x0L);
		}
	}
}

void copy_img(short mouse)	/*copy part of icon/image to the clipboard*/
{
	GRECT    temp_area;

	graf_mouse(M_OFF, 0x0L);
	rc_copy(&selec_area, &clip_area);
	rc_copy(&selec_area, &temp_area);
	gsx_sclip(&dat_img);
	clip_area.g_x -= dat_img.g_x;	/*relative to the data frame*/
	clip_area.g_y -= dat_img.g_y;	/*relative to the data frame*/
	rast_op(3, &temp_area, &scrn_mfdb, &clip_area, &clip_mfdb);
	if (gl_isicon)
	{
		gsx_sclip(&mas_img);
		temp_area.g_x += deltax;
		temp_area.g_y += deltay;
		rast_op(3, &temp_area, &scrn_mfdb, &clip_area, &clp2_mfdb);
	}
	clear_clipb();
	/*show the stuff in the clip board*/
	rc_copy(&dispc_area, &orign_area);	
	orign_area.g_x -= (clip_area.g_w - orign_area.g_w) / 2; /*don't change 
			/ 2 to >> 1, because it maybe a negative value*/
	orign_area.g_w = clip_area.g_w;
	orign_area.g_h = clip_area.g_h;
	clipped = true;	/*something in the clipboard*/
	draw_clipb(false, &orign_area);
	if (mouse)
		graf_mouse(M_ON, 0x0L);
	gsx_sclip(&full);
}

void cut_img(void)
{
	copy_img(false);
	del_img(false, 3);
}

void do_paste_img(void)
{
	unsigned short 	t1[2], smlxy[2];
	GRECT	dest_area;

	hold_to_undo();
	t1[0] = mousex;
	t1[1] = mousey;
	dest_area.g_w = clip_area.g_w;
	dest_area.g_h = clip_area.g_h;
	fat2sml((short *)t1, (short *)smlxy, 1, 1);
	dest_area.g_x = smlxy[0];
	dest_area.g_y = smlxy[1];
	rast_op(3, &clip_area, &clip_mfdb, &dest_area, &scrn_mfdb);
	if (gl_isicon)
	{
		gsx_sclip(&mas_img);
		dest_area.g_x += deltax;
		dest_area.g_y += deltay;
		rast_op(3, &clip_area, &clp2_mfdb, &dest_area, &scrn_mfdb);
	}
	save_img();
}

void solid_img(void)
{	      
	short	color;

	hold_to_undo();
	color = (colour == WHITE ? 0 : 15);
	if (pen_on != BCLORBOX)	/*eraser or 0 or fgcolr*/
	{
		rast_op(color,&hold_area, &hold_mfdb, &hold_area, &hold_mfdb);  
		if (color && gl_isicon)
			rast_op(0,&hold_area, &hld2_mfdb, &hold_area, &hld2_mfdb);
	}
	if (pen_on == BCLORBOX || (pen_on == ERASEBOX && gl_isicon))
	{
		rast_op(color,&hold_area, &hld2_mfdb, &hold_area, &hld2_mfdb);
		if (color)
			rast_op(0,&hold_area, &hold_mfdb, &hold_area, &hold_mfdb);  
	}
	save_fat(false);
	gsx_sclip(&fat_area);
	fattify(NULL);	/*fattify the whole scroll area*/
	inverted = false;
}

void invrt_img(void)
{
	short	color1, color2;

	hold_to_undo();
	if (gl_nplanes == 1)
	{
		color2 = fgcolor ^ 0x01;
		color1 = bgcolor ^ 0x01;
	}
	else if(gl_nplanes == 3)
	{
		color2 = invert3[fgcolor];
		if (gl_isicon)
			color1 = invert3[bgcolor];
	}
	else
	{
		color2 = invert4[fgcolor];
		if (gl_isicon)
			color1 = invert4[bgcolor];
	}
	set_fgcol(color2, true);
	if (gl_isicon)
		set_bgcol(color1, true);
	if (pen_on == BCLORBOX)
		colour = color1;
	else if(pen_on == FCLORBOX || pen_on == false)
		colour = color2;
	inverted = true;
	draw_clipb(true, &orign_area);
}

void icon_size(short *pwidth, short *pheight)
{
	long		tree;
	unsigned short		state;
	short		ii, butn, exitobj,new_w, new_h;
	GRECT		p;

	objc_xywh(ad_menu, EDITMENU, &p);
	ini_tree(&tree, NEWICNSZ);

	butn = (gl_wimage >> 4) + BEGWDSZ - 1;
	(*(short *)(OB_STATE(butn)) = (SELECTED));
	butn = (gl_himage >> 2) + BEGHTSZ  - 1;
	(*(short *)(OB_STATE(butn)) = (SELECTED));
	(*(short *)(OB_STATE(SCALESZ)) = (SELECTED));
	exitobj = hndl_dial(tree,0,&p);
	/* find the new buttons	*/
	for (ii = BEGWDSZ; ii <= LASTWDSZ; ii++)
	{
		state = (*(short *)(OB_STATE(ii)));
		if (state & SELECTED)
		{
			new_w = ii;
			break;
		}
	} /* for */
	/* get the height button	*/
	for (ii = BEGHTSZ; ii <= LASTHTSZ; ii++)
	{
		state = (*(short *)(OB_STATE(ii)));
		if (state & SELECTED)
		{
			new_h = ii;
			break;
		}
	} /* for */
	for (ii = SCALESZ; ii <= CLEARSZ; ii++) 
	{
		state = (*(short *)(OB_STATE(ii)));
		if (state & SELECTED)
		{
			scale = ii;
			break;
		}
	}
	/* reset the just-changed buttons	*/
	(*(short *)(OB_STATE(new_w)) = (NORMAL));
	(*(short *)(OB_STATE(new_h)) = (NORMAL));
	(*(short *)(OB_STATE(scale)) = (NORMAL));
	if ( exitobj == OKCHNGSZ )	
	{
		*pwidth = (new_w - BEGWDSZ + 1 ) << 4;
		*pheight = (new_h - BEGHTSZ  + 1) << 2;
		(*(short *)(OB_STATE(OKCHNGSZ)) = (NORMAL));
	}
	else
	{ 	/* it was CANCEL	*/
		/* reset the old buttons	*/
		(*(short *)(OB_STATE(CNCLCHGS)) = (NORMAL));
		/* return old values		*/
		*pwidth = gl_wimage;
		*pheight = gl_himage;
	} /* else */
} /* icon_size */

void horiz_scale(char *srcpt, short srcw, char *despt)
{
	short s_bit_index, d_bit_index, xds, xdd, i, j, e;
	unsigned char bmask;

	xds = srcw << 1;
	xdd = gl_wimage << 1;

	bmask = 0x80;    
	e = j = d_bit_index = 0;
	for(i=0; i < srcw; i++)
	{
		e += xdd;
		s_bit_index = i % 8;
		if (i && !s_bit_index)
			srcpt++;
		if (e <= 0) /*this bit should be skipped, but save it by
			doing the logical or to the privious bit in destination */ 
		{
			if (*srcpt & (bmask >> s_bit_index))
				*despt |= (bmask >> d_bit_index);
		}
		else 
		{
			while( e > 0)
			{
				d_bit_index = j % 8;
				if (j++ && !d_bit_index)
					despt++;
				if (*srcpt & (bmask >> s_bit_index))
					*despt |= (bmask >> d_bit_index);
				e -= xds;
			}	/*end of while(e>0)*/
		}	/*end else	   */
	}	/*end of for i-loop*/
}

/* This routine will expand or shrink a given image. */
void scale_img(long srcpt, short srcw, short srch, long despt)
{
	short yds, ydd, f, l, k, j, last_k, db_per_row, sb_per_row;
	char *p, *q, *last_q, *t, *s, temp[8];

	yds = srch << 1;
	ydd = gl_himage << 1;
	db_per_row = gl_wimage >> 3;	/*bytes per row in destination*/
	sb_per_row = srcw >> 3;	/*bytes per row in source*/
	f = l = 0;
	memmove((void *)(((long)(stemp))), (void *)(srcpt), sb_per_row * srch);
	last_k = -1;
	last_q = q = (char *) dtemp;

	for(k=0; k < srch; k++)
	{
		f += ydd;
		p = ((char*) stemp) + sb_per_row * k;
		if ( f <= 0) /*instead of skipping this row, scale it, bitwise or to
			the previous destination row*/
		{
			for (j=0; j<8; temp[j++]=0)
				;
			horiz_scale(p, srcw, temp);
			s = temp;
			t = q;
			for (j=0; j < db_per_row; j++)
				*t++ |= *s++;
		}
		else 
		{
			while(f > 0)
			{
				q = ((char*) dtemp) + db_per_row * l++;
				if (k == last_k)/*no nedd to go through the scaling,just copy*/
					memmove((void *)(((long)(q))), (void *)(((long)(last_q))), db_per_row);
				else
				{
					last_q = q;
					last_k = k;
					horiz_scale(p, srcw, q);
				}		/*end of else clause*/
				f -= yds;
			}		/*end of while(f>0) */
		}	/*end of else clause*/
	}		/*end of for k-loop*/

	memmove((void *)(despt), (void *)(((long)(dtemp))), db_per_row * gl_himage);
}	/*end of scale_img*/

/************************************************************************/
/*keep_img()  This routine will keep or crop the given image in a new	*/
/*size.  The initialization of the destination image with zeros is done	*/
/*in the routine resize().                                              */
/************************************************************************/
void keep_img(long srcpt, short srcw, short srch, long despt)
{
	short  j, sw_per_row, dw_per_row;
	short  *p, *q;

	sw_per_row = srcw >> 4;	/*words per row in source image*/
	dw_per_row = gl_wimage >> 4;	/*words per row in destination image*/

	memmove((void *)(((long)(stemp))), (void *)(srcpt), 2 * (sw_per_row * srch));
	for (j=0; j<min(srch, gl_himage); j++)
	{
		p = (short *) stemp + sw_per_row * j;
		q = (short *) dtemp + dw_per_row * j;
		memmove((void *)(((long)(q))), (void *)(((long)(p))), 2 * (min(dw_per_row, sw_per_row)));
	}
	memmove((void *)(despt), (void *)(((long)(dtemp))), 2 * (dw_per_row * gl_himage));
}

/************************************************************************/
/* resize(): src_pt points to src image, src_w is its width in pixels,	*/
/* src_h is its height in pixels, des_pt points to destination image.	*/
/* This routine will call scale_img, keep_img or clear_img to scale an	*/
/* source image to a destination, depends on "scale" obtained from      */
/* icon_size dialog.  "scale" can be scale, keep or clear.  The new size*/
/* information is already set in gl_wimage and gl_himage in icon_size().*/
/************************************************************************/
/* the bytes of each word in the other order, if the host is little-endian: the
   same to go from the words of the host to big-endian bytes, and back */
static short is_be_host(void)
{
	unsigned short one = 1;

	return *(unsigned char *)&one == 0;
}

static void swap_words(long addr, long words)
{
	unsigned char	*p = (unsigned char *)addr, t;

	for (; words > 0; words--, p += 2)
		if (!is_be_host())
		{
			t = p[0];
			p[0] = p[1];
			p[1] = t;
		}
}

void resize(long src_pt, short src_w, short src_h, long des_pt)
{
	short k;    

	for(k=0; k<256; k++)
		dtemp[k] = 0;
	/* the scaling takes the pixels as the bits of bytes, left to right, which
	   is how the words are in memory on a big-endian host only */
	swap_words(src_pt, (long)(src_w >> 4) * src_h);
	if (scale == KEEPSZ)
		keep_img(src_pt, src_w, src_h, des_pt);
	else if(scale == SCALESZ)
		scale_img(src_pt, src_w, src_h, des_pt);
	else
		memmove((void *)(des_pt), (void *)(((long)(dtemp))), 2 * (gl_datasize));
	swap_words(src_pt, (long)(src_w >> 4) * src_h);
	swap_words(des_pt, gl_datasize);
}
