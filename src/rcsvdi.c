/*	RCSVDI.C	04/09/85	Tim Oren converted from:	*/
/*	APGSXIF.C	05/06/84 - 12/21/84	Lee Lorenzen		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include "rcslib.h"
#include "rcsvdi.h"

#define ORGADDR 0x0L

 short	gl_width;
 short	gl_height;

 short	gl_nrows;
 short	gl_ncols;

 short	gl_wchar;
 short	gl_hchar;

 short	gl_wschar;
 short	gl_hschar;

 short	gl_wptschar;
 short	gl_hptschar;

 short	gl_wsptschar;
 short	gl_hsptschar;

 short	gl_wbox;
 short	gl_hbox;

 short	gl_xclip;
 short	gl_yclip;
 short	gl_wclip;
 short	gl_hclip;

 short 	gl_nplanes;
 short 	gl_handle;

 MFDB		gl_src;
 MFDB		gl_dst;
 MFDB		gl_tmp;

 WS		gl_ws;

 short		intin[128];
 short		ptsin[128];
 short		intout[128];
 short		ptsout[128];

 long		ad_intin;

 short		gl_mode;
 short		gl_tcolor;
 short		gl_lcolor;
 short		gl_fis;
 short		gl_patt;

 GRECT		gl_rscreen;
 GRECT		gl_rfull;
 GRECT		gl_rzero;
 GRECT		gl_rcenter;
 GRECT		gl_rmenu;

static void gsx_fix(MFDB *pfd, long theaddr, short wb, short h)
{
	if (theaddr == ORGADDR)
	{
		pfd->fd_w = gl_ws.ws_xres + 1;
		pfd->fd_wdwidth = pfd->fd_w / 16;
		pfd->fd_h = gl_ws.ws_yres + 1;
		pfd->fd_nplanes = gl_nplanes;
	}
	else
	{
		pfd->fd_wdwidth = wb / 2;
		pfd->fd_w = wb * 8;
		pfd->fd_h = h;
		pfd->fd_nplanes = 1;
	}
	pfd->fd_stand = false;
	pfd->fd_addr = (void *)theaddr;
}

static void bb_fill(short mode, short fis, short patt, short hx, short hy, short hw, short hh)
{
	gsx_fix(&gl_dst, 0x0L, 0, 0);
	ptsin[0] = hx;
	ptsin[1] = hy;
	ptsin[2] = hx + hw - 1;
	ptsin[3] = hy + hh - 1;

	gsx_attr(true, mode, gl_tcolor);
	if (fis != gl_fis)
	{
		vsf_interior(gl_handle, fis);
		gl_fis = fis;
	}
	if (patt != gl_patt)
	{
		vsf_style( gl_handle, patt );
		gl_patt = patt;
	}
	vr_recfl(gl_handle, &ptsin[0]);
}

void gr_rect(short icolor, short ipattern, GRECT *pt)
{
	short		fis;

	fis = FIS_PATTERN;
	if (ipattern == IP_HOLLOW)
		fis = FIS_HOLLOW;
	else if (ipattern == IP_SOLID)
		fis = FIS_SOLID;

	vsf_color(gl_handle, icolor);
	bb_fill(MD_REPLACE, fis, ipattern, 
		pt->g_x, pt->g_y, pt->g_w, pt->g_h);
} 

void gsx_sclip(GRECT *pt)
{
	rc_get(pt, &gl_xclip, &gl_yclip, &gl_wclip, &gl_hclip);

	if ( gl_wclip && gl_hclip )
	{
		ptsin[0] = gl_xclip;
		ptsin[1] = gl_yclip;
		ptsin[2] = gl_xclip + gl_wclip - 1;
		ptsin[3] = gl_yclip + gl_hclip - 1;
		vs_clip(gl_handle, true, &ptsin[0]);
	}
	else
		vs_clip(gl_handle, false, &ptsin[0]);
} /* gsx_setclip */

