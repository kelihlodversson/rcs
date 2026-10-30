/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include "rcs.h"
#include "rcsdefs.h"
#include "rcsiext.h"
#include "rcsintf.h"
#include "rcsmain.h"
#include "rcsvdi.h"
#include "rcsiext.h"
#include "rcsiinit.h"
#include "rcsiedit.h"
#include "rcslib.h"
#include "rcsasm.h"
#include "rcsimain.h"

#define MAX_POLY_POINTS 128
#define ABS(x) (x) > 0 ? (x) : -(x)
#define YSCALE(x)  umul_div(x, gl_ws.ws_wpixel, gl_ws.ws_hpixel)
#define XSCALE(x)  umul_div(x, gl_ws.ws_hpixel, gl_ws.ws_wpixel)

extern  WS      gl_ws;

short    gridx, gridy;
unsigned short   mflags, ev_which, smlxy[513], SCRATCH[513];
short flash_on;
extern	unsigned short	color_map[], gridw, gridh, orign_area;
short    selmask = 0xcccc;

void fat2sml(short x[], short y[], short count, short isdata)
{
	short i, srcx,srcy;

	if (isdata)
	{
		srcx = dat_img.g_x;
		srcy = dat_img.g_y;
		gsx_sclip(&dat_img);
	}
	else
	{
		gsx_sclip(&mas_img);
		srcx = mas_img.g_x;
		srcy = mas_img.g_y;
	}
	for (i=0; i<(count << 1); i += 2)
	{
		y[i] = srcx + ((x[i] - fat_area.g_x) >> 3) + scroll_fat.g_x;
		y[i+1] = srcy + ((x[i+1] - fat_area.g_y) >> 3) + scroll_fat.g_y;
	}
}

void set_pix(void)
{
	short	pix_x, pix_y, i;
	short	sm_pxy[4];
	GRECT	big_pxy, *src;

	out1 = true;
	/* get 0-rel. x,y */
	pix_x = (mousex - fat_area.g_x) >> 3;
	pix_y = (mousey - fat_area.g_y) >> 3;
	if (pen_on == ERASEBOX)
	{
		i=2;
		gsx_attr(false, 1, WHITE);
	}
	else
	{
		i=0;
		gsx_attr(false, 1, gl_isicon ? BLACK: fgcolor);
	}
there:
	if (is_mask)	/* set the new small pixel */
		src = &mas_img;
	else
		src = &dat_img;
	gsx_sclip(src);

	sm_pxy[0] = src->g_x + pix_x + scroll_fat.g_x;
	sm_pxy[1] = src->g_y + pix_y + scroll_fat.g_y;
	sm_pxy[2] = sm_pxy[0];
	sm_pxy[3] = sm_pxy[1];

	v_pline(gl_handle, 2, sm_pxy);
	rast_op(3,src,&scrn_mfdb,&hold_area,(is_mask? &hld2_mfdb:&hold_mfdb));

	if (i)
	{
		is_mask = !is_mask;
		if(--i)
			goto there;
	}

	if (gl_isicon)
	{
		gsx_sclip(&icn_img);
		gsx_attr( false, 1, colour);
		sm_pxy[2] =  sm_pxy[0] = icn_img.g_x + pix_x + scroll_fat.g_x;
		sm_pxy[3] =  sm_pxy[1] = icn_img.g_y + pix_y + scroll_fat.g_y;
		v_pline(gl_handle, 2, sm_pxy);
	}
	big_pxy.g_h = big_pxy.g_w = 8 - (grid ? 1 : 0);
	big_pxy.g_x = (pix_x << 3) + fat_area.g_x + (grid ? 1 : 0);
	big_pxy.g_y = (pix_y << 3) + fat_area.g_y + (grid ? 1 : 0);
	gsx_sclip(&fat_area);
	gr_rect(colour, IP_SOLID, &big_pxy );
	/* set the rect. to wait for	*/
	xwait = big_pxy.g_x;
	ywait = big_pxy.g_y;
	wwait = gl_sqsize;
	hwait = gl_sqsize;
	gsx_sclip(&fat_area);	  
	gsx_attr( gl_tcolor, gl_mode, gl_lcolor);
	gsx_sclip(&full);
} /* set_pix */

void set_scroll(void)
{
	scroll_fat.g_x =  rcs_xpan/8;
	scroll_fat.g_y  = rcs_ypan/8;
	scroll_fat.g_w = gl_wimage - scroll_fat.g_x;
	scroll_fat.g_h = gl_himage - scroll_fat.g_y;
} /* set_scroll */

void copy_colplanes(long longaddr, short color, short isdata)
{
	unsigned short	plane;
	long	buffer;
	short	i;

	hold_mfdb.fd_stand = 0;
	vr_trnfm(gl_handle, &hold_mfdb, &hold_mfdb);
	plane = color_map[color];
	buffer = (long)(isdata? hold_mfdb.fd_addr : hld2_mfdb.fd_addr);
	rast_op(0, &hold_area, &scrn_mfdb, &hold_area,
		(isdata? &hold_mfdb : &hld2_mfdb));
	for (i=0; i<gl_nplanes; i++)	    
	{
		if (plane & 1)	
			memcpy((void *)(buffer), (void *)((*(long *)(BI_PDATA(longaddr)))), 2 * (gl_datasize));
		buffer += gl_datasize * 2;
		plane = plane >> 1;
	}
	hold_mfdb.fd_stand = 1;
	vr_trnfm(gl_handle, &hold_mfdb, &hold_mfdb);
}

void dr_image(void)
{
	objc_draw((OBJECT  *)ad_pbx, ROOT, MAX_DEPTH, pbx.g_x, pbx.g_y,pbx.g_w,pbx.g_h);
	outl_img();
} /* dr_image */

/* copy hold/hld2 buffer to undo/und2 buffer */
void hold_to_undo(void)
{
	if(gl_isicon)
		rast_op(3, &hold_area, &hld2_mfdb, &hold_area, &und2_mfdb);
	rast_op(3, &hold_area, &hold_mfdb, &hold_area, &undo_mfdb);
}

