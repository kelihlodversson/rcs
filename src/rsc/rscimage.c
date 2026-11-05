/* RSCIMAGE.C	icon and bitmap images as pictures (see rscimage.h): PNM files,
 *		the standard VDI palette, planar words and chunky pixels, depth
 *		conversion, and the images of the objects of a model
 *
 * Pure C99 and no I/O, so that it compiles for the Atari and the Raspberry
 * Pi as well as for the host.  The functions that replace an image inside
 * the model allocate every new block first and swap the pointers last, so
 * a failure leaves the model as it was; they write the data pointers of the
 * records themselves, which only the library may (rscmodel.h, "Direct
 * access"): the blocks come from rsc_malloc, have the sizes the size fields
 * say and replace blocks freed with rsc_mfree.
 */
#include <string.h>
#include "rscimage.h"

/* ---- pixmaps ---------------------------------------------------------------- */

static int dims_ok(int w, int h)
{
	return w >= 0 && h >= 0 && w <= RSC_PNM_MAX_DIM && h <= RSC_PNM_MAX_DIM;
}

static void pm_clear(struct rsc_pixmap *pm)
{
	pm->w = 0;
	pm->h = 0;
	pm->planes = 1;
	pm->pix = NULL;
}

int rsc_pixmap_new(struct rsc_pixmap *pm, int w, int h, int planes)
{
	size_t	n;

	if (!pm)
		return RSC_IMG_EARG;
	pm_clear(pm);
	if (planes < 1 || planes > 8 || !dims_ok(w, h))
		return RSC_IMG_EARG;
	n = (size_t)w * (size_t)h;
	if (n > RSC_PIXMAP_MAX_PIXELS)
		return RSC_IMG_ETOOBIG;
	pm->pix = rsc_malloc(n);
	if (!pm->pix)
		return RSC_IMG_ENOMEM;
	memset(pm->pix, 0, n);
	pm->w = w;
	pm->h = h;
	pm->planes = planes;
	return 0;
}

void rsc_pixmap_free(struct rsc_pixmap *pm)
{
	if (!pm)
		return;
	rsc_mfree(pm->pix);
	pm_clear(pm);
}

/* A pixmap that is consistent: sizes, depth, and pix set unless empty. */
static int pm_valid(const struct rsc_pixmap *pm)
{
	return pm && dims_ok(pm->w, pm->h) && pm->planes >= 1 && pm->planes <= 8
	       && (pm->pix || pm->w == 0 || pm->h == 0)
	       && (size_t)pm->w * (size_t)pm->h <= RSC_PIXMAP_MAX_PIXELS;
}

int rsc_pixmap_scale(const struct rsc_pixmap *in, int w, int h, struct rsc_pixmap *out)
{
	int	x, y, err;

	if (!out)
		return RSC_IMG_EARG;
	if (in == out)
		return RSC_IMG_EARG;
	pm_clear(out);
	if (!pm_valid(in) || !dims_ok(w, h))
		return RSC_IMG_EARG;
	if (w > 0 && h > 0 && (in->w == 0 || in->h == 0))
		return RSC_IMG_EARG;
	err = rsc_pixmap_new(out, w, h, in->planes);
	if (err)
		return err;
	for (y = 0; y < h; y++)
	{
		size_t	sy = (size_t)y * (size_t)in->h / (size_t)h;

		for (x = 0; x < w; x++)
		{
			size_t	sx = (size_t)x * (size_t)in->w / (size_t)w;

			out->pix[(size_t)y * (size_t)w + (size_t)x] = in->pix[sy * (size_t)in->w + sx];
		}
	}
	return 0;
}

/* ---- the standard palette ----------------------------------------------------- */

/* The default VDI palette of the pTOS / EmuTOS VDI (vdi/vdi_col.c, the same
 * tables as upstream EmuTOS), in thousandths as the VDI has them:
 *  st_pal   pens 0..15 at 1, 2 and 4 planes (st_palette)
 *  tt_pal   pens 0..15 at 8 planes (tt_palette1)
 *  ext_pal  pens 16..255 (tt_palette2, equal to videl_palette2) */
