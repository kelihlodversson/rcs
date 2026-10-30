/*	RCSMONO.C	one-plane images in memory	*/
#include <gem.h>
#include <stdbool.h>
#include "rcsdefs.h"
#include "rcsiext.h"
#include "rcslib.h"
#include "rcsvdi.h"
#include "rcsmono.h"

extern	short	gl_xclip, gl_yclip, gl_wclip, gl_hclip;

short edit_colour(void)
{
	return gl_isicon ? BLACK : fgcolor;
}

short is_mono(const MFDB *m)
{
	return m->fd_addr != NULL && m->fd_nplanes == 1 && m->fd_stand;
}

/* the words of an image are the words of the host, the top bit the left pixel */
static unsigned short *row(const MFDB *m, short y)
{
	return (unsigned short *)m->fd_addr + (long)y * m->fd_wdwidth;
}

static short getbit(const MFDB *m, short x, short y)
{
	return (row(m, y)[x >> 4] >> (15 - (x & 15))) & 1;
}

static void putbit(const MFDB *m, short x, short y, short v)
{
	unsigned short	*p = row(m, y) + (x >> 4);
	unsigned short	bit = 0x8000 >> (x & 15);

	if (v)
		*p |= bit;
	else
		*p &= ~bit;
}

/* the modes of vro_cpyfm, on one bit */
static short rop(short mode, short s, short d)
{
	switch (mode)
	{
		case 0:	return 0;
		case 1:	return s & d;
		case 2:	return s & !d;
		case 3:	return s;
		case 4:	return (!s) & d;
		case 5:	return d;
		case 6:	return s ^ d;
		case 7:	return s | d;
		case 8:	return !(s | d);
		case 9:	return !(s ^ d);
		case 10: return !d;
		case 11: return s | !d;
		case 12: return !s;
		case 13: return (!s) | d;
		case 14: return !(s & d);
		default: return 1;
	}
}

void mono_rop(short mode, GRECT *sa, MFDB *s, GRECT *da, MFDB *d)
{
	short	x, y, w, h;

	w = min(sa->g_w, da->g_w);
	h = min(sa->g_h, da->g_h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			putbit(d, da->g_x + x, da->g_y + y, rop(mode,
				s ? getbit(s, sa->g_x + x, sa->g_y + y) : 0,
				getbit(d, da->g_x + x, da->g_y + y)));
}

static short sig_pel[16], sig_idx[16];

void calibrate_colours(short x, short y)
{
	GRECT	clip;
	short	c, pxy[4];

	clip.g_x = gl_xclip;
	clip.g_y = gl_yclip;
	clip.g_w = gl_wclip;
	clip.g_h = gl_hclip;
	gsx_sclip(&full);
	graf_mouse(M_OFF, NULL);
	vswr_mode(gl_handle, 1);
	vsl_type(gl_handle, 1);
	vsl_width(gl_handle, 1);
	for (c = 0; c < 16; c++)
	{
		vsl_color(gl_handle, c);
		pxy[0] = pxy[2] = x + 12 + c;
		pxy[1] = pxy[3] = y + 12;
		v_pline(gl_handle, 2, pxy);
		v_get_pixel(gl_handle, x + 12 + c, y + 12, &sig_pel[c], &sig_idx[c]);
	}
	vsl_color(gl_handle, gl_lcolor);
	vswr_mode(gl_handle, gl_mode);
	graf_mouse(M_ON, NULL);
	gsx_sclip(&clip);
}

short pixel_colour(short x, short y)
{
	short	c, pel, idx;

	v_get_pixel(gl_handle, x, y, &pel, &idx);
	for (c = 0; c < 16; c++)
		if (pel == sig_pel[c] && idx == sig_idx[c])
			return c;
	return WHITE;
}

void scrn_to_mono(GRECT *sa, MFDB *d, GRECT *da, short colour)
{
	GRECT	clip;
	short	x, y, w, h;

	/* the clip rectangle is for drawing; look at all of the screen */
	clip.g_x = gl_xclip;
	clip.g_y = gl_yclip;
	clip.g_w = gl_wclip;
	clip.g_h = gl_hclip;
	gsx_sclip(&full);
	w = min(sa->g_w, da->g_w);
	h = min(sa->g_h, da->g_h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			putbit(d, da->g_x + x, da->g_y + y,
				pixel_colour(sa->g_x + x, sa->g_y + y) == colour);
	gsx_sclip(&clip);
}

void mono_to_scrn(GRECT *sa, MFDB *s, GRECT *da, short colour)
{
	short	pxy[8], colours[2];

	grect_to_array(sa, pxy);
	grect_to_array(da, &pxy[4]);
	colours[0] = colour;
	colours[1] = WHITE;
	vrt_cpyfm(gl_handle, MD_REPLACE, pxy, s, &scrn_mfdb, colours);
}