void gsx_attr(short text, short mode, short color)
{
	if (mode != gl_mode)
	{
		vswr_mode( gl_handle, mode );
		gl_mode = mode;
	}
	if (text)
	{
		if (color != gl_tcolor)
		{
			vst_color(gl_handle, color);
			gl_tcolor = color;
		}
	}	
	else
	{
		if (color != gl_lcolor)
		{
			vsl_color(gl_handle, color);
			gl_lcolor = color;
		}
	}
}

void gsx_bxpts(GRECT *pt)
{
	ptsin[0] = pt->g_x;
	ptsin[1] = pt->g_y;
	ptsin[2] = pt->g_x + pt->g_w - 1;
	ptsin[3] = pt->g_y;
	ptsin[4] = pt->g_x + pt->g_w - 1;
	ptsin[5] = pt->g_y + pt->g_h - 1;
	ptsin[6] = pt->g_x;
	ptsin[7] = pt->g_y + pt->g_h - 1;
	ptsin[8] = pt->g_x;
	ptsin[9] = pt->g_y;
}

static void gsx_xline(short ptscount, short *ppoints)
{
	static	short	hztltbl[2] = { 0x5555, 0xaaaa };
	static  short	verttbl[4] = { 0x5555, 0xaaaa, 0xaaaa, 0x5555 };
	short		*linexy,i;
	short		st;

	for ( i = 1; i < ptscount; i++ )
	{
		if ( *ppoints == *(ppoints + 2) )
		 {
			st = verttbl[( (( *ppoints) & 1) | ((*(ppoints + 1) & 1 ) << 1))];
		}	
		else
		{
			linexy = ( *ppoints < *( ppoints + 2 )) ? ppoints : ppoints + 2;
			st = hztltbl[( *(linexy + 1) & 1)];
		}
		vsl_udsty( gl_handle, st );
		v_pline( gl_handle, 2, ppoints );
		ppoints += 2;
	}
	vsl_udsty( gl_handle, 0xffff );
}	

void gsx_xbox(GRECT *pt)
{
	gsx_bxpts(pt);
	gsx_xline( 5, &ptsin[0] );
}

void gsx_untrans(long saddr, short swb, long daddr, short dwb, short h)
{
	gsx_fix(&gl_src, saddr, swb, h);
	gl_src.fd_stand = false;
	gl_src.fd_nplanes = 1;

	gsx_fix(&gl_dst, daddr, dwb, h);
	vr_trnfm(gl_handle, &gl_src, &gl_dst);
}

void gsx_trans(long saddr, short swb, long daddr, short dwb, short h)
{
	gsx_fix(&gl_src, saddr, swb, h);
	gl_src.fd_stand = true;
	gl_src.fd_nplanes = 1;

	gsx_fix(&gl_dst, daddr, dwb, h);
	vr_trnfm(gl_handle, &gl_src, &gl_dst);
}

void gsx_outline(GRECT *pt)
{
	vs_clip(gl_handle, false, &ptsin[0]);
	gsx_attr(false, MD_XOR, BLACK);
	gsx_bxpts(pt);
	v_pline(gl_handle, 5, &ptsin[0]);
}

void gsx_invert(GRECT *pt)
{
	vsf_color(gl_handle, BLACK);
	vs_clip(gl_handle, false, &ptsin[0]); 
	bb_fill(MD_XOR, FIS_SOLID, IP_SOLID,
		pt->g_x, pt->g_y, pt->g_w, pt->g_h);
}

short ch_width(short fn)
{
	if (fn == IBM)
		return(gl_wchar);
	else if (fn == SMALL)
		return(gl_wschar);
	else
		return(0);
}

short ch_height(short fn)
{
	if (fn == IBM)
		return(gl_hchar);
	else if (fn == SMALL)
		return(gl_hschar);
	else
		return(0);
}