static const unsigned short st_pal[16][3] =
{
    { 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
    { 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
    { 714, 714, 714 }, { 428, 428, 428 }, { 1000, 428, 428 }, { 428, 1000, 428 },
    { 428, 428, 1000 }, { 428, 1000, 1000 }, { 1000, 1000, 428 }, { 1000, 428, 1000 }
};

static const unsigned short tt_pal[16][3] =
{
    { 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
    { 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
    { 667, 667, 667 }, { 400, 400, 400 }, { 1000, 600, 600 }, { 600, 1000, 600 },
    { 600, 600, 1000 }, { 600, 1000, 1000 }, { 1000, 1000, 600 }, { 1000, 600, 1000 }
};

static const unsigned short ext_pal[240][3] =
{
    { 1000, 1000, 1000 }, { 933, 933, 933 }, { 867, 867, 867 }, { 800, 800, 800 },
    { 733, 733, 733 }, { 667, 667, 667 }, { 600, 600, 600 }, { 533, 533, 533 },
    { 467, 467, 467 }, { 400, 400, 400 }, { 333, 333, 333 }, { 267, 267, 267 },
    { 200, 200, 200 }, { 133, 133, 133 }, { 67, 67, 67 }, { 0, 0, 0 },
    { 1000, 0, 0 }, { 1000, 0, 67 }, { 1000, 0, 133 }, { 1000, 0, 200 },
    { 1000, 0, 267 }, { 1000, 0, 333 }, { 1000, 0, 400 }, { 1000, 0, 467 },
    { 1000, 0, 533 }, { 1000, 0, 600 }, { 1000, 0, 667 }, { 1000, 0, 733 },
    { 1000, 0, 800 }, { 1000, 0, 867 }, { 1000, 0, 933 }, { 1000, 0, 1000 },
    { 933, 0, 1000 }, { 867, 0, 1000 }, { 800, 0, 1000 }, { 733, 0, 1000 },
    { 667, 0, 1000 }, { 600, 0, 1000 }, { 533, 0, 1000 }, { 467, 0, 1000 },
    { 400, 0, 1000 }, { 333, 0, 1000 }, { 267, 0, 1000 }, { 200, 0, 1000 },
    { 133, 0, 1000 }, { 67, 0, 1000 }, { 0, 0, 1000 }, { 0, 67, 1000 },
    { 0, 133, 1000 }, { 0, 200, 1000 }, { 0, 267, 1000 }, { 0, 333, 1000 },
    { 0, 400, 1000 }, { 0, 467, 1000 }, { 0, 533, 1000 }, { 0, 600, 1000 },
    { 0, 667, 1000 }, { 0, 733, 1000 }, { 0, 800, 1000 }, { 0, 867, 1000 },
    { 0, 933, 1000 }, { 0, 1000, 1000 }, { 0, 1000, 933 }, { 0, 1000, 867 },
    { 0, 1000, 800 }, { 0, 1000, 733 }, { 0, 1000, 667 }, { 0, 1000, 600 },
    { 0, 1000, 533 }, { 0, 1000, 467 }, { 0, 1000, 400 }, { 0, 1000, 333 },
    { 0, 1000, 267 }, { 0, 1000, 200 }, { 0, 1000, 133 }, { 0, 1000, 67 },
    { 0, 1000, 0 }, { 67, 1000, 0 }, { 133, 1000, 0 }, { 200, 1000, 0 },
    { 267, 1000, 0 }, { 333, 1000, 0 }, { 400, 1000, 0 }, { 467, 1000, 0 },
    { 533, 1000, 0 }, { 600, 1000, 0 }, { 667, 1000, 0 }, { 733, 1000, 0 },
    { 800, 1000, 0 }, { 867, 1000, 0 }, { 933, 1000, 0 }, { 1000, 1000, 0 },
    { 1000, 933, 0 }, { 1000, 867, 0 }, { 1000, 800, 0 }, { 1000, 733, 0 },
    { 1000, 667, 0 }, { 1000, 600, 0 }, { 1000, 533, 0 }, { 1000, 467, 0 },
    { 1000, 400, 0 }, { 1000, 333, 0 }, { 1000, 267, 0 }, { 1000, 200, 0 },
    { 1000, 133, 0 }, { 1000, 67, 0 }, { 733, 0, 0 }, { 733, 0, 67 },
    { 733, 0, 133 }, { 733, 0, 200 }, { 733, 0, 267 }, { 733, 0, 333 },
    { 733, 0, 400 }, { 733, 0, 467 }, { 733, 0, 533 }, { 733, 0, 600 },
    { 733, 0, 667 }, { 733, 0, 733 }, { 667, 0, 733 }, { 600, 0, 733 },
    { 533, 0, 733 }, { 467, 0, 733 }, { 400, 0, 733 }, { 333, 0, 733 },
    { 267, 0, 733 }, { 200, 0, 733 }, { 133, 0, 733 }, { 67, 0, 733 },
    { 0, 0, 733 }, { 0, 67, 733 }, { 0, 133, 733 }, { 0, 200, 733 },
    { 0, 267, 733 }, { 0, 333, 733 }, { 0, 400, 733 }, { 0, 467, 733 },
    { 0, 533, 733 }, { 0, 600, 733 }, { 0, 667, 733 }, { 0, 733, 733 },
    { 0, 733, 667 }, { 0, 733, 600 }, { 0, 733, 533 }, { 0, 733, 467 },
    { 0, 733, 400 }, { 0, 733, 333 }, { 0, 733, 267 }, { 0, 733, 200 },
    { 0, 733, 133 }, { 0, 733, 67 }, { 0, 733, 0 }, { 67, 733, 0 },
    { 133, 733, 0 }, { 200, 733, 0 }, { 267, 733, 0 }, { 333, 733, 0 },
    { 400, 733, 0 }, { 467, 733, 0 }, { 533, 733, 0 }, { 600, 733, 0 },
    { 667, 733, 0 }, { 733, 733, 0 }, { 733, 667, 0 }, { 733, 600, 0 },
    { 733, 533, 0 }, { 733, 467, 0 }, { 733, 400, 0 }, { 733, 333, 0 },
    { 733, 267, 0 }, { 733, 200, 0 }, { 733, 133, 0 }, { 733, 67, 0 },
    { 467, 0, 0 }, { 467, 0, 67 }, { 467, 0, 133 }, { 467, 0, 200 },
    { 467, 0, 267 }, { 467, 0, 333 }, { 467, 0, 400 }, { 467, 0, 467 },
    { 400, 0, 467 }, { 333, 0, 467 }, { 267, 0, 467 }, { 200, 0, 467 },
    { 133, 0, 467 }, { 67, 0, 467 }, { 0, 0, 467 }, { 0, 67, 467 },
    { 0, 133, 467 }, { 0, 200, 467 }, { 0, 267, 467 }, { 0, 333, 467 },
    { 0, 400, 467 }, { 0, 467, 467 }, { 0, 467, 400 }, { 0, 467, 333 },
    { 0, 467, 267 }, { 0, 467, 200 }, { 0, 467, 133 }, { 0, 467, 67 },
    { 0, 467, 0 }, { 67, 467, 0 }, { 133, 467, 0 }, { 200, 467, 0 },
    { 267, 467, 0 }, { 333, 467, 0 }, { 400, 467, 0 }, { 467, 467, 0 },
    { 467, 400, 0 }, { 467, 333, 0 }, { 467, 267, 0 }, { 467, 200, 0 },
    { 467, 133, 0 }, { 467, 67, 0 }, { 267, 0, 0 }, { 267, 0, 67 },
    { 267, 0, 133 }, { 267, 0, 200 }, { 267, 0, 267 }, { 200, 0, 267 },
    { 133, 0, 267 }, { 67, 0, 267 }, { 0, 0, 267 }, { 0, 67, 267 },
    { 0, 133, 267 }, { 0, 200, 267 }, { 0, 267, 267 }, { 0, 267, 200 },
    { 0, 267, 133 }, { 0, 267, 67 }, { 0, 267, 0 }, { 67, 267, 0 },
    { 133, 267, 0 }, { 200, 267, 0 }, { 267, 267, 0 }, { 267, 200, 0 },
    { 267, 133, 0 }, { 267, 67, 0 }, { 1000, 1000, 1000 }, { 0, 0, 0 }
};

/* The pen of pixel value n: the reverse of the VDI's MAP_COL (pen to
 * hardware register: MAP_COL_ROM = 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12,
 * 14, 11, 13, with pen 1 changed to the last register of the depth, and at
 * 8 planes pens 16 to 254 unchanged and pen 255 to register 15). */
static const unsigned char pen_of_2[4] = { 0, 2, 3, 1 };
static const unsigned char pen_of_4[16] = { 0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1 };
/* 8 planes: registers 0..14 as at 4 planes, register 15 is pen 255,
 * register 255 is pen 1 and registers 16..254 are the pens of the same
 * number. */
static const unsigned char pen_of_8lo[16] = { 0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 255 };

int rsc_depth_supported(int planes)
{
	return planes == 2 || planes == 4 || planes == 8;
}

static int pal_depth(int planes)
{
	return planes == 1 || rsc_depth_supported(planes);
}

static unsigned pen_of(int planes, unsigned pixel)
{
	switch (planes)
	{
	case 1:
		return pixel;
	case 2:
		return pen_of_2[pixel];
	case 4:
		return pen_of_4[pixel];
	default:
		if (pixel < 16)
			return pen_of_8lo[pixel];
		return pixel == 255 ? 1 : pixel;
	}
}

static int to_8bit(unsigned thousandths)
{
	return (int)((thousandths * 255 + 500) / 1000);
}

int rsc_palette_rgb(int planes, unsigned pixel, int *r, int *g, int *b)
{
	const unsigned short	*c;
	unsigned	pen;

	if (!pal_depth(planes))
		return RSC_IMG_ENODEPTH;
	if (pixel >= (1u << planes))
		return RSC_IMG_EARG;
	pen = pen_of(planes, pixel);
	if (pen < 16)
		c = planes == 8 ? tt_pal[pen] : st_pal[pen];
	else
		c = ext_pal[pen - 16];
	if (r)
		*r = to_8bit(c[0]);
	if (g)
		*g = to_8bit(c[1]);
	if (b)
		*b = to_8bit(c[2]);
	return 0;
}

/* The 8-bit colours of the palette of a depth (1, 2, 4 or 8 planes). */
struct pal_table
{
	unsigned char	rgb[256][3];
	unsigned	n;
};

static void pal_table(int planes, struct pal_table *t)
{
	unsigned	p;

	t->n = 1u << planes;
	for (p = 0; p < t->n; p++)
	{
		int	r, g, b;

		rsc_palette_rgb(planes, p, &r, &g, &b);
		t->rgb[p][0] = (unsigned char)r;
		t->rgb[p][1] = (unsigned char)g;
		t->rgb[p][2] = (unsigned char)b;
	}
}

/* The nearest entry of t to r, g, b: the least sum of the squares of the
 * differences, the lowest pixel value among equals. */
static unsigned pal_search(const struct pal_table *t, int r, int g, int b)
{
	unsigned	p, best = 0;
	long	bestd = -1;

	for (p = 0; p < t->n; p++)
	{
		long	dr = (long)t->rgb[p][0] - r, dg = (long)t->rgb[p][1] - g, db = (long)t->rgb[p][2] - b;
		long	d = dr * dr + dg * dg + db * db;

		if (bestd < 0 || d < bestd)
		{
			bestd = d;
			best = p;
			if (d == 0)
				break;
		}
	}
	return best;
}

int rsc_palette_nearest(int planes, int r, int g, int b)
{
	struct pal_table	t;

	if (!pal_depth(planes))
		return RSC_IMG_ENODEPTH;
	if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
		return RSC_IMG_EARG;
	pal_table(planes, &t);
	return (int)pal_search(&t, r, g, b);
}

/* ---- planar words and pixels ----------------------------------------------------- */

/* The word index of the pixel (x, y) in plane p. */
static size_t word_index(int p, int w, int h, int x, int y)
{
	size_t	pw = (size_t)((w + 15) / 16);

	return (size_t)p * pw * (size_t)h + (size_t)y * pw + (size_t)(x >> 4);
}

unsigned rsc_plane_get(const short *data, int planes, int w, int h, int x, int y)
{
	const unsigned short	*d = (const unsigned short *)data;
	unsigned	v = 0;
	int	p;

	if (!d || planes < 1 || planes > 8 || w < 0 || h < 0 || x < 0 || y < 0 || x >= w || y >= h)
		return 0;
	for (p = 0; p < planes; p++)
		v |= (unsigned)((d[word_index(p, w, h, x, y)] >> (15 - (x & 15))) & 1u) << p;
	return v;
}

static void put_pixel(unsigned short *d, int planes, int w, int h, int x, int y, unsigned pixel)
{
	int	p;

	for (p = 0; p < planes; p++)
	{
		unsigned short	*wp = &d[word_index(p, w, h, x, y)];
		unsigned short	bit = (unsigned short)(1u << (15 - (x & 15)));

		if ((pixel >> p) & 1u)
			*wp = (unsigned short)(*wp | bit);
		else
			*wp = (unsigned short)(*wp & ~bit);
	}
}

int rsc_plane_put(short *data, int planes, int w, int h, int x, int y, unsigned pixel)
{
	if (!data || planes < 1 || planes > 8 || w < 0 || h < 0 || x < 0 || y < 0 || x >= w || y >= h)
		return -1;
	put_pixel((unsigned short *)data, planes, w, h, x, y, pixel);
	return 0;
}

int rsc_planes_to_pixmap(const short *data, int planes, int w, int h, struct rsc_pixmap *pm)
{
	int	x, y, err;

	err = rsc_pixmap_new(pm, w, h, planes);
	if (err || !data)
		return err;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			pm->pix[(size_t)y * (size_t)w + (size_t)x] =
				(unsigned char)rsc_plane_get(data, planes, w, h, x, y);
	return 0;
}

int rsc_pixmap_to_planes(const struct rsc_pixmap *pm, short **out)
{
	unsigned short	*blk;
	size_t	bytes;
	unsigned	mask;
	int	x, y;

	if (!out)
		return RSC_IMG_EARG;
	*out = NULL;
	if (!pm_valid(pm))
		return RSC_IMG_EARG;
	bytes = rsc_plane_bytes(pm->w, pm->h) * (size_t)pm->planes;
	blk = rsc_malloc(bytes);
	if (!blk)
		return RSC_IMG_ENOMEM;
	memset(blk, 0, bytes);
	mask = (1u << pm->planes) - 1;
	for (y = 0; y < pm->h; y++)
		for (x = 0; x < pm->w; x++)
			put_pixel(blk, pm->planes, pm->w, pm->h, x, y,
				  pm->pix[(size_t)y * (size_t)pm->w + (size_t)x] & mask);
	*out = (short *)(void *)blk;
	return 0;
}

int rsc_pixmap_convert(const struct rsc_pixmap *in, int planes, struct rsc_pixmap *out, long *inexact)
{
	struct pal_table	from, to;
	long	lost = 0;
	short	map[256];
	size_t	i, n;
	int	err, k;

	if (inexact)
		*inexact = 0;
	if (!out || in == out)
		return RSC_IMG_EARG;
	pm_clear(out);
	if (!pm_valid(in) || planes < 1 || planes > 8)
		return RSC_IMG_EARG;
	if (in->planes != planes && in->planes != 1
	    && (!rsc_depth_supported(in->planes) || !pal_depth(planes)))
		return RSC_IMG_ENODEPTH;
	err = rsc_pixmap_new(out, in->w, in->h, planes);
	if (err)
		return err;
	n = (size_t)in->w * (size_t)in->h;
	if (in->planes == planes)
		memcpy(out->pix, in->pix, n);
	else if (in->planes == 1)
	{
		unsigned char	black = (unsigned char)((1u << planes) - 1);

		for (i = 0; i < n; i++)
			out->pix[i] = (in->pix[i] & 1u) ? black : 0;
	}
	else
	{
		pal_table(in->planes, &from);
		pal_table(planes, &to);
		for (k = 0; k < 256; k++)
			map[k] = -1;
		for (i = 0; i < n; i++)
		{
			unsigned	v = in->pix[i] & ((1u << in->planes) - 1);

			if (map[v] < 0)
			{
				const unsigned char	*c = from.rgb[v];

				k = (int)pal_search(&to, c[0], c[1], c[2]);
				map[v] = (short)(k | ((c[0] != to.rgb[k][0] || c[1] != to.rgb[k][1]
						       || c[2] != to.rgb[k][2]) ? 0x100 : 0));
			}
			out->pix[i] = (unsigned char)(map[v] & 0xff);
			if (map[v] & 0x100)
				lost++;
		}
	}
	if (inexact)
		*inexact = lost;
	return 0;
}

/* ---- writing PNM ------------------------------------------------------------------ */

/* The output is produced twice by the same code, once to count (p NULL) and
 * once to fill the buffer, so the buffer has exactly the size needed. */
struct sink
{
	unsigned char	*p;
	size_t	n;
	int	col;		/* characters on the current line (ascii forms) */
};

#define LINE_MAX_CHARS	70

static void put_byte(struct sink *s, unsigned c)
{
	if (s->p)
		s->p[s->n] = (unsigned char)c;
	s->n++;
}

static void put_text(struct sink *s, const char *t)
{
	while (*t)
		put_byte(s, (unsigned char)*t++);
}

/* The decimal digits of v into buf; returns their number. */
static int dec_digits(char *buf, unsigned v)
{
	char	tmp[12];
	int	n = 0, k;

	do
	{
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	for (k = 0; k < n; k++)
		buf[k] = tmp[n - 1 - k];
	buf[n] = '\0';
	return n;
}

static void put_dec(struct sink *s, unsigned v)
{
	char	buf[12];

	dec_digits(buf, v);
	put_text(s, buf);
}

static void end_line(struct sink *s)
{
	if (s->col > 0)
	{
		put_byte(s, '\n');
		s->col = 0;
	}
}

/* A number of the text forms, separated from the one before by a space,
 * on a new line when the line would get longer than LINE_MAX_CHARS. */
static void put_token(struct sink *s, unsigned v)
{
	char	buf[12];
	int	n = dec_digits(buf, v);

	if (s->col > 0 && s->col + 1 + n > LINE_MAX_CHARS)
		end_line(s);
	if (s->col > 0)
	{
		put_byte(s, ' ');
		s->col++;
	}
	put_text(s, buf);
	s->col += n;
}

static void put_header(struct sink *s, int fmt, const struct rsc_pixmap *pm, int maxval)
{
	put_byte(s, 'P');
	put_byte(s, (unsigned)('0' + fmt));
	put_byte(s, '\n');
	put_dec(s, (unsigned)pm->w);
	put_byte(s, ' ');
	put_dec(s, (unsigned)pm->h);
	put_byte(s, '\n');
	if (maxval)
	{
		put_dec(s, (unsigned)maxval);
		put_byte(s, '\n');
	}
}

static void emit_pbm(struct sink *s, const struct rsc_pixmap *pm, int ascii)
{
	int	x, y;

	put_header(s, ascii ? 1 : 4, pm, 0);
	for (y = 0; y < pm->h; y++)
	{
		const unsigned char	*row = pm->pix + (size_t)y * (size_t)pm->w;

		if (ascii)
		{
			for (x = 0; x < pm->w; x++)
			{
				if (s->col >= LINE_MAX_CHARS)
					end_line(s);
				put_byte(s, (row[x] & 1u) ? '1' : '0');
				s->col++;
			}
			end_line(s);
		}
		else
		{
			for (x = 0; x < pm->w; x += 8)
			{
				unsigned	v = 0;
				int	k;

				for (k = 0; k < 8; k++)
					if (x + k < pm->w && (row[x + k] & 1u))
						v |= 0x80u >> k;
				put_byte(s, v);
			}
		}
	}
}

static void emit_ppm(struct sink *s, const struct rsc_pixmap *pm, int ascii, unsigned char (*rgb)[3])
{
	unsigned	mask = (1u << pm->planes) - 1;
	int	x, y, k;

	put_header(s, ascii ? 3 : 6, pm, 255);
	for (y = 0; y < pm->h; y++)
	{
		const unsigned char	*row = pm->pix + (size_t)y * (size_t)pm->w;

		for (x = 0; x < pm->w; x++)
			for (k = 0; k < 3; k++)
			{
				unsigned	v = rgb[row[x] & mask][k];

				if (ascii)
					put_token(s, v);
				else
					put_byte(s, v);
			}
		if (ascii)
			end_line(s);
	}
}

static int write_pnm(const struct rsc_pixmap *pm, int ascii, int colour, unsigned char **out, size_t *len)
{
	struct pal_table	pt;
	struct sink	s1, s2;

	if (!out || !len)
		return RSC_IMG_EARG;
	*out = NULL;
	*len = 0;
	if (!pm_valid(pm) || pm->w < 1 || pm->h < 1)
		return RSC_IMG_EARG;
	if (colour)
	{
		if (!pal_depth(pm->planes))
			return RSC_IMG_ENODEPTH;
		pal_table(pm->planes, &pt);
	}
	else if (pm->planes != 1)
		return RSC_IMG_EARG;
	s1.p = NULL;
	s1.n = 0;
	s1.col = 0;
	if (colour)
		emit_ppm(&s1, pm, ascii, pt.rgb);
	else
		emit_pbm(&s1, pm, ascii);
	s2.p = rsc_malloc(s1.n);
	if (!s2.p)
		return RSC_IMG_ENOMEM;
	s2.n = 0;
	s2.col = 0;
	if (colour)
		emit_ppm(&s2, pm, ascii, pt.rgb);
	else
		emit_pbm(&s2, pm, ascii);
	*out = s2.p;
	*len = s2.n;
	return 0;
}

int rsc_image_to_pbm(const struct rsc_pixmap *pm, int ascii, unsigned char **out, size_t *len)
{
	return write_pnm(pm, ascii, 0, out, len);
}

int rsc_image_to_ppm(const struct rsc_pixmap *pm, int ascii, unsigned char **out, size_t *len)
{
	return write_pnm(pm, ascii, 1, out, len);
}

/* ---- reading PNM ------------------------------------------------------------------- */

struct rd
{
	const unsigned char	*b;
	size_t	len, pos;
};

static int is_ws(unsigned c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int is_digit(unsigned c)
{
	return c >= '0' && c <= '9';
}

/* Skips whitespace and comments (# up to the end of the line). */
static void skip_sep(struct rd *d)
{
	for (;;)
	{
		while (d->pos < d->len && is_ws(d->b[d->pos]))
			d->pos++;
		if (d->pos < d->len && d->b[d->pos] == '#')
		{
			while (d->pos < d->len && d->b[d->pos] != '\n' && d->b[d->pos] != '\r')
				d->pos++;
		}
		else
			break;
	}
}

#define NUM_SATURATED	0x7fffffffUL

/* A decimal number after separators, of any number of digits in constant
 * space: the value saturates at NUM_SATURATED.  Returns 0, RSC_IMG_ETRUNC
 * at the end of the buffer, RSC_IMG_EFORMAT for a byte that is no digit. */
static int read_num(struct rd *d, unsigned long *val)
{
	unsigned long	v = 0;

	skip_sep(d);
	if (d->pos >= d->len)
		return RSC_IMG_ETRUNC;
	if (!is_digit(d->b[d->pos]))
		return RSC_IMG_EFORMAT;
	while (d->pos < d->len && is_digit(d->b[d->pos]))
	{
		unsigned	dg = (unsigned)(d->b[d->pos] - '0');

		if (v > NUM_SATURATED / 10)
			v = NUM_SATURATED;
		else
		{
			v = v * 10 + dg;
			if (v > NUM_SATURATED)
				v = NUM_SATURATED;
		}
		d->pos++;
	}
	*val = v;
	return 0;
}

void rsc_pnm_free(struct rsc_pnm *p)
{
	if (!p)
		return;
	rsc_mfree(p->samples);
	p->samples = NULL;
	p->format = 0;
	p->w = 0;
	p->h = 0;
	p->maxval = 0;
	p->channels = 0;
}

/* The header up to the first byte of data; fills p (not samples) and sets
 * *nsamp to the number of samples. */
static int pnm_header(struct rd *d, size_t max_pixels, struct rsc_pnm *p, size_t *nsamp)
{
	unsigned long	w, h, mv = 1;
	size_t	npix, avail, need;
	int	fmt, err, text;

	if (d->len < 1)
		return RSC_IMG_ETRUNC;
	if (d->b[0] != 'P')
		return RSC_IMG_EFORMAT;
	if (d->len < 2)
		return RSC_IMG_ETRUNC;
	if (d->b[1] < '1' || d->b[1] > '6')
		return RSC_IMG_EFORMAT;
	fmt = d->b[1] - '0';
	d->pos = 2;
	if (d->len < 3)
		return RSC_IMG_ETRUNC;
	if (!is_ws(d->b[2]) && d->b[2] != '#')
		return RSC_IMG_EFORMAT;
	err = read_num(d, &w);
	if (err)
		return err;
	err = read_num(d, &h);
	if (err)
		return err;
	if (w == 0 || h == 0)
		return RSC_IMG_EFORMAT;
	if (w > RSC_PNM_MAX_DIM || h > RSC_PNM_MAX_DIM)
		return RSC_IMG_ETOOBIG;
	if (fmt != 1 && fmt != 4)
	{
		err = read_num(d, &mv);
		if (err)
			return err;
		if (mv == 0)
			return RSC_IMG_EFORMAT;
		if (mv > 255)
			return RSC_IMG_EMAXVAL;
	}
	if (max_pixels == 0)
		max_pixels = RSC_PNM_DEFAULT_MAX_PIXELS;
	if (max_pixels > RSC_PIXMAP_MAX_PIXELS)
		max_pixels = RSC_PIXMAP_MAX_PIXELS;
	npix = (size_t)w * (size_t)h;
	if (npix > max_pixels)
		return RSC_IMG_ETOOBIG;
	p->format = fmt;
	p->w = (int)w;
	p->h = (int)h;
	p->maxval = (int)mv;
	p->channels = (fmt == 3 || fmt == 6) ? 3 : 1;
	*nsamp = npix * (size_t)p->channels;
	text = fmt <= 3;
	if (!text)
	{
		/* exactly one whitespace byte, then the data; a comment right after the
		 * last number is skipped to the end of its line, which is that byte
		 * (as netpbm reads it) */
		if (d->pos >= d->len)
			return RSC_IMG_ETRUNC;
		if (d->b[d->pos] == '#')
		{
			while (d->pos < d->len && d->b[d->pos] != '\n' && d->b[d->pos] != '\r')
				d->pos++;
			if (d->pos >= d->len)
				return RSC_IMG_ETRUNC;
		}
		else if (!is_ws(d->b[d->pos]))
			return RSC_IMG_EFORMAT;
		d->pos++;
	}
	avail = d->len - d->pos;
	if (fmt == 4)
		need = ((size_t)(w + 7) / 8) * (size_t)h;
	else if (!text || fmt == 1)
		need = *nsamp;		/* a byte per sample, a digit per pixel at least */
	else
		need = 0;
	if (text && fmt != 1)
	{
		/* n numbers take at least 2n - 1 bytes */
		if (*nsamp > (avail + 1) / 2)
			return RSC_IMG_ETRUNC;
	}
	else if (avail < need)
		return RSC_IMG_ETRUNC;
	return 0;
}

int rsc_image_from_pnm(const unsigned char *buf, size_t len, size_t max_pixels, struct rsc_pnm *out)
{
	struct rd	d;
	size_t	nsamp = 0, i;
	int	err;

	if (!out)
		return RSC_IMG_EARG;
	memset(out, 0, sizeof *out);
	if (!buf && len)
		return RSC_IMG_EARG;
	d.b = buf;
	d.len = len;
	d.pos = 0;
	err = pnm_header(&d, max_pixels, out, &nsamp);
	if (err)
	{
		memset(out, 0, sizeof *out);
		return err;
	}
	out->samples = rsc_malloc(nsamp);
	if (!out->samples)
	{
		memset(out, 0, sizeof *out);
		return RSC_IMG_ENOMEM;
	}
	err = 0;
	switch (out->format)
	{
	case 4:
	{
		size_t	rb = ((size_t)out->w + 7) / 8;
		int	x, y;

		for (y = 0; y < out->h; y++)
			for (x = 0; x < out->w; x++)
				out->samples[(size_t)y * (size_t)out->w + (size_t)x] =
					(d.b[d.pos + (size_t)y * rb + (size_t)(x >> 3)] >> (7 - (x & 7))) & 1u;
		break;
	}
	case 5:
	case 6:
		for (i = 0; i < nsamp; i++)
		{
			unsigned	v = d.b[d.pos + i];

			if ((int)v > out->maxval)
			{
				err = RSC_IMG_EFORMAT;
				break;
			}
			out->samples[i] = (unsigned char)v;
		}
		break;
	case 1:
		for (i = 0; i < nsamp && !err; i++)
		{
			skip_sep(&d);
			if (d.pos >= d.len)
				err = RSC_IMG_ETRUNC;
			else if (d.b[d.pos] != '0' && d.b[d.pos] != '1')
				err = RSC_IMG_EFORMAT;
			else
				out->samples[i] = (unsigned char)(d.b[d.pos++] - '0');
		}
		break;
	default:	/* 2 and 3 */
		for (i = 0; i < nsamp && !err; i++)
		{
			unsigned long	v;

			err = read_num(&d, &v);
			if (!err && v > (unsigned long)out->maxval)
				err = RSC_IMG_EFORMAT;
			if (!err)
				out->samples[i] = (unsigned char)v;
		}
		break;
	}
	if (err)
	{
		rsc_mfree(out->samples);
		memset(out, 0, sizeof *out);
		return err;
	}
	return 0;
}

static int scale_sample(int v, int maxval)
{
	return (v * 255 + maxval / 2) / maxval;
}

/* The colour of sample i (pixel number) of a decoded file. */
static void pnm_rgb_at(const struct rsc_pnm *p, size_t i, int *r, int *g, int *b)
{
	const unsigned char	*s = p->samples;

	if (p->format == 1 || p->format == 4)
		*r = *g = *b = s[i] ? 0 : 255;
	else if (p->channels == 1)
		*r = *g = *b = scale_sample(s[i], p->maxval);
	else
	{
		*r = scale_sample(s[3 * i], p->maxval);
		*g = scale_sample(s[3 * i + 1], p->maxval);
		*b = scale_sample(s[3 * i + 2], p->maxval);
	}
}

int rsc_pnm_rgb(const struct rsc_pnm *p, int x, int y, int *r, int *g, int *b)
{
	int	rr, gg, bb;

	if (!p || !p->samples || x < 0 || y < 0 || x >= p->w || y >= p->h)
		return RSC_IMG_EARG;
	pnm_rgb_at(p, (size_t)y * (size_t)p->w + (size_t)x, &rr, &gg, &bb);
	if (r)
		*r = rr;
	if (g)
		*g = gg;
	if (b)
		*b = bb;
	return 0;
}

int rsc_pnm_to_pixmap(const struct rsc_pnm *p, int planes, int threshold, int exact,
		      struct rsc_pixmap *out, long *bad)
{
	struct pal_table	pt;
	size_t	i, n;
	int	err, last_r = -1, last_g = -1, last_b = -1, last_v = 0;

	if (bad)
		*bad = -1;
	if (!out)
		return RSC_IMG_EARG;
	pm_clear(out);
	if (!p || !p->samples || p->w < 1 || p->h < 1 || planes < 1 || planes > 8
	    || threshold < 0 || threshold > 100)
		return RSC_IMG_EARG;
	if (!pal_depth(planes))
		return RSC_IMG_ENODEPTH;
	err = rsc_pixmap_new(out, p->w, p->h, planes);
	if (err)
		return err;
	if (threshold == 0)
		threshold = 50;
	pal_table(planes, &pt);
	n = (size_t)p->w * (size_t)p->h;
	for (i = 0; i < n; i++)
	{
		int	r, g, b, v;

		pnm_rgb_at(p, i, &r, &g, &b);
		if (planes == 1)
		{
			int	lum = (299 * r + 587 * g + 114 * b) / 1000;

			out->pix[i] = (unsigned char)(((255 - lum) * 100 >= threshold * 255) ? 1 : 0);
			continue;
		}
		if (r != last_r || g != last_g || b != last_b)
		{
			last_r = r;
			last_g = g;
			last_b = b;
			last_v = (int)pal_search(&pt, r, g, b);
		}
		v = last_v;
		if (exact)
		{
			if (pt.rgb[v][0] != r || pt.rgb[v][1] != g || pt.rgb[v][2] != b)
			{
				if (bad)
					*bad = (long)i;
				rsc_pixmap_free(out);
				return RSC_IMG_ENOTPAL;
			}
		}
		out->pix[i] = (unsigned char)v;
	}
	return 0;
}

/* ---- the images of a model ----------------------------------------------------------- */

/* Where an image lives and how it is laid out.  tp is the slot of the data
 * pointer of the selected image (the data or the mask), the pointer the
 * model owns; dp and mp the slots of the data and of the mask (mp NULL:
 * no mask); w and h the size of the icon, planes the depth of the data.
 * bytewise: a BITBLK, whose rows are wb bytes (not necessarily an even
 * number) of the file byte stream. */
struct loc
{
	struct rsc_cicon	*c;
	struct rsc_cicon_version	*v;
	short	**dp, **mp, **tp;
	int	w, h;
	int	planes;		/* of the data */
	int	tplanes;	/* of the selected image */
	size_t	pb;		/* bytes of one icon plane */
	size_t	dbytes;		/* bytes of the data block */
	size_t	tbytes;		/* bytes of the selected block */
	int	bytewise;
	int	is_new;		/* RSC_VER_NEW */
	int	sel_absent;	/* a selected image to be created */
	int	is_mask;
	int	wb;		/* bytewise: bytes per row */
};

static void loc_icon(struct loc *L, ICONBLK *ib, size_t bytes)
{
	L->w = ib->ib_wicon;
	L->h = ib->ib_hicon;
	L->planes = 1;
	L->pb = rsc_plane_bytes(L->w, L->h);
	L->dbytes = bytes;
	L->dp = &ib->ib_pdata;
	L->mp = &ib->ib_pmask;
}

/* Resolves s (see rscimage.h).  dest: for a destination, which may be a
 * version to create. */
static int resolve(const struct rsc *r, const struct rsc_imgsel *s, int dest, struct loc *L)
{
	BITBLK	*bb = NULL;
	ICONBLK	*ib = NULL;
	struct rsc_cicon	*c = NULL;
	OBJECT	*objs;
	int	n;

	memset(L, 0, sizeof *L);
	if (!r || !s || s->version < RSC_VER_NEW)
		return RSC_IMG_EARG;
	if (s->tree == RSC_IMG_FREE)
	{
		bb = rsc_frimg(r, s->obj);
		if (!bb)
			return RSC_IMG_EARG;
	}
	else
	{
		OBJECT	*o;

		objs = rsc_tree_objects(r, s->tree, &n);
		if (!objs || s->obj < 0 || s->obj >= n)
			return RSC_IMG_EARG;
		o = &objs[s->obj];
		switch (rsc_obj_spec_kind(o))
		{
		case RSC_SPEC_ICONBLK:
			ib = o->ob_spec.iconblk;
			break;
		case RSC_SPEC_BITBLK:
			bb = o->ob_spec.bitblk;
			break;
		case RSC_SPEC_CICON:
			c = rsc_obj_cicon(r, s->tree, s->obj);
			break;
		default:
			return RSC_IMG_ENOIMAGE;
		}
		if (!ib && !bb && !c)
			return RSC_IMG_ENOIMAGE;
	}
	if (ib || bb || (c && s->version == RSC_VER_MONO))
	{
		if (s->version != RSC_VER_MONO || s->sel)
			return RSC_IMG_ENOIMAGE;
		if (bb)
		{
			if (s->mask)
				return RSC_IMG_ENOMASK;
			L->w = bb->bi_wb < 0 ? 0 : bb->bi_wb * 8;
			L->h = bb->bi_hl;
			L->wb = bb->bi_wb;
			L->planes = 1;
			L->dbytes = rsc_bitblk_bytes(bb);
			L->pb = L->dbytes;
			L->dp = &bb->bi_pdata;
			L->bytewise = 1;
		}
		else if (ib)
			loc_icon(L, ib, rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon));
		else
		{
			ib = &c->blk.monoblk;
			loc_icon(L, ib, rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon));
		}
		L->c = c;
	}
	else
	{
		ICONBLK	*mono = &c->blk.monoblk;

		L->c = c;
		L->w = mono->ib_wicon;
		L->h = mono->ib_hicon;
		L->pb = rsc_plane_bytes(L->w, L->h);
		if (s->version == RSC_VER_NEW)
		{
			if (!dest || s->sel)
				return RSC_IMG_ENOIMAGE;
			if (!rsc_depth_supported(s->planes))
				return RSC_IMG_ENODEPTH;
			if (rsc_cicon_find_version(c, s->planes) >= 0)
				return RSC_IMG_EEXIST;
			if (rsc_cicon_nversions(c) >= RSC_MAX_VERSIONS)
				return RSC_IMG_ELIMIT;
			L->is_new = 1;
			L->planes = s->planes;
			L->dbytes = L->pb * (size_t)s->planes;
			L->mp = &mono->ib_pmask;		/* the mask it copies */
		}
		else
		{
			L->v = rsc_cicon_version(c, s->version);
			if (!L->v)
				return RSC_IMG_ENOIMAGE;
			L->planes = L->v->c.num_planes;
			L->dbytes = L->pb * (size_t)L->planes;
			if (s->sel)
			{
				L->dp = &L->v->c.sel_data;
				L->mp = &L->v->c.sel_mask;
				L->sel_absent = !L->v->c.sel_data && !L->v->c.sel_mask;
				if (L->sel_absent && !dest)
					return RSC_IMG_ENOIMAGE;
			}
			else
			{
				L->dp = &L->v->c.col_data;
				L->mp = &L->v->c.col_mask;
			}
		}
	}
	L->is_mask = s->mask != 0;
	if (L->is_mask)
	{
		L->tp = L->mp;
		L->tplanes = 1;
		L->tbytes = L->pb;
	}
	else
	{
		L->tp = L->dp;
		L->tplanes = L->planes;
		L->tbytes = L->dbytes;
	}
	return 0;
}

int rsc_image_info(const struct rsc *r, const struct rsc_imgsel *s, struct rsc_imginfo *info)
{
	struct loc	L;
	int	err;

	if (!info)
		return RSC_IMG_EARG;
	memset(info, 0, sizeof *info);
	err = resolve(r, s, 0, &L);
	if (err)
		return err;
	if (!L.tp)
		return RSC_IMG_ENOMASK;
	info->w = L.w;
	info->h = L.h;
	info->planes = L.tplanes;
	info->blank = *L.tp == NULL;
	return 0;
}

/* ---- the bytes of a BITBLK -------------------------------------------------------- */

/* Byte b of the file byte stream of a block of nbytes bytes held as words
 * of the machine (an odd last byte is kept as it is, rscatari_read.c). */
static unsigned stream_get(const short *d, size_t nbytes, size_t b)
{
	unsigned short	w;

	if ((nbytes & 1) && b + 1 == nbytes)
		return ((const unsigned char *)d)[b];
	memcpy(&w, (const unsigned char *)d + (b & ~(size_t)1), 2);
	return (b & 1) ? (w & 0xffu) : (unsigned)(w >> 8);
}

static void stream_or(short *d, size_t nbytes, size_t b, unsigned bits)
{
	unsigned short	w;

	if ((nbytes & 1) && b + 1 == nbytes)
	{
		((unsigned char *)d)[b] = (unsigned char)(((unsigned char *)d)[b] | bits);
		return;
	}
	memcpy(&w, (unsigned char *)d + (b & ~(size_t)1), 2);
	w = (unsigned short)(w | ((b & 1) ? bits : (bits << 8)));
	memcpy((unsigned char *)d + (b & ~(size_t)1), &w, 2);
}

/* The selected image of L as a pixmap. */
static int load_pixmap(const struct loc *L, struct rsc_pixmap *pm)
{
	int	err;

	if (!L->tp)
		return RSC_IMG_ENOMASK;
	if (!L->is_mask && L->tplanes > 1 && !rsc_depth_supported(L->tplanes))
		return RSC_IMG_ENODEPTH;
	if (!L->bytewise)
		return rsc_planes_to_pixmap(*L->tp, L->tplanes, L->w, L->h, pm);
	err = rsc_pixmap_new(pm, L->w, L->h, 1);
	if (err || !*L->tp)
		return err;
	{
		int	x, y;

		for (y = 0; y < L->h; y++)
			for (x = 0; x < L->w; x++)
			{
				size_t	b = (size_t)y * (size_t)L->wb + (size_t)(x >> 3);

				pm->pix[(size_t)y * (size_t)L->w + (size_t)x] =
					(unsigned char)((stream_get(*L->tp, L->dbytes, b) >> (7 - (x & 7))) & 1u);
			}
	}
	return 0;
}

/* A new block of the selected image of L holding pm, which has been
 * checked to fit. */
static int build_block(const struct loc *L, const struct rsc_pixmap *pm, short **out)
{
	unsigned char	*blk;
	int	x, y;

	if (!L->bytewise)
		return rsc_pixmap_to_planes(pm, out);
	*out = NULL;
	blk = rsc_malloc(L->dbytes);
	if (!blk)
		return RSC_IMG_ENOMEM;
	memset(blk, 0, L->dbytes);
	for (y = 0; y < L->h; y++)
		for (x = 0; x < L->w; x++)
			if (pm->pix[(size_t)y * (size_t)L->w + (size_t)x] & 1u)
				stream_or((short *)(void *)blk, L->dbytes,
					  (size_t)y * (size_t)L->wb + (size_t)(x >> 3), 0x80u >> (x & 7));
	*out = (short *)(void *)blk;
	return 0;
}

/* A copy of the n bytes at src (NULL: zeros) in *out. */
static int dup_block(const short *src, size_t n, short **out)
{
	void	*p = rsc_malloc(n);

	*out = NULL;
	if (!p)
		return RSC_IMG_ENOMEM;
	if (src)
		memcpy(p, src, n);
	else
		memset(p, 0, n);
	*out = p;
	return 0;
}

/* Makes the new blocks nd (data) and nm (mask), either may be NULL for
 * "unchanged", the images of L; takes them over.  Selected images that
 * did not exist are created, the half not given a copy of the normal one.
 * Returns 0 or RSC_IMG_ENOMEM (then the blocks are freed). */
static int commit(struct loc *L, short *nd, short *nm)
{
	if (L->sel_absent)
	{
		if (!nd && dup_block(L->v->c.col_data, L->dbytes, &nd))
		{
			rsc_mfree(nm);
			return RSC_IMG_ENOMEM;
		}
		if (!nm && dup_block(L->v->c.col_mask, L->pb, &nm))
		{
			rsc_mfree(nd);
			return RSC_IMG_ENOMEM;
		}
	}
	if (nd)
	{
		rsc_mfree(*L->dp);
		*L->dp = nd;
	}
	if (nm)
	{
		rsc_mfree(*L->mp);
		*L->mp = nm;
	}
	return 0;
}

/* Appends the new version of L->c with the given data and mask blocks
 * (copied); both are freed. */
static int commit_new(struct loc *L, short *nd, short *nm)
{
	int	k = rsc_cicon_add_version(L->c, L->planes, nd, nm, NULL, NULL);

	rsc_mfree(nd);
	rsc_mfree(nm);
	return k < 0 ? RSC_IMG_ENOMEM : 0;
}

int rsc_image_get(const struct rsc *r, const struct rsc_imgsel *s, struct rsc_pixmap *out)
{
	struct loc	L;
	int	err;

	if (!out)
		return RSC_IMG_EARG;
	memset(out, 0, sizeof *out);
	out->planes = 1;
	err = resolve(r, s, 0, &L);
	if (err)
		return err;
	return load_pixmap(&L, out);
}

/* The checks of a pixmap about to become the selected image of L. */
static int check_fit(const struct loc *L, const struct rsc_pixmap *pm)
{
	if (!pm_valid(pm))
		return RSC_IMG_EARG;
	if (pm->w != L->w || pm->h != L->h)
		return RSC_IMG_ESIZE;
	if (pm->planes != L->tplanes)
		return RSC_IMG_EARG;
	if (!L->is_mask && L->tplanes > 1 && !rsc_depth_supported(L->tplanes))
		return RSC_IMG_ENODEPTH;
	return 0;
}

/* The block for the other half of a version about to be created: the
 * data (zeros) or the mask (the mono mask). */
static int new_version_blocks(const struct loc *L, short *given, short **nd, short **nm)
{
	int	err;

	if (L->is_mask)
	{
		err = dup_block(NULL, L->dbytes, nd);
		if (!err)
			*nm = given;
	}
	else
	{
		err = dup_block(*L->mp, L->pb, nm);
		if (!err)
			*nd = given;
	}
	return err;
}

int rsc_image_set(struct rsc *r, const struct rsc_imgsel *s, const struct rsc_pixmap *in)
{
	struct loc	L;
	short	*blk = NULL, *od = NULL, *om = NULL;
	int	err;

	err = resolve(r, s, 1, &L);
	if (err)
		return err;
	if (L.is_new)
	{
		/* a new version: the destination is the data (or the mask) of
		 * something that does not exist yet */
		L.tplanes = L.is_mask ? 1 : L.planes;
	}
	else if (!L.tp)
		return RSC_IMG_ENOMASK;
	err = check_fit(&L, in);
	if (err)
		return err;
	err = build_block(&L, in, &blk);
	if (err)
		return err;
	if (L.is_new)
	{
		err = new_version_blocks(&L, blk, &od, &om);
		if (err)
		{
			rsc_mfree(blk);
			return err;
		}
		return commit_new(&L, od, om);
	}
	return L.is_mask ? commit(&L, NULL, blk) : commit(&L, blk, NULL);
}

int rsc_image_copy(struct rsc *r, const struct rsc_imgsel *from, const struct rsc_imgsel *to,
		   int flags, long *inexact)
{
	struct loc	LF, LT;
	struct rsc_pixmap	src, cv, msrc;
	short	*nd = NULL, *nm = NULL, *od = NULL, *om = NULL;
	int	err, want_mask;

	if (inexact)
		*inexact = 0;
	memset(&src, 0, sizeof src);
	memset(&cv, 0, sizeof cv);
	memset(&msrc, 0, sizeof msrc);
	if (!from || !to || (flags & ~RSC_COPY_MASK))
		return RSC_IMG_EARG;
	if ((flags & RSC_COPY_MASK) && (from->mask || to->mask))
		return RSC_IMG_EARG;
	err = resolve(r, from, 0, &LF);
	if (err)
		return err;
	err = resolve(r, to, 1, &LT);
	if (err)
		return err;
	if (LT.is_new)
		LT.tplanes = LT.is_mask ? 1 : LT.planes;
	else if (!LT.tp)
		return RSC_IMG_ENOMASK;
	if (!LT.is_new && LF.tp == LT.tp)
		return RSC_IMG_EARG;
	if (LF.w != LT.w || LF.h != LT.h)
		return RSC_IMG_ESIZE;
	if (!LT.is_mask && LT.tplanes > 1 && !rsc_depth_supported(LT.tplanes))
		return RSC_IMG_ENODEPTH;
	err = load_pixmap(&LF, &src);
	if (err)
		return err;
	err = rsc_pixmap_convert(&src, LT.tplanes, &cv, inexact);
	if (err)
		goto done;
	err = build_block(&LT, &cv, &nd);
	if (err)
		goto done;
	want_mask = (flags & RSC_COPY_MASK) && LF.mp && *LF.mp && LT.mp;
	if (want_mask)
	{
		/* the mask of `from`, whatever the depth, into the mask of `to` */
		struct loc	Lm = LF;

		Lm.is_mask = 1;
		Lm.tp = LF.mp;
		Lm.tplanes = 1;
		Lm.tbytes = LF.pb;
		err = load_pixmap(&Lm, &msrc);
		if (err)
			goto done;
		err = rsc_pixmap_to_planes(&msrc, &nm);
		if (err)
			goto done;
	}
	if (LT.is_new)
	{
		/* data := the converted image, mask := the copied mask or the
		 * mono mask */
		if (LT.is_mask)
		{
			om = nd;
			nd = NULL;
			err = dup_block(NULL, LT.dbytes, &od);
		}
		else
		{
			od = nd;
			nd = NULL;
			if (nm)
			{
				om = nm;
				nm = NULL;
			}
			else
				err = dup_block(*LT.mp, LT.pb, &om);
		}
		if (!err)
		{
			err = commit_new(&LT, od, om);
			od = om = NULL;
		}
		goto done;
	}
	if (LT.is_mask)
	{
		err = commit(&LT, NULL, nd);
		nd = NULL;
	}
	else
	{
		err = commit(&LT, nd, nm);
		nd = nm = NULL;
	}
done:
	rsc_pixmap_free(&src);
	rsc_pixmap_free(&cv);
	rsc_pixmap_free(&msrc);
	rsc_mfree(nd);
	rsc_mfree(nm);
	rsc_mfree(od);
	rsc_mfree(om);
	return err;
}

/* ---- the versions of a colour icon ----------------------------------------------------- */

int rsc_cicon_find_version(const struct rsc_cicon *c, int planes)
{
	const CICON	*p;
	int	k = 0;

	if (!c)
		return -1;
	for (p = c->blk.mainlist; p; p = p->next_res, k++)
		if (p->num_planes == planes)
			return k;
	return -1;
}

int rsc_cicon_new_version(struct rsc_cicon *c, int planes)
{
	const ICONBLK	*mono;
	short	*nd, *nm;
	size_t	pb;
	int	k, err;

	if (!c)
		return RSC_IMG_EARG;
	if (!rsc_depth_supported(planes))
		return RSC_IMG_ENODEPTH;
	if (rsc_cicon_find_version(c, planes) >= 0)
		return RSC_IMG_EEXIST;
	if (rsc_cicon_nversions(c) >= RSC_MAX_VERSIONS)
		return RSC_IMG_ELIMIT;
	mono = &c->blk.monoblk;
	pb = rsc_plane_bytes(mono->ib_wicon, mono->ib_hicon);
	err = dup_block(NULL, pb * (size_t)planes, &nd);
	if (err)
		return err;
	err = dup_block(mono->ib_pmask, pb, &nm);
	if (err)
	{
		rsc_mfree(nd);
		return err;
	}
	k = rsc_cicon_add_version(c, planes, nd, nm, NULL, NULL);
	rsc_mfree(nd);
	rsc_mfree(nm);
	return k < 0 ? RSC_IMG_ENOMEM : k;
}

int rsc_cicon_add_selected(struct rsc_cicon *c, int v)
{
	struct rsc_cicon_version	*cv;
	short	*nd, *nm;
	size_t	pb;
	int	err;

	if (!c || v < 0)
		return RSC_IMG_ENOIMAGE;
	cv = rsc_cicon_version(c, v);
	if (!cv)
		return RSC_IMG_ENOIMAGE;
	if (cv->c.sel_data || cv->c.sel_mask)
		return RSC_IMG_EEXIST;
	pb = rsc_plane_bytes(c->blk.monoblk.ib_wicon, c->blk.monoblk.ib_hicon);
	err = dup_block(cv->c.col_data, pb * (size_t)cv->c.num_planes, &nd);
	if (err)
		return err;
	err = dup_block(cv->c.col_mask, pb, &nm);
	if (err)
	{
		rsc_mfree(nd);
		return err;
	}
	cv->c.sel_data = nd;
	cv->c.sel_mask = nm;
	return 0;
}

int rsc_cicon_drop_selected(struct rsc_cicon *c, int v)
{
	struct rsc_cicon_version	*cv;

	if (!c || v < 0)
		return RSC_IMG_ENOIMAGE;
	cv = rsc_cicon_version(c, v);
	if (!cv || (!cv->c.sel_data && !cv->c.sel_mask))
		return RSC_IMG_ENOIMAGE;
	rsc_mfree(cv->c.sel_data);
	rsc_mfree(cv->c.sel_mask);
	cv->c.sel_data = NULL;
	cv->c.sel_mask = NULL;
	return 0;
}