/* This routine handle either save operation or undo operation depending */
/* the parameter "undo".  The save operation will save the content in */
/* hold/hld2 buffer into data/mask.  Copying hold/hld2 buffer to undo/und2 */
/* buffer was done before any modification is taken place.  Undo operation */
/* will put undo/und2 buffer into data/mask, then copy hold/hld2 buffer */
/* to undo/und2 buffer, finally, call copy_colplanes to copy data/mask */
/* to hold/hld2 buffer.  So that undo an undo can work correctly. */
void save_fat(short undo)
{							   
	long	taddr, buffer;			  
	short    i;
	unsigned short   plane;

	i =  2 ;
	/* copy hold_mfdb.fd_addr data to data/mask and icon. */
	while( i--)
	{
		if (is_mask)
		{
			buffer = (long)(undo? und2_mfdb.fd_addr : hld2_mfdb.fd_addr);
			taddr = GET_SPEC(ad_pbx, MASKWIND);	
		}
		else
		{
			buffer = (long)(undo? undo_mfdb.fd_addr : hold_mfdb.fd_addr);
			taddr = GET_SPEC(ad_pbx, DATAWIND);
		}
		hold_mfdb.fd_stand = 0;
		vr_trnfm(gl_handle, &hold_mfdb, &hold_mfdb);
		if (!gl_isicon && gl_nplanes > 1) 
		{
			plane = color_map[fgcolor];
			while ( !(plane & 1) )	
			{
				plane = plane >> 1;
				buffer += gl_datasize * 2;
			}
			memcpy((void *)((*(long *)(BI_PDATA(taddr)))), (void *)(buffer), 2 * (gl_datasize));
		}
		else
			memcpy((void *)((*(long *)(BI_PDATA(taddr)))), (void *)(buffer), 2 * (gl_datasize));
		hold_mfdb.fd_stand = 1;
		vr_trnfm(gl_handle, &hold_mfdb, &hold_mfdb);
		if (is_mask)
		{
			if (undo)
			{
				rast_op(3, &hold_area, &hld2_mfdb, &hold_area, &und2_mfdb);
				copy_colplanes(taddr, BLACK, false);
			}
			else	/*make sure the data and mask do not overlap*/
				rast_op(4, &hold_area, &hld2_mfdb, &hold_area, &hold_mfdb);
		}
		else
		{
			if (undo)
			{
				rast_op(3, &hold_area, &hold_mfdb, &hold_area, &undo_mfdb);
				copy_colplanes(taddr, BLACK, true);
			}
			else if (gl_isicon)	/*make sure that data and mask do not overlap*/
				rast_op(4, &hold_area, &hold_mfdb, &hold_area, &hld2_mfdb);
		}
		if (gl_isicon)
			is_mask = !is_mask;
		else	/*image doesn't need to do it twice*/
			i--;
	}
	dr_image();	/*update the icon image*/
} /* save_fat */

static void draw_gridbx(void)
{	      
	short	xypoint[10];

	vswr_mode(gl_handle, MD_XOR);	/*xor mode*/ 
	xypoint[0] = gridbx.g_x;
	xypoint[2] = gridbx.g_x + gridbx.g_w;
	for (xypoint[1] = gridbx.g_y; xypoint[1] <= gridbx.g_y + gridbx.g_h;
		xypoint[1] += gl_sqsize)
	{
		xypoint[3] = xypoint[1];
		v_pline(gl_handle, 2, xypoint);
	}
	xypoint[1] = gridbx.g_y;
	xypoint[3] = gridbx.g_y + gridbx.g_h;
	for ( ;xypoint[0] <= gridbx.g_x + gridbx.g_w; xypoint[0] += gl_sqsize)
	{
		xypoint[2] = xypoint[0];
		v_pline(gl_handle, 2, xypoint);
	}
	vswr_mode(gl_handle, gl_mode);
	gsx_sclip(&full);		
} /*draw_gridbx*/

static void MAGNIFY(long src_mp, long dst_mp, short npix)
/* expand bits to bytes from source to dest */
{
	short i;
	unsigned char *src, *dst, val;

	src = (unsigned char *)src_mp;
	dst = (unsigned char *)dst_mp;
	while (npix > 0)
	{
		val = *src;
		for (i=0; i < 8 && i < npix; i++)
		{
			if (val & 0x80)
				*dst = 0xff;
			else
				*dst = 0;
			val = val << 1;
			dst++;
		}
		npix -= 8;
		src++;
	}
}

/* this routine will magnify a portion of icon/data image to the work area */
/* It takes the parameter "area" for the spcification of the portion. */
/* If area is zero, it takes the scroll_fat area.  The area's x, y */
/* are relative to the icon/data image. */
void fattify(GRECT *area)
{
	GRECT	trans_area, *pt;
	short	i,j, k, rows_fit, no_loop, leftover, rows_in_loop, row_w;
	GRECT	src_area, dst_area;

	if (!area)
		pt = &scroll_fat;
	else
		pt = area;
	row_w = (pt->g_w + 0x0f) & (~0x0f);
	fat_mfdb.fd_w = row_w << 3;
	fat_mfdb.fd_wdwidth = fat_mfdb.fd_w >> 4; 

	src_area.g_x = src_area.g_y = 0;
	src_area.g_w = dst_area.g_w = pt->g_w << 3;
	dst_area.g_h = pt->g_h << 3; 	/*set for the clip area*/

	dst_area.g_x = fat_area.g_x;
	dst_area.g_y = fat_area.g_y;
	if (area)
	{
		dst_area.g_x += (area->g_x - scroll_fat.g_x) << 3;
		dst_area.g_y += (area->g_y - scroll_fat.g_y) << 3;
	}

	disp_mfdb.fd_w = row_w;
	disp_mfdb.fd_wdwidth = disp_mfdb.fd_w >> 4;
	disp_mfdb.fd_h = pt->g_h;
	trans_area.g_w = pt->g_w;
	trans_area.g_h = pt->g_h;
	trans_area.g_x = pt->g_x + (gl_isicon? icn_img.g_x : dat_img.g_x);
	trans_area.g_y = pt->g_y + (gl_isicon? icn_img.g_y : dat_img.g_y);

	rast_op(0, &hold_area, &disp_mfdb, &hold_area, &disp_mfdb);
	rast_op(3, &trans_area, &scrn_mfdb, &hold_area, &disp_mfdb);
	disp_mfdb.fd_stand = 0;
	vr_trnfm(gl_handle, &disp_mfdb, &disp_mfdb);
     
	/*figue out how many fat rows can fit into the buffer*/
	rows_fit = (short)(ibuff_size /(long)(fat_mfdb.fd_w * gl_nplanes));
	/*fat_mfdb.fwp = row_w* 8 * 8 /8, byte size per fat row per color plane*/
	if (rows_fit >= pt->g_h)
		rows_fit = pt->g_h; /*this is not redundant*/
	no_loop = pt->g_h / rows_fit;
	leftover = pt->g_h % rows_fit;
	if (leftover)
		no_loop++; 	/*need to go through the loop one more time*/
	for (i = 0; i < no_loop ; i++)
	{
		if (i == no_loop -1 && leftover)
			rows_in_loop = leftover;
		else
			rows_in_loop = rows_fit;
		for (k=0; k < rows_in_loop; k++)
		{
			for (j = 0 ; j < gl_nplanes ; j++)
			{
				src_mp = (long)disp_mfdb.fd_addr;
				src_mp += (long)((disp_mfdb.fd_wdwidth << 1) * 
					(disp_mfdb.fd_h * j + (rows_fit * i + k)));
				dst_mp = (long)fat_mfdb.fd_addr + (long)(fat_mfdb.fd_w *
					(rows_in_loop * j + k));
				MAGNIFY(src_mp, dst_mp, pt->g_w);
				/*copy 8 times for magnifying the height*/
				B_MOVE(dst_mp, row_w, dst_mp+(long)(row_w));
				B_MOVE(dst_mp, row_w << 1, dst_mp + (long)(row_w << 1));
				B_MOVE(dst_mp, row_w << 2, dst_mp + (long)(row_w << 2));
			}
		}
		src_area.g_h = dst_area.g_h = fat_mfdb.fd_h = rows_in_loop << 3;
		fat_mfdb.fd_stand = 1;
		vr_trnfm(gl_handle, &fat_mfdb, &fat_mfdb);
		rast_op(3, &src_area, &fat_mfdb, &dst_area, &scrn_mfdb);
		dst_area.g_y += (rows_fit << 3);
	}
	disp_mfdb.fd_stand = 1;
	vr_trnfm(gl_handle, &disp_mfdb, &disp_mfdb);
	if (grid)
		draw_gridbx();
	gsx_sclip(&full);  
} /* fattify */