short gsx_start(void)
{
	short		char_height, nc, attrib[10];

	gl_xclip = 0;
	gl_yclip = 0;
	gl_width = gl_wclip = gl_ws.ws_xres + 1;
	gl_height = gl_hclip = gl_ws.ws_yres + 1;

	nc = gl_ws.ws_ncolors;
	gl_nplanes = 0;
	while (nc != 1)
	{
		nc >>= 1;
		gl_nplanes++;
	}
	vqt_attributes( gl_handle, &attrib[0] );
	gl_ws.ws_chmaxh = attrib[7];
	gl_ws.ws_pts2 = attrib[6];
	char_height = gl_ws.ws_chminh;
	vst_height( gl_handle, char_height,
				&gl_wsptschar, &gl_hsptschar, 
				&gl_wschar, &gl_hschar );
	char_height = gl_ws.ws_chmaxh;
	vst_height( gl_handle, char_height,
				&gl_wptschar, &gl_hptschar, 
				&gl_wchar, &gl_hchar );
	gl_ncols = gl_width / gl_wchar;
	gl_nrows = gl_height / gl_hchar;
	gl_hbox = gl_hchar + 3;
	gl_wbox = (gl_hbox * gl_ws.ws_hpixel) / gl_ws.ws_wpixel;
	vsl_type( gl_handle, 7 );
	vsl_width( gl_handle, 1 );
	vsl_udsty( gl_handle, 0xffff );
	rc_set(&gl_rscreen, 0, 0, gl_width, gl_height);
	rc_set(&gl_rfull, 0, gl_hbox, gl_width, (gl_height - gl_hbox));
	rc_set(&gl_rzero, 0, 0, 0, 0);
	rc_set(&gl_rcenter, (gl_width-gl_wbox)/2, (gl_height-(2*gl_hbox))/2, 
			gl_wbox, gl_hbox);
	rc_set(&gl_rmenu, 0, 0, gl_width, gl_hbox);
	ad_intin = (long)((long)(&intin[0]));
	return (true);
}

void gsx_vopen(void)
{
	short	i;

	for(i=0; i<10; i++)
		intin[i] = 1;
	intin[10] = 2;			/* device coordinate space */

	v_opnvwk( &intin[0], &gl_handle, (short *)&gl_ws );
}

static void bb_set(register short sx, register short sy, register short sw, register short sh,
	register short *pts1, register short *pts2, MFDB *pfd, MFDB *psrc, MFDB *pdst)
{
	short		oldsx;

	/* get on word boundary	*/
	oldsx = sx;
	sx = (sx / 16) * 16;
	sw = ( ((oldsx - sx) + (sw + 15)) / 16 ) * 16;

	gl_tmp.fd_stand = true;
	gl_tmp.fd_wdwidth = sw / 16;
	gl_tmp.fd_w = sw;
	gl_tmp.fd_h = sh;

	graf_mouse(M_OFF, NULL);
	pts1[0] = sx;
	pts1[1] = sy;
	pts1[2] = sx + sw - 1;
	pts1[3] = sy + sh - 1;
	pts2[0] = 0;
	pts2[1] = 0;
	pts2[2] = sw - 1;
	pts2[3] = sh - 1 ;

	gsx_fix(pfd, 0x0L, 0, 0);
	vro_cpyfm( gl_handle, S_ONLY, &ptsin[0], psrc, pdst );
	graf_mouse(M_ON, NULL);
}

void bb_save(GRECT *ps)
{	
	bb_set(ps->g_x, ps->g_y, ps->g_w, ps->g_h, &ptsin[0], &ptsin[4], 
		&gl_src, &gl_src, &gl_tmp);
}

void bb_restore(GRECT *pr)
{
	bb_set(pr->g_x, pr->g_y, pr->g_w, pr->g_h, &ptsin[4], &ptsin[0], 
		&gl_dst, &gl_tmp, &gl_dst);
}