short __CDECL dr_code(PARMBLK *pparmbk)
{
	PARMBLK pt;
	GRECT area;
	short xy[2];

	memcpy((void *)(((long)(&pt))), (void *)pparmbk, sizeof(PARMBLK));
	if ((long)pt.pb_tree != ad_view || pt.pb_obj != GRIDWIND ||
		pt.pb_prevstate != pt.pb_currstate)
	{
		return(pt.pb_currstate);
	}
	else
	{
		rc_intersect(&fat_area, (GRECT *)&pt.pb_xc);
		if (rc_equal(&fat_area, (GRECT *)&pt.pb_xc))
		{
			gsx_sclip(&fat_area);
			fattify(NULL);
		}
		else
		{
			area.g_x = pt.pb_xc;
			area.g_y = pt.pb_yc;
			area.g_x -= (area.g_x - fat_area.g_x) % 8;	/*snap to the 8x8 bdry*/
			area.g_y -= (area.g_y - fat_area.g_y) % 8;
			xy[0] = pt.pb_xc + pt.pb_wc;	/*the other pt of the rectangle*/
			xy[1] = pt.pb_yc + pt.pb_hc;
			xy[0] -= (xy[0] -fat_area.g_x) % 8;		/*snap to the 8x8 bdry*/
			xy[1] -= (xy[1] -fat_area.g_y) % 8;
			area.g_w = 2 + ((xy[0] - area.g_x) >> 3);
			area.g_h = 2 + ((xy[1] - area.g_y) >> 3);
			fat2sml((short *)&area, xy, 1, 1);	/*map the point to the data image*/
			xy[0] -= dat_img.g_x;	/*set the relative x, y coordinate*/
			xy[1] -= dat_img.g_y;
			area.g_x = xy[0];
			area.g_y = xy[1];
			gsx_sclip((GRECT *)&pt.pb_xc);
			fattify(&area);	/*fattify the clip rectangle in icon/data image*/
		}
		return(0);
	}
}

void fb_redraw(void)
{     
	dr_image();
	graf_mouse(M_OFF,NULL);
	gsx_sclip(&fat_area);
	fattify(NULL);	/*fattify the whole scroll area*/
	graf_mouse(M_ON,NULL);
} /* fb_redraw */

void set_bgcol(short item, short invert/*indicate if it is a invert action*/)
{
	short	color;
	long	taddr;      

	color = item - (invert ? 0 : COL0POP);
	if (color != bgcolor) 	/*new color != old color*/
	{
		bgcolor  = (color << 8) | ((*(short *)(IB_CHAR(gl_icnspec)))
			& 0xf0ff);
		(*(short *)(IB_CHAR(gl_icnspec)) = (bgcolor));
		taddr = GET_SPEC(ad_pbx,MASKWIND);
		bgcolor = color;
		copy_colplanes(taddr,BLACK, false); /*bgcolor*/
		fb_redraw();
	}
}

void set_fgcol(short item,short invert/*flag to tell if the color being inverted*/)
{
	short	 color;			      
	long 	 taddr;

	color = item - (invert? 0 : COL0POP);
	if (color != fgcolor)	/*new color != old color*/
	{
		old_fc = fgcolor;	/*old fgcolor*/
		taddr = GET_SPEC(ad_pbx, DATAWIND);
		fgcolor  = (color << 12) | ((*(short *)(IB_CHAR(gl_icnspec)))
			& 0xfff);
		if (gl_isicon)
			(*(short *)(IB_CHAR(gl_icnspec)) = (fgcolor));
		else	/* case G_IMAGE:  */
		{
			(*(short *)(BI_COLOR(taddr)) = (color));	
			(*(short *)(BI_COLOR(gl_icnspec)) = (color));	
		}
		fgcolor = color;
		copy_colplanes(taddr, BLACK, true); /*fgcolor*/
		if (!invert || !gl_isicon)
			fb_redraw(); /*invert and isicon skip*/
	}
}

/* rubber band primitives and pencil primitives */
static void rub_on(void)
{
	wind_update(3);	/*BEG_MCTRL*/
	vsl_width(gl_handle, 1);
	vsl_type(gl_handle, 1);
	vswr_mode(gl_handle, 3);
	gsx_sclip(&fat_area);
}

static void rub_off(void)
{
	wind_update(2);	/*END_MCTRL*/
	vswr_mode(gl_handle, 1);
/*	vsl_width(gl_handle, wp->line_width); */
}

static short snap_point(short *sx, short *sy)
{
	short	tempx, tempy, off_4;

	*sx = min((tempx = fat_area.g_x + fat_area.g_w - 1),
		max(fat_area.g_x, mousex));
	*sy = min((tempy = fat_area.g_y + fat_area.g_h - 1),
		max(fat_area.g_y, mousey));
	*sx += 4 - (*sx - fat_area.g_x) % 8;
	*sy += 4 - (*sy - fat_area.g_y) % 8;
	off_4 = 0;
	if (*sx > tempx)
	{
		*sx -= 4;
		off_4 = 1;
	}
	if (*sy > tempy)
	{
		*sy -= 4;
		off_4 += 4;
	}
	gridx = mousex;
	gridy = mousey;
	return(off_4);
}

static void constrn(short sx, short sy, short *cx, short *cy, short line)
{
	short	dx, dy;

	if (kstate & 3)
	{
		dx = YSCALE(ABS(*cx - sx));
		dy = ABS(*cy - sy);
		if (dx > dy)
		{
			if (line && (dx > (dy << 1)))
			{
				dx = ABS(*cx - sx);
				dy = 0;
			}
			else
			{
				dy = ABS(*cy - sy);
				dx = XSCALE(dy);
			}
		}
		else
		{
			if (line && (dy > (dx << 1)))
			{
				dx = 0;
				dy = ABS(*cy - sy);
			}
			else
			{
				dx = ABS(*cx - sx);
				dy = YSCALE(dx);
			}
		}
		if ((*cx - sx) > 0)
			*cx = sx + dx;
		else
			*cx = sx - dx;
		if ((*cy - sy) > 0)
			*cy = sy + dy;
		else
			*cy = sy - dy;
	}
}

static void rub_line(short pxy[])
{
	short	bwait;
	short	done;
	short	mon;

	rub_on();
	bwait = bstate ^ 0x1;
	mousex = pxy[0];		/* save last point in case of polygon */
	mousey = pxy[1];
	snap_point(&pxy[0], &pxy[1]);
	pxy[2] = pxy[0];
	pxy[3] = pxy[1];

	v_pline(gl_handle, 2, pxy);
	done = false;
	while (!done)
	{
		if ((kstate & 3) || grid)
		{
			graf_mouse(M_ON, NULL);
			mon = true;
		}
		else
			mon = false;
		ev_which = evnt_multi(MU_BUTTON | MU_M1,
			0x02, 0x01, bwait, 
			1, gridx, gridy, gridw, gridh,
			0, 0, 0, 0, 0,
			(short *)ad_rmsg, 0, 
			&mousex, &mousey, &bstate, &kstate,
			&kreturn, &bclicks);
		if (mon)
			graf_mouse(M_OFF, NULL);
		if (ev_which & MU_BUTTON)
		{
			if ((bclicks != 1) || (bwait == 0))
			{
				v_pline(gl_handle, 2, pxy);
				snap_point(&pxy[2], &pxy[3]);
				constrn(pxy[0], pxy[1], &pxy[2], &pxy[3], true);
				done = true;
			}
			else
				bwait = 0;
		}
	 	else /*	if (ev_which & MU_M1)*/
		{
			v_pline(gl_handle, 2, pxy);
			snap_point(&pxy[2], &pxy[3]);
			constrn(pxy[0], pxy[1], &pxy[2], &pxy[3], true);
			v_pline(gl_handle, 2, pxy);
		}
	} /* while */	
	rub_off();
}

static short rub_box(short pxy[])
{
	short	mon;
	short	done;
	short	off_4;

	rub_on();

	off_4 = snap_point(&pxy[0], &pxy[1]);
	pxy[2] = pxy[0];
	pxy[3] = pxy[1];
	pxy[4] = pxy[0];
	pxy[5] = pxy[1];
	pxy[6] = pxy[0];
	pxy[7] = pxy[1];
	pxy[8] = pxy[0];
	pxy[9] = pxy[1];

	graf_mkstate(&mousex, &mousey, &bstate, &kstate);

	if (!bstate)
	{
		rub_off();
		return(0);
	}
	else
	{
		v_pline(gl_handle, 5, pxy);
		done = false;
		while (!done)
		{
			if ((kstate & 3) || grid)
			{
				graf_mouse(M_ON, NULL);
				mon = true;
			}
			else
				mon = false;
			ev_which = evnt_multi(MU_BUTTON | MU_M1,
				0x01, 0x01, 0x00, 
				1, gridx, gridy, gridw, gridh,
				0, 0, 0, 0, 0,
				(short *)ad_rmsg, 0, 
				&mousex, &mousey, &bstate, &kstate,
				&kreturn, &bclicks);
			if (mon)
				graf_mouse(M_OFF, NULL);
			if (ev_which & MU_BUTTON)
			{
				v_pline(gl_handle, 5, pxy);
				off_4 += snap_point(&pxy[4], &pxy[5]);
				constrn(pxy[0], pxy[1], &pxy[4], &pxy[5], false);
				pxy[2] = pxy[4];
				pxy[7] = pxy[5];
				done = true;
			}
			else /* (ev_which & MU_M1) */
			{
				v_pline(gl_handle, 5, pxy);
				snap_point(&pxy[4], &pxy[5]);
				constrn(pxy[0], pxy[1], &pxy[4], &pxy[5], false);
				pxy[2] = pxy[4];
				pxy[7] = pxy[5];
				if (pxy[7] > pxy[1])
					pxy[9] = pxy[1] + 1;
				else
					pxy[9] = pxy[1] - 1;
				v_pline(gl_handle, 5, pxy);
			}
		} /* while */	
		/* we defined outside of fat border */
		/* so we subtract half of line width*/
		pxy[2] = pxy[4];
		pxy[3] = pxy[1];
		pxy[6] = pxy[0];
		pxy[7] = pxy[5];
		pxy[8] = pxy[0];
		pxy[9] = pxy[1];

		rub_off();
		return(off_4);
	}
}

static void rub_arc(short pxy[])
{
	short	done;
	short	mon;
	short	startx, starty;
	short	left_right;	/* does arc start left right or up down */

	rub_on();

	snap_point(&pxy[0], &pxy[1]);
	startx = pxy[0];
	starty = pxy[1];
	pxy[2] = 1;
	pxy[3] = 1;
	pxy[4] = 0;
	pxy[5] = 900;
	v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], pxy[4], pxy[5]);
	/* define the major direction the arc will take */
	evnt_multi(MU_M1, 0x00, 0x00, 0x00, 
		1, mousex-3, mousey-3, 7, 7,
		0, 0, 0, 0, 0,
		(short *)ad_rmsg, 0, 
		&mousex, &mousey, &bstate, &kstate,
		&kreturn, &bclicks);
	left_right = ((ABS(mousex - startx)) < (ABS(mousey - starty)));

	done = false;
	while (!done)
	{
		if ((kstate & 3) || grid)
		{
			graf_mouse(M_ON, NULL);
			mon = true;
		}
		else
			mon = false;
		ev_which = evnt_multi(MU_BUTTON | MU_M1,
			0x01, 0x01, 0x00, 
			1, mousex, mousey, 1, 1,
			0, 0, 0, 0, 0,
			(short *)ad_rmsg, 0, 
			&mousex, &mousey, &bstate, &kstate,
			&kreturn, &bclicks);
		if (mon)
			graf_mouse(M_OFF, NULL);
		if (ev_which & MU_BUTTON)
		{
			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], pxy[4], pxy[5]);
			done = true;
		}
		else  /* (ev_which & MU_M1) */
		{
			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], pxy[4], pxy[5]);
			if (left_right)
			{
				pxy[0] = mousex;
				pxy[3] = mousey;
				snap_point(&pxy[0], &pxy[3]);
				constrn(startx, starty, &pxy[0], &pxy[3], false);
				pxy[1] = starty;
				pxy[2] = ABS(pxy[0] - startx);
				pxy[3] = ABS(pxy[3] - starty);

				if (mousex > startx)
					if (mousey > starty)
						pxy[4] = 1800;
					else
						pxy[4] = 900;
				else
					if (mousey > starty)
						pxy[4] = 2700;
					else
						pxy[4] = 0;
			}
			else
			{
				pxy[1] = mousey;
				pxy[2] = mousex;
				snap_point(&pxy[2], &pxy[1]);
				constrn(startx, starty, &pxy[2], &pxy[1], false);
				pxy[0] = startx;
				pxy[2] = ABS(pxy[2] - startx);
				pxy[3] = ABS(pxy[1] - starty);

				if (mousex > startx)
					if (mousey > starty)
						pxy[4] = 0;
					else
						pxy[4] = 2700;
				else
					if (mousey > starty)
						pxy[4] = 900;
					else
						pxy[4] = 1800;
			}
			pxy[5] = pxy[4] + 900;
			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], pxy[4], pxy[5]);
		}/*else MU_M1*/
	} /* while */	
	rub_off();
}

static void rub_ellipse(short pxy[])
{
	short	done;
	short	mon;
	short	startx, starty;

	rub_on();

	snap_point(&pxy[0], &pxy[1]);
	startx = pxy[0];
	starty = pxy[1];
	pxy[2] = 1;
	pxy[3] = 1;
	v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], 0, 3600);
	done = false;
	while (!done)
	{
		if ((kstate & 3) || grid)
		{
			graf_mouse(M_ON, NULL);
			mon = true;
		}
		else
			mon = false;
		ev_which = evnt_multi(MU_BUTTON | MU_M1,
			0x01, 0x01, 0x00, 
		    1, gridx, gridy, gridw, gridh,
		    0, 0, 0, 0, 0,
		    (short *)ad_rmsg, 0, 
		    &mousex, &mousey, &bstate, &kstate,
		    &kreturn, &bclicks);
		if (mon)
			graf_mouse(M_OFF, NULL);
		if (ev_which & MU_BUTTON)
		{
			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], 0, 3600);
			done = true;
		}
		else /* (ev_which & MU_M1) */
		{
			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], 0, 3600);
			snap_point(&pxy[0], &pxy[1]);
			constrn(startx, starty, &pxy[0], &pxy[1], false);
			pxy[0] = (startx + pxy[0] + 1) >> 1;
			pxy[1] = (starty + pxy[1] + 1) >> 1;

			pxy[2] = ABS(pxy[0] - startx);
			pxy[3] = ABS(pxy[1] - starty);

			v_ellarc(gl_handle, pxy[0], pxy[1], pxy[2], pxy[3], 0, 3600);
		}
	} /* while */	
	/* we defined outside of fat border */
	/* so subtract half of line width   */
	rub_off();
}

static void scr2buf(void)
{
	if (gl_isicon && pen_on != FCLORBOX && pen_on) /*eraser/bgcolr*/
		rast_op(3, &mas_img, &scrn_mfdb, &hold_area, &hld2_mfdb);
	if (pen_on != BCLORBOX) /*eraser or fclorbox*/
		rast_op(3, &dat_img, &scrn_mfdb, &hold_area, &hold_mfdb);
}

static void flash_rec(void)
{
	short	pxy[10];

	if (selecton == 3 && selec_area.g_w && selec_area.g_h)
	{
		gsx_sclip(&fat_area);
		vswr_mode(gl_handle, 3);
		vsl_type(gl_handle, 7);
		vsl_udsty(gl_handle, selmask);
		pxy[0] = flash_area.g_x + 1;
		pxy[1] = flash_area.g_y;
		pxy[2] = flash_area.g_x + flash_area.g_w - 1;
		pxy[3] = pxy[1];
		pxy[5] = flash_area.g_y + flash_area.g_h - 1;
		pxy[4] = pxy[2];
		pxy[6] = pxy[0] - 1;
		pxy[7] = pxy[5];
		pxy[8] = pxy[6];
		pxy[9] = pxy[1];
		v_pline(gl_handle, 5, pxy);
		vsl_type(gl_handle, 1);
		vswr_mode(gl_handle, gl_mode);  /*restore the mode*/
		flash_on ^= 0xffff;
	}
}

void save_img(void)
{
	short	old_pen;

	old_pen = pen_on;
	hold_to_undo();
	pen_on = gl_isicon? ERASEBOX : FCLORBOX;	/*fake it*/
	scr2buf();
	pen_on = old_pen;
	save_fat(false);
	gsx_sclip(&fat_area);
	fattify(NULL);	/*fattify the whole scroll area*/
	inverted = false;
}

void rubrec_off(void)
{
	if (selecton == 3 && flash_on)	/*turn off the flashing rectangle*/
	{
		flash_rec();		/*if the flashing rectangle presents*/
		selecton &= 0x0001;	/*turn off the rect. present flag*/
	}
	flash_area.g_h = flash_area.g_w = selec_area.g_h = selec_area.g_w = 0;
	disab_obj(ad_menu, COPYITEM);
	disab_obj(ad_menu, CUTITEM);
	disab_obj(ad_menu, DELITEM);
}

static void do_select(void)
{
	GRECT	src_area, new_area, temp_area;
	short	t1pxy[10], t2pxy[4], off_4;

	if (flash_area.g_w && flash_area.g_h)
	{
		if (rc_inside(&flash_area, mousex, mousey)) /*in existing selec.area*/
		{
			graf_mkstate(&mousex, &mousey, &bstate, &kstate);
			graf_dragbox(flash_area.g_w, flash_area.g_h, flash_area.g_x,
			flash_area.g_y, fat_area.g_x, fat_area.g_y,
			fat_area.g_w, fat_area.g_h, &t1pxy[0], &t1pxy[1]);
			/*snap to the 8x8 square boudary*/
			t1pxy[0] -= (t1pxy[0] - fat_area.g_x) % 8;
			t1pxy[1] -= (t1pxy[1] - fat_area.g_y) % 8;
			t2pxy[0] = flash_area.g_x;	  
			t2pxy[1] = flash_area.g_y;
			if (t1pxy[0] != t2pxy[0] || t1pxy[1] != t2pxy[1])
			{
				graf_mbox(flash_area.g_w, flash_area.g_h, flash_area.g_x,
				flash_area.g_y, t1pxy[0], t1pxy[1]);
				/* source's starting x,y coordinates in fat area*/
				new_area.g_w =src_area.g_w =temp_area.g_w =selec_area.g_w;
				new_area.g_h =src_area.g_h =temp_area.g_h =selec_area.g_h;
				fat2sml(t1pxy, (short *)smlxy, 1, 1);  /*destination in data area*/
				new_area.g_x = smlxy[0];
				new_area.g_y = smlxy[1];
				src_area.g_x = selec_area.g_x;
				src_area.g_y = selec_area.g_y;

				graf_mouse(M_OFF, NULL);
				if (kstate & 4)	/*control key pressed, remove source area*/
				{
					disp_mfdb.fd_stand = 0;
					disp_mfdb.fd_w = gl_wimage;
					disp_mfdb.fd_wdwidth = hold_mfdb.fd_wdwidth;
					disp_mfdb.fd_h = gl_himage;
					temp_area.g_x = temp_area.g_y = 0;
					rast_op(3,&src_area,&scrn_mfdb,&temp_area,&disp_mfdb);
					del_img(false, 1);
					rast_op(3,&temp_area,&disp_mfdb,&new_area,&scrn_mfdb);
				}
				else
					rast_op(3,&src_area,&scrn_mfdb,&new_area,&scrn_mfdb);
				if (gl_isicon)	/*take care the mask part*/
				{
					fat2sml(t1pxy, (short *)smlxy, 1, 0);
					new_area.g_x = smlxy[0];
					new_area.g_y = smlxy[1];
					src_area.g_x += deltax;
					src_area.g_y += deltay;
					if (kstate & 4)/*remove source area*/
					{
						rast_op(3,&src_area,&scrn_mfdb,&temp_area,&disp_mfdb);
						del_img(false, 2);
						rast_op(3,&temp_area,&disp_mfdb,&new_area,&scrn_mfdb);
		 			}
					else
						rast_op(3,&src_area,&scrn_mfdb,&new_area,&scrn_mfdb);
				}
				save_img();
				graf_mouse(M_ON, 0x0L);
			}	/*if source and destination are not the same*/
			return;
		}	/*if inside the existing selected area*/
		else
			/*if it is outside the existing flash area, xor the rubber rec*/
			rubrec_off();	/*so the existing one vanished*/
	}	/*if there is a selected area*/

	/*start a new selected area*/
	graf_mouse(M_OFF, NULL);
	off_4 = rub_box(t1pxy);
	/*the snap points are in the middle of the 8x8 square, so fix it
	to make the starting point in the boudary in stead of the middle*/
	flash_area.g_x = min(t1pxy[0], t1pxy[4]) - 4;
	flash_area.g_y = min(t1pxy[1], t1pxy[5]) - 4;
	flash_area.g_w = (ABS(t1pxy[4] - t1pxy[0])) + 8 - (off_4 & 0x0003) * 4;
	flash_area.g_h = (ABS(t1pxy[5] - t1pxy[1])) + 8 - (off_4 & 0x000c);
	selec_area.g_w = flash_area.g_w >> 3;
	selec_area.g_h = flash_area.g_h >> 3;
	fat2sml((short *)&flash_area, (short *)&selec_area, 1, 1);
	flash_on = 0x0000;
	selecton = 3;
	flash_rec();
	graf_mouse(M_ON, 0x0L);
	/*enable menu copy, cut, delete*/
	enab_obj(ad_menu, CUTITEM);
	enab_obj(ad_menu, COPYITEM);
	enab_obj(ad_menu, DELITEM);
}

static void do_button(void)
{
	short	i, j, k, colr;
	short	t1pxy[10];
	short	t2pxy[4];

	if (pen_on == ERASEBOX)
	{
		j = gl_isicon ? 2 : 1;	/*if icon, need to erase data & mask*/
		colr = WHITE;
	}
	else
	{
		j = 1;
		colr = gl_isicon ? BLACK : fgcolor;
	}
	gsx_attr(false, 1, colr);
	vsf_color(gl_handle, colr);
	vsf_interior(gl_handle, 1);	/*solid style for filling pattern*/

	k = (pen_on == BCLORBOX)? 0 : 1;
	if (rcs_hot == RUBERBOX)
		do_select();
	else
	{
		graf_mouse(M_OFF, NULL);
		hold_to_undo();
 	  	switch (rcs_hot)
    	{
			case LINEBOX:
				t1pxy[0] = mousex;
				t1pxy[1] = mousey;
				rub_line(t1pxy);
				while(j--)
				{
					fat2sml(t1pxy, (short *)smlxy, 2, k--);
					v_pline(gl_handle, 2, (short *)smlxy);
				}
				scr2buf();
				save_fat(false);
				gsx_sclip(&fat_area);
				fattify(NULL);	/*fattify the whole scroll area*/
				graf_mouse(M_ON, NULL);
				break;
			case HARCBOX:
			case SARCBOX:
				rub_arc(t1pxy);
				while(j--)
				{
					fat2sml(t1pxy, (short *)smlxy, 1, k--);
					if (rcs_hot == SARCBOX)
						v_ellpie(gl_handle, smlxy[0], smlxy[1], t1pxy[2]>>3, t1pxy[3]>>3,
							t1pxy[4], t1pxy[5]);
					else
						v_ellarc(gl_handle, smlxy[0], smlxy[1], t1pxy[2]>>3, t1pxy[3]>>3,
							t1pxy[4], t1pxy[5]);
				}
				scr2buf();
				save_fat(false);
				gsx_sclip(&fat_area);
				fattify(NULL);	/*fattify the whole scroll area*/
				graf_mouse(M_ON, NULL);
				break;
			case SRCTGBOX:
			case HRCTGBOX:
				rub_box(t1pxy);
				t2pxy[0] = t1pxy[0];
				t2pxy[1] = t1pxy[1];
				t2pxy[2] = t1pxy[4];
				t2pxy[3] = t1pxy[5];
				while(j--)
				{
					fat2sml(t2pxy, (short *)smlxy, 2, k);
					vsl_ends(gl_handle, 2, 2);	/* set rounded ends	*/
					if (rcs_hot == SRCTGBOX)  /* && wp->fill_style)   */
						v_bar(gl_handle, (short *)smlxy);
					else
					{
						fat2sml(t1pxy, (short *)smlxy, 5, k);
						v_pline(gl_handle, 5, (short *)smlxy);
					}
					k--;
				}
				scr2buf();
				save_fat(false);
				gsx_sclip(&fat_area);
				fattify(NULL);	/*fattify the whole scroll area*/
				graf_mouse(M_ON, NULL);
				break;
			case DOTBOX:
 			case NIL:	/*default action*/
				set_pix();
				save_fat(false);
				graf_mouse(M_ON, NULL);
				break;
			case HOVALBOX:
			case SOVALBOX:
				rub_ellipse(t1pxy);
				while(j--)
				{
					fat2sml(t1pxy, (short *)smlxy, 1, k--);
					vsl_ends(gl_handle, 2, 2);	/* set rounded ends	*/
					if (rcs_hot == SOVALBOX)
						v_ellipse(gl_handle, smlxy[0], smlxy[1], t1pxy[2]>>3, t1pxy[3]>>3);
					else
						v_ellarc(gl_handle, smlxy[0], smlxy[1], t1pxy[2]>>3, t1pxy[3]>>3, 0, 3600);
				}
				scr2buf();
				save_fat(false);
				gsx_sclip(&fat_area);
				fattify(NULL);	/*fattify the whole scroll area*/
				graf_mouse(M_ON, NULL);
				break;
			case HPLYGBOX:
			case SPLYGBOX:
				vsl_ends(gl_handle, 2, 2);	/* set round ends	*/
				SCRATCH[0] = mousex;
				SCRATCH[1] = mousey;
				for (i = 0 ; i < ((MAX_POLY_POINTS - 1) * 2) ; i+=2)
				{
					rub_line((short *)&SCRATCH[i]);
					v_pline(gl_handle, 2, (short *)&SCRATCH[i]);
					fat2sml((short *)&SCRATCH[i], (short *)&smlxy[i], 2, k); 
					if (bclicks == 1)
					{
again:					gsx_attr(false, 1, colr);
						v_pline(gl_handle, 2, (short *)&smlxy[i]);
						if (j==2)
						{
							fat2sml((short *)&SCRATCH[i], (short *)&smlxy[i], 2, k-1);
							j--;
							goto again;
						}
						else if(pen_on == ERASEBOX && gl_isicon)
							j++;
						scr2buf();
						save_fat(false);
						gsx_sclip(&fat_area);
	 					fattify(NULL);	/*fattify the whole scroll area*/
					}
					else
						break;
					if ((SCRATCH[i+2]==SCRATCH[0]) && (SCRATCH[i+3]==SCRATCH[1]))
						break;
				}
				i+=2;
				SCRATCH[i] = SCRATCH[0];
				SCRATCH[i+1] = SCRATCH[1];
				i = i >> 1;
				if (rcs_hot == SPLYGBOX)
				{
					vsl_ends(gl_handle, 2, 2);	/* set round ends	*/
					while(j--)
					{
						fat2sml((short *)SCRATCH, (short *)smlxy, i, k--); 
						vsf_color(gl_handle, colr);
						vswr_mode(gl_handle, 1);
						v_fillarea(gl_handle, i++, (short *)smlxy);
					}
					scr2buf();
					save_fat(false);
					gsx_sclip(&fat_area);
					fattify(NULL);	/*fattify the whole scroll area*/
				}
				graf_mouse(M_ON, NULL);
				break;
		} /* case */
	}	/*else*/
	vswr_mode(gl_handle, gl_mode);  /*restore the mode*/
	inverted = false;
}

static void do_popmenu(short which, short item)
{
	switch (which)
	{
		case BCLORBOX:
			set_bgcol( item, false);
      		break;
		case FCLORBOX:
			set_fgcol( item, false);
			break;
		default:
			return;
	}
	draw_clipb(false, (GRECT *)&orign_area);/*redraw the clip board image*/
	icn_edited = true;
	rcs_edited = true;
	set_icnmenus();
}

static void fb_tools_wait(void)
{
	out1 = false;		/* wait to enter */
	xwait = itool.g_x ;
	ywait = itool.g_y;
	wwait = itool.g_w ;
	hwait = itool.g_h  ;
}

static void do_pen(short pencol)
{
	if (pencol == BCLORBOX && !is_mask)
	{
		is_mask = true;
		wind_set_str(rcs_view, WF_NAME, "Editing Mask");
	}
	else if (pencol == FCLORBOX && is_mask)
	{
		is_mask = false;
		wind_set_str(rcs_view, WF_NAME, "Editing Data");
	}
	if (pencol == BCLORBOX)
		colour = bgcolor;
	else if (pencol == ERASEBOX)
		colour = WHITE;
	else 	/* if (pencol == FCLORBOX || pencol == 0) */
		colour = fgcolor;
}

static void hndl_hot_tlbx(short obj, short clicks)
{
	short	popmenu;

	if ( obj != NIL && (GET_FLAGS(ad_itool, obj) & SELECTABLE))
	{
		if (obj==FCLORBOX || obj==BCLORBOX || obj==ERASEBOX)
		{
			if (pen_on)
				invert_obj(ad_itool, pen_on, &itool);
			if (clicks == 2 && (popmenu = hndl_pop(obj, true)) != NIL)
				do_popmenu(obj, popmenu); 
			do_pen(obj);
			pen_on = obj;
			if (paste_img)
			{
				paste_img = false;
				out2 = false;
			}
			if (selecton)	/* if ruberbox is on, turn if off*/
			{
				rubrec_off();/*turn off the flashing rect.*/
				selecton = false;
				invert_obj(ad_itool, rcs_hot, &itool); /*turn off ruberbox*/
				rcs_hot = NIL;		/* set dot as default tool*/
			}
		}
		else if(obj == GRIDBOX)
			grid ^= 0x01;
		else if(obj == UNDOBOX)
		{
			invert_obj(ad_itool, obj, &itool);
			if (inverted)	/*if last action is invert*/
				invrt_img();
			else
			{
				graf_mouse(M_OFF, NULL);
				save_fat(true);	/*handle undo*/
				gsx_sclip(&fat_area);
				fattify(NULL);	/*fattify the whole scroll area*/
	 			graf_mouse(M_ON, NULL);
			}
		}
		else
		{
			if (obj == RUBERBOX)
			{
				if (pen_on)
				{
					invert_obj(ad_itool, pen_on, &itool);
					pen_on = false;
				}
				gsx_attr(false, 3, BLACK);
				selecton = true;
			}
			else if (selecton)
			{
				rubrec_off();/*turn off the selected ruber rectangle*/
				selecton = false;
				if (!pen_on)
					colour = fgcolor; /*if no pen selected*/
			}
			hot_off(); 	/*turn off the old one*/
			rcs_hot = obj;
		}
		invert_obj(ad_itool, obj, &itool); /* turn on the newly selected*/
		if (obj == GRIDBOX)
		{
			gsx_sclip(&fat_area);
			if (grid)
				draw_gridbx();
			else
			{
				graf_mouse(M_OFF, 0x0L);
				fattify(NULL); /*fattify the whole scroll area*/
		 		graf_mouse(M_ON, NULL);
			}
		}
		fb_tools_wait();
	}
}

static short fb_button(short clicks)
{
	short	done, tool_obj;

	done = false;    
	graf_mouse(M_OFF, NULL);
	if (rc_inside(&fat_area, mousex, mousey))
	{
		if(paste_img)
		{
			do_paste_img();
			mflags &= (~MU_M2);
			paste_img = false;
			out2 = false;
			graf_mouse(0, NULL);
		}
		else
			do_button();
		icn_edited = true;
		rcs_edited = true;
		set_icnmenus();
	}
	else if (rc_inside(&itool, mousex, mousey))
	{	    
		graf_mouse(M_ON, NULL);
		tool_obj = objc_find((OBJECT  *)ad_itool, 0, MAX_DEPTH, mousex, mousey);
		hndl_hot_tlbx( tool_obj, clicks);
		return(done);					
	}
	graf_mouse(M_ON,0x0L); 	     
	return(done);
} /* fb_button */

void undo_img(void)
{
	if (gl_isicon)
		rast_op(3, &hold_area, &sav2_mfdb, &hold_area, &hld2_mfdb);
	rast_op(3, &hold_area, &save_mfdb, &hold_area, &hold_mfdb);
	save_fat(false);
	graf_mouse(M_OFF, NULL);
	gsx_sclip(&fat_area);
	fattify(NULL);	/*fattify the whole scroll area*/
	graf_mouse(M_ON, NULL);
}

static void before(void)
{
	save_fat(false);
	if (selecton == 3 && flash_on)	/*turn off the flashing rectangle*/
	{
		flash_rec();		/*if the flashing rectangle presents*/
		selecton &= 0x0001;	/*turn off the rect. present flag*/
	}
	if (selecton)
	{
		flash_area.g_x += rcs_xpan;
		flash_area.g_y += rcs_ypan;
	}
}

static void after(void)
{
	GRECT	temp_area;

	if (selecton)
	{
		flash_area.g_x -= rcs_xpan;
		flash_area.g_y -= rcs_ypan;
		rc_copy(&flash_area, &temp_area);
		if (rc_intersect(&fat_area, &temp_area))
		{
			selecton = 3;
			flash_on = 0x0000;
			flash_rec();
		}
	}
	set_scroll();  
	set_icnmenus();
}

static short do_fbmesag(void)
{		 
	short	done;

    done = false;	
	switch ( rcs_rmsg[0] )
	{
		case MN_SELECTED:	
			done = hndl_menu(rcs_rmsg[3], rcs_rmsg[4]);
			break;
		case WM_REDRAW:		     
			hndl_redraw(rcs_rmsg[3], (GRECT *) &rcs_rmsg[4]); 
			break;
		case WM_TOPPED:
			wind_set(rcs_rmsg[3], WF_TOP, 0, 0, 0, 0);
			break;
		case WM_CLOSED:			/* must exit from fatbits	*/
			done = true;
			break;
		case WM_FULLED:			/* must exit from fatbits	*/
			break;
		case WM_ARROWED:
			before();
			hndl_arrowed( rcs_rmsg[3], rcs_rmsg[4]);
			after();
			break;
		case WM_HSLID:
			before();
			hndl_hslid(rcs_rmsg[3], rcs_rmsg[4]);
			after();
			break;
		case WM_VSLID:
			before();
			hndl_vslid(rcs_rmsg[3], rcs_rmsg[4]);
			after();
			break;
		case WM_SIZED:			/* must exit from fatbits	*/
			break;
		case WM_MOVED:
			break;
	} /* switch */
	rcs_rmsg[0] = 0;
	return(done);	
} /* do_fbmesag */

void iconmain(void)
{
	short	ev_which, done, button;

	/* wait for events	*/
	mflags = MU_TIMER | MU_BUTTON | MU_MESAG | MU_M1 | MU_KEYBD;
	fb_tools_wait();
	flash_on = 0x0000;
	out2 = false;
	button = 0x01;
	done = false;
	gsx_sclip(&full);
	while (!done)
	{
		wind_update(false);
		ev_which = evnt_multi(mflags, 0x02, 0x01, button,
			out1, xwait, ywait, wwait, hwait,
			out2, fat_area.g_x, fat_area.g_y,
			fat_area.g_w, fat_area.g_h,
			(short *)ad_rmsg, 250, 
			&mousex, &mousey, &bstate, &kstate,
			&kreturn, &bclicks);
		wind_update(true);

		if (ev_which & MU_MESAG)
			done = do_fbmesag();
		if (selecton == 3 && (ev_which & MU_TIMER))
		{
			graf_mouse(M_OFF, NULL);
			flash_rec();
			graf_mouse(M_ON, NULL);
		}
		if (ev_which & MU_M1)
		{
			if ( rc_inside(&fat_area, mousex, mousey) && (button == 0x0) )
			{	
				graf_mouse(M_OFF, NULL);
				hold_to_undo();
				set_pix();	
				save_fat(false);
				graf_mouse(M_ON, NULL);
				if (!icn_edited)
				{
					icn_edited = true;
					rcs_edited = true;
					set_icnmenus();
				}
			}
			else
				fb_tools_wait();
		} /* if MU_M1 */
		/*MU_M2 included in mflags for paste only, set in hndl_editmenu*/
		if (paste_img && (ev_which & MU_M2))
		{ 
			if (out2)
				graf_mouse(ARROW, NULL); /*outside of fat area, show arrow*/
			else
				graf_mouse(THIN_CROSS, NULL);/*inside of fat area show thin X hair*/
			out2 = !out2;
		}	/*if MU_M2*/
		if (ev_which & MU_BUTTON)
		{
			if (button == 0x01)
			{
				done = fb_button(bclicks);
				button = 0x0;
			} /* if */
			else
			{
				button = 0x01;
			} /* else */
		} /* if MU_BUTTON */
		if (ev_which & MU_KEYBD)
		{
			if ((kreturn & 0xFF) == 0x1B)
			{
				undo_img();
			} /* if */
			else
				done = hndl_keybd(kreturn);
		} /* if KEYBD */
	} /* while */
} 
