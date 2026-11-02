/* TEST_IMAGE.C	host tests of the image calls of the resource library
 *		(src/rsc/rscimage.h): the palette, planar words and pixels,
 *		PBM/PGM/PPM files (writer, parser, hostile input), depth
 *		conversion, and the images of the objects of a model;
 *		make rsc-test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rscmodel.h"
#include "rscfile.h"
#include "rscimage.h"

/* ---- a tiny assertion framework -------------------------------------------- */

static long checks, failures;

#define CHECK(c)							\
	do								\
	{								\
		checks++;						\
		if (!(c))						\
		{							\
			failures++;					\
			fprintf(stderr, "%s:%d: CHECK failed: %s\n",	\
				__FILE__, __LINE__, #c);		\
		}							\
	} while (0)

static long live(void)
{
	return rsc_test_live_blocks();
}

/* A fixed generator: one draw per statement. */
static uint32_t rng_s = 12345;

static unsigned rnd(void)
{
	rng_s ^= rng_s << 13;
	rng_s ^= rng_s >> 17;
	rng_s ^= rng_s << 5;
	return rng_s;
}

static void words(short *d, int n, unsigned seed)
{
	unsigned	h = seed * 2654435761u + 1;
	int	i;

	for (i = 0; i < n; i++)
	{
		h = h * 1103515245u + 12345u;
		d[i] = (short)(h >> 8);
	}
}

/* Clears the bits beyond the width, so that a block is what an image of
 * the model written pixel by pixel would be. */
static void norm(short *d, int planes, int w, int h)
{
	struct rsc_pixmap	pm;
	short	*e;

	CHECK(rsc_planes_to_pixmap(d, planes, w, h, &pm) == 0);
	CHECK(rsc_pixmap_to_planes(&pm, &e) == 0);
	memcpy(d, e, rsc_plane_bytes(w, h) * (size_t)planes);
	rsc_mfree(e);
	rsc_pixmap_free(&pm);
}

static void rand_pm(struct rsc_pixmap *pm, int w, int h, int planes)
{
	unsigned	mask = (1u << planes) - 1;
	size_t	i;
	unsigned	v;

	CHECK(rsc_pixmap_new(pm, w, h, planes) == 0);
	for (i = 0; i < (size_t)w * (size_t)h; i++)
	{
		v = rnd();
		pm->pix[i] = (unsigned char)(v & mask);
	}
}

static int pm_equal(const struct rsc_pixmap *a, const struct rsc_pixmap *b)
{
	return a->w == b->w && a->h == b->h && a->planes == b->planes
	       && (a->w == 0 || a->h == 0 || memcmp(a->pix, b->pix, (size_t)a->w * (size_t)a->h) == 0);
}

/* ---- the palette ------------------------------------------------------------ */

static void rgb_is(int planes, unsigned pixel, int r, int g, int b)
{
	int	rr = -1, gg = -1, bb = -1;
	int	e = rsc_palette_rgb(planes, pixel, &rr, &gg, &bb);

	CHECK(e == 0);
	if (rr != r || gg != g || bb != b)
		fprintf(stderr, "planes %d pixel %u: %d %d %d, expected %d %d %d\n", planes, pixel, rr, gg, bb,
			r, g, b);
	CHECK(rr == r && gg == g && bb == b);
}

static int naive_nearest(int planes, int r, int g, int b)
{
	long	best = -1;
	int	bp = 0, p, pr, pg, pb;

	for (p = 0; p < (1 << planes); p++)
	{
		long	d;

		rsc_palette_rgb(planes, (unsigned)p, &pr, &pg, &pb);
		d = (long)(pr - r) * (pr - r);
		d += (long)(pg - g) * (pg - g);
		d += (long)(pb - b) * (pb - b);
		if (best < 0 || d < best)
		{
			best = d;
			bp = p;
		}
	}
	return bp;
}

static void test_palette(void)
{
	int	pl, r, g, b, k;
	unsigned	p;

	CHECK(rsc_depth_supported(2) && rsc_depth_supported(4) && rsc_depth_supported(8));
	CHECK(!rsc_depth_supported(1) && !rsc_depth_supported(3) && !rsc_depth_supported(5)
	      && !rsc_depth_supported(6) && !rsc_depth_supported(7) && !rsc_depth_supported(16)
	      && !rsc_depth_supported(0) && !rsc_depth_supported(-4));

	/* 1 plane: white and black */
	rgb_is(1, 0, 255, 255, 255);
	rgb_is(1, 1, 0, 0, 0);
	/* 2 planes: pens 0, 2, 3, 1 */
	rgb_is(2, 0, 255, 255, 255);
	rgb_is(2, 1, 255, 0, 0);
	rgb_is(2, 2, 0, 255, 0);
	rgb_is(2, 3, 0, 0, 0);
	/* 4 planes: pens 0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1 */
	rgb_is(4, 0, 255, 255, 255);
	rgb_is(4, 1, 255, 0, 0);
	rgb_is(4, 2, 0, 255, 0);
	rgb_is(4, 3, 255, 255, 0);
	rgb_is(4, 4, 0, 0, 255);
	rgb_is(4, 5, 255, 0, 255);
	rgb_is(4, 6, 0, 255, 255);
	rgb_is(4, 7, 182, 182, 182);
	rgb_is(4, 8, 109, 109, 109);
	rgb_is(4, 9, 255, 109, 109);
	rgb_is(4, 10, 109, 255, 109);
	rgb_is(4, 11, 255, 255, 109);
	rgb_is(4, 12, 109, 109, 255);
	rgb_is(4, 13, 255, 109, 255);
	rgb_is(4, 14, 109, 255, 255);
	rgb_is(4, 15, 0, 0, 0);
	/* 8 planes: pens 0..15 of the TT palette, 16..254 the extension */
	rgb_is(8, 0, 255, 255, 255);
	rgb_is(8, 1, 255, 0, 0);
	rgb_is(8, 3, 255, 255, 0);
	rgb_is(8, 7, 170, 170, 170);
	rgb_is(8, 8, 102, 102, 102);
	rgb_is(8, 9, 255, 153, 153);
	rgb_is(8, 14, 153, 255, 255);
	rgb_is(8, 15, 0, 0, 0);		/* pen 255 */
	rgb_is(8, 16, 255, 255, 255);	/* pen 16 */
	rgb_is(8, 17, 238, 238, 238);
	rgb_is(8, 31, 0, 0, 0);
	rgb_is(8, 32, 255, 0, 0);
	rgb_is(8, 33, 255, 0, 17);
	rgb_is(8, 116, 255, 102, 0);
	rgb_is(8, 252, 68, 34, 0);
	rgb_is(8, 253, 68, 17, 0);
	rgb_is(8, 254, 255, 255, 255);	/* pen 254 */
	rgb_is(8, 255, 0, 0, 0);	/* pen 1 */

	/* errors */
	CHECK(rsc_palette_rgb(3, 0, &r, &g, &b) == RSC_IMG_ENODEPTH);
	CHECK(rsc_palette_rgb(0, 0, &r, &g, &b) == RSC_IMG_ENODEPTH);
	CHECK(rsc_palette_rgb(9, 0, &r, &g, &b) == RSC_IMG_ENODEPTH);
	CHECK(rsc_palette_rgb(16, 0, &r, &g, &b) == RSC_IMG_ENODEPTH);
	CHECK(rsc_palette_rgb(2, 4, &r, &g, &b) == RSC_IMG_EARG);
	CHECK(rsc_palette_rgb(1, 2, &r, &g, &b) == RSC_IMG_EARG);
	CHECK(rsc_palette_rgb(8, 256, &r, &g, &b) == RSC_IMG_EARG);
	CHECK(rsc_palette_rgb(4, 16, NULL, NULL, NULL) == RSC_IMG_EARG);
	CHECK(rsc_palette_rgb(4, 15, NULL, NULL, NULL) == 0);
	CHECK(rsc_palette_nearest(3, 0, 0, 0) == RSC_IMG_ENODEPTH);
	CHECK(rsc_palette_nearest(4, -1, 0, 0) == RSC_IMG_EARG);
	CHECK(rsc_palette_nearest(4, 0, 256, 0) == RSC_IMG_EARG);
	CHECK(rsc_palette_nearest(4, 0, 0, -5) == RSC_IMG_EARG);

	/* every entry is its own nearest, unless an earlier one has the same
	 * colour; and the colour of the nearest is the colour */
	for (pl = 1; pl <= 8; pl++)
	{
		if (pl != 1 && !rsc_depth_supported(pl))
			continue;
		for (p = 0; p < (1u << pl); p++)
		{
			int	n;

			rsc_palette_rgb(pl, p, &r, &g, &b);
			n = rsc_palette_nearest(pl, r, g, b);
			CHECK(n >= 0 && (unsigned)n <= p);
			CHECK(n == naive_nearest(pl, r, g, b));
			rsc_palette_rgb(pl, (unsigned)n, &k, &k, &k);
		}
	}
	/* the two lowest entries of equal colour: white is pixel 0 */
	CHECK(rsc_palette_nearest(8, 255, 255, 255) == 0);
	CHECK(rsc_palette_nearest(8, 0, 0, 0) == 15);
	CHECK(rsc_palette_nearest(4, 0, 0, 0) == 15);
	CHECK(rsc_palette_nearest(2, 0, 0, 0) == 3);
	CHECK(rsc_palette_nearest(1, 10, 10, 10) == 1);
	CHECK(rsc_palette_nearest(1, 200, 200, 200) == 0);
	CHECK(rsc_palette_nearest(4, 200, 200, 200) == 7);
	CHECK(rsc_palette_nearest(4, 250, 10, 10) == 1);

	/* random colours against the naive search */
	for (k = 0; k < 3000; k++)
	{
		static const int depths[4] = { 1, 2, 4, 8 };
		int	d = depths[k & 3];

		r = (int)(rnd() & 255);
		g = (int)(rnd() & 255);
		b = (int)(rnd() & 255);
		CHECK(rsc_palette_nearest(d, r, g, b) == naive_nearest(d, r, g, b));
	}
}

/* ---- planar words and pixels --------------------------------------------------- */

static void test_planes_layout(void)
{
	short	d[8];
	struct rsc_pixmap	pm;
	short	*blk;
	int	x, y;

	/* 17 x 2 pixels, 2 planes: 2 words per row, 4 words per plane */
	memset(d, 0, sizeof d);
	d[0] = (short)0x8000;			/* plane 0, row 0, pixel 0 */
	d[7] = (short)0x8000;			/* plane 1, row 1, word 1: pixel 16 */
	d[1] = 0x0001;				/* plane 0, row 0, word 1: pixel 31 (outside) */
	d[2] = 0x0100;				/* plane 0, row 1, word 0: pixel 7 */
	d[4] = 0x0001;				/* plane 1, row 0, word 0: pixel 15 */
	CHECK(rsc_plane_get(d, 2, 17, 2, 0, 0) == 1);
	CHECK(rsc_plane_get(d, 2, 17, 2, 16, 1) == 2);
	CHECK(rsc_plane_get(d, 2, 17, 2, 7, 1) == 1);
	CHECK(rsc_plane_get(d, 2, 17, 2, 15, 0) == 2);
	CHECK(rsc_plane_get(d, 2, 17, 2, 1, 0) == 0);
	CHECK(rsc_plane_get(d, 2, 17, 2, 16, 0) == 0);
	CHECK(rsc_plane_get(d, 2, 17, 2, 17, 0) == 0);	/* outside */
	CHECK(rsc_plane_get(d, 2, 17, 2, 0, 2) == 0);
	CHECK(rsc_plane_get(d, 2, 17, 2, -1, 0) == 0);
	CHECK(rsc_plane_get(NULL, 2, 17, 2, 0, 0) == 0);
	CHECK(rsc_plane_get(d, 0, 17, 2, 0, 0) == 0);
	CHECK(rsc_plane_get(d, 9, 17, 2, 0, 0) == 0);

	CHECK(rsc_planes_to_pixmap(d, 2, 17, 2, &pm) == 0);
	CHECK(pm.w == 17 && pm.h == 2 && pm.planes == 2);
	for (y = 0; y < 2; y++)
		for (x = 0; x < 17; x++)
		{
			unsigned	e = 0;

			if (x == 0 && y == 0)
				e = 1;
			if (x == 16 && y == 1)
				e = 2;
			if (x == 7 && y == 1)
				e = 1;
			if (x == 15 && y == 0)
				e = 2;
			CHECK(pm.pix[y * 17 + x] == e);
		}
	CHECK(rsc_pixmap_to_planes(&pm, &blk) == 0);
	/* the padding bit of word 1 (pixel 31) is gone, the rest is as it was */
	CHECK(blk[0] == (short)0x8000 && blk[1] == 0 && blk[2] == 0x0100 && blk[3] == 0);
	CHECK(blk[4] == 0x0001 && blk[5] == 0 && blk[6] == 0 && blk[7] == (short)0x8000);
	rsc_mfree(blk);
	rsc_pixmap_free(&pm);

	/* put */
	memset(d, 0, sizeof d);
	CHECK(rsc_plane_put(d, 2, 17, 2, 16, 1, 3) == 0);
	CHECK(d[3] == (short)0x8000 && d[7] == (short)0x8000);
	CHECK(rsc_plane_put(d, 2, 17, 2, 16, 1, 1) == 0);	/* clears plane 1 */
	CHECK(d[3] == (short)0x8000 && d[7] == 0);
	CHECK(rsc_plane_put(d, 2, 17, 2, 16, 1, 4) == 0);	/* masked to 2 planes: 0 */
	CHECK(d[3] == 0);
	CHECK(rsc_plane_put(d, 2, 17, 2, 17, 0, 1) == -1);
	CHECK(rsc_plane_put(d, 2, 17, 2, 0, -1, 1) == -1);
	CHECK(rsc_plane_put(NULL, 2, 17, 2, 0, 0, 1) == -1);
	CHECK(rsc_plane_put(d, 0, 17, 2, 0, 0, 1) == -1);
	CHECK(rsc_plane_put(d, 9, 17, 2, 0, 0, 1) == -1);

	/* NULL data is a blank image */
	CHECK(rsc_planes_to_pixmap(NULL, 3, 5, 5, &pm) == 0);
	CHECK(pm.pix[24] == 0 && pm.planes == 3);
	rsc_pixmap_free(&pm);
	CHECK(pm.pix == NULL && pm.w == 0);
}

static void test_planes_roundtrip(void)
{
	long	n = 0;
	int	w, h, pl;

	for (pl = 1; pl <= 8; pl++)
		for (w = 1; w <= 65; w++)
			for (h = 1; h <= 33; h++)
			{
				size_t	nw = rsc_plane_bytes(w, h) * (size_t)pl / 2;
				short	*d = malloc(nw * 2), *e = NULL;
				short	*f = malloc(nw * 2);
				struct rsc_pixmap	a, b;
				size_t	i;
				int	x, y, ok = 1;
				unsigned	v;

				for (i = 0; i < nw; i++)
				{
					v = rnd();
					d[i] = (short)v;
				}
				CHECK(rsc_planes_to_pixmap(d, pl, w, h, &a) == 0);
				for (y = 0; y < h && ok; y++)
					for (x = 0; x < w; x++)
						if (a.pix[y * w + x] != rsc_plane_get(d, pl, w, h, x, y))
						{
							ok = 0;
							break;
						}
				CHECK(ok);
				CHECK(rsc_pixmap_to_planes(&a, &e) == 0);
				CHECK(rsc_planes_to_pixmap(e, pl, w, h, &b) == 0);
				CHECK(pm_equal(&a, &b));
				/* put pixel by pixel gives the same block, padding zero */
				memset(f, 0xff, nw * 2);
				memset(f, 0, nw * 2);
				ok = 1;
				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++)
						ok &= rsc_plane_put(f, pl, w, h, x, y, a.pix[y * w + x]) == 0;
				CHECK(ok);
				CHECK(memcmp(e, f, nw * 2) == 0);
				n++;
				rsc_mfree(e);
				rsc_pixmap_free(&a);
				rsc_pixmap_free(&b);
				free(d);
				free(f);
			}
	CHECK(n == 8 * 65 * 33);
	CHECK(live() == 0);
}

static void test_pixmap(void)
{
	struct rsc_pixmap	pm, o;
	long	inex = -1;
	int	i;

	CHECK(rsc_pixmap_new(NULL, 1, 1, 1) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_new(&pm, -1, 1, 1) == RSC_IMG_EARG && pm.pix == NULL);
	CHECK(rsc_pixmap_new(&pm, 1, 32768, 1) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_new(&pm, 1, 1, 0) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_new(&pm, 1, 1, 9) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_new(&pm, 32767, 32767, 1) == RSC_IMG_ETOOBIG);
	CHECK(rsc_pixmap_new(&pm, 4096, 4096, 1) == 0);	/* exactly the budget */
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 4097, 4096, 1) == RSC_IMG_ETOOBIG);
	CHECK(rsc_pixmap_new(&pm, 0, 5, 3) == 0 && pm.w == 0 && pm.h == 5 && pm.planes == 3);
	rsc_pixmap_free(&pm);
	rsc_pixmap_free(NULL);
	/* allocation failure */
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_pixmap_new(&pm, 2, 2, 1) == RSC_IMG_ENOMEM && pm.pix == NULL && pm.w == 0);
	rsc_test_fail_alloc_after(-1);

	/* scale */
	CHECK(rsc_pixmap_new(&pm, 2, 2, 4) == 0);
	pm.pix[0] = 1;
	pm.pix[1] = 2;
	pm.pix[2] = 3;
	pm.pix[3] = 4;
	CHECK(rsc_pixmap_scale(&pm, 4, 4, &o) == 0);
	{
		static const unsigned char e[16] = { 1, 1, 2, 2, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 4, 4 };

		CHECK(o.w == 4 && o.h == 4 && o.planes == 4 && memcmp(o.pix, e, 16) == 0);
	}
	rsc_pixmap_free(&o);
	CHECK(rsc_pixmap_scale(&pm, 1, 1, &o) == 0 && o.pix[0] == 1);
	rsc_pixmap_free(&o);
	CHECK(rsc_pixmap_scale(&pm, 3, 1, &o) == 0);
	CHECK(o.pix[0] == 1 && o.pix[1] == 1 && o.pix[2] == 2);	/* x * 2 / 3: 0, 0, 1 */
	rsc_pixmap_free(&o);
	CHECK(rsc_pixmap_scale(&pm, 0, 0, &o) == 0 && o.w == 0);
	rsc_pixmap_free(&o);
	CHECK(rsc_pixmap_scale(&pm, 40000, 1, &o) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_scale(NULL, 1, 1, &o) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_scale(&pm, 1, 1, &pm) == RSC_IMG_EARG);
	{
		struct rsc_pixmap	e;

		memset(&e, 0, sizeof e);
		e.planes = 1;
		CHECK(rsc_pixmap_scale(&e, 2, 2, &o) == RSC_IMG_EARG);
		CHECK(rsc_pixmap_scale(&e, 0, 3, &o) == 0 && o.w == 0 && o.h == 3);
		rsc_pixmap_free(&o);
	}
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_pixmap_scale(&pm, 4, 4, &o) == RSC_IMG_ENOMEM && o.pix == NULL);
	rsc_test_fail_alloc_after(-1);
	rsc_pixmap_free(&pm);

	/* convert */
	CHECK(rsc_pixmap_new(&pm, 4, 1, 1) == 0);
	pm.pix[0] = 1;
	pm.pix[1] = 0;
	pm.pix[2] = 1;
	pm.pix[3] = 0;
	for (i = 1; i <= 8; i++)
	{
		unsigned char	black = (unsigned char)((1 << i) - 1);

		CHECK(rsc_pixmap_convert(&pm, i, &o, &inex) == 0);
		CHECK(o.planes == i && o.pix[0] == black && o.pix[1] == 0 && o.pix[2] == black && o.pix[3] == 0);
		CHECK(inex == 0);
		rsc_pixmap_free(&o);
	}
	CHECK(rsc_pixmap_convert(&pm, 0, &o, &inex) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_convert(&pm, 9, &o, &inex) == RSC_IMG_EARG);
	CHECK(rsc_pixmap_convert(&pm, 1, &o, NULL) == 0 && memcmp(o.pix, pm.pix, 4) == 0);
	rsc_pixmap_free(&o);
	CHECK(rsc_pixmap_convert(&pm, 2, &pm, NULL) == RSC_IMG_EARG);
	rsc_pixmap_free(&pm);
}

static void test_convert_depths(void)
{
	static const int depths[4] = { 1, 2, 4, 8 };
	int	a, b, p;

	for (a = 1; a < 4; a++)		/* from 2, 4, 8 */
		for (b = 0; b < 4; b++)
		{
			int	from = depths[a], to = depths[b];
			struct rsc_pixmap	in, out;
			long	inex = -1, expect_inex = 0;
			int	n = 1 << from;

			CHECK(rsc_pixmap_new(&in, n, 1, from) == 0);
			for (p = 0; p < n; p++)
				in.pix[p] = (unsigned char)p;
			CHECK(rsc_pixmap_convert(&in, to, &out, &inex) == 0);
			for (p = 0; p < n; p++)
			{
				int	r, g, bl, r2, g2, b2;
				int	nn;

				rsc_palette_rgb(from, (unsigned)p, &r, &g, &bl);
				nn = naive_nearest(to, r, g, bl);
				CHECK(from == to || out.pix[p] == nn);
				rsc_palette_rgb(to, (unsigned)nn, &r2, &g2, &b2);
				if (r != r2 || g != g2 || bl != b2)
					expect_inex++;
			}
			if (from == to)
				CHECK(inex == 0 && memcmp(in.pix, out.pix, (size_t)n) == 0);
			else
				CHECK(inex == expect_inex);
			rsc_pixmap_free(&in);
			rsc_pixmap_free(&out);
		}
	/* hand-checked: 4 to 2 planes */
	{
		struct rsc_pixmap	in, out;
		long	inex;

		CHECK(rsc_pixmap_new(&in, 4, 1, 4) == 0);
		in.pix[0] = 1;		/* red */
		in.pix[1] = 15;		/* black */
		in.pix[2] = 7;		/* light grey: nearest of the four is white */
		in.pix[3] = 2;		/* green */
		CHECK(rsc_pixmap_convert(&in, 2, &out, &inex) == 0);
		CHECK(out.pix[0] == 1 && out.pix[1] == 3 && out.pix[2] == 0 && out.pix[3] == 2);
		CHECK(inex == 1);
		rsc_pixmap_free(&in);
		rsc_pixmap_free(&out);
		/* a depth with no palette */
		CHECK(rsc_pixmap_new(&in, 2, 1, 3) == 0);
		CHECK(rsc_pixmap_convert(&in, 4, &out, &inex) == RSC_IMG_ENODEPTH && out.pix == NULL);
		CHECK(rsc_pixmap_convert(&in, 3, &out, &inex) == 0);	/* the same depth: a copy */
		rsc_pixmap_free(&out);
		rsc_pixmap_free(&in);
		CHECK(rsc_pixmap_new(&in, 2, 1, 4) == 0);
		CHECK(rsc_pixmap_convert(&in, 5, &out, &inex) == RSC_IMG_ENODEPTH);
		rsc_pixmap_free(&in);
	}
	CHECK(live() == 0);
}

/* ---- writing PNM ------------------------------------------------------------------- */

static int bytes_are(const unsigned char *p, size_t len, const char *e, size_t elen)
{
	return len == elen && memcmp(p, e, len) == 0;
}

static void test_write(void)
{
	struct rsc_pixmap	pm;
	unsigned char	*out;
	size_t	len;
	int	x;

	CHECK(rsc_pixmap_new(&pm, 3, 2, 1) == 0);
	pm.pix[0] = 1;
	pm.pix[2] = 1;
	pm.pix[4] = 1;
	pm.pix[5] = 1;
	CHECK(rsc_image_to_pbm(&pm, 0, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P4\n3 2\n\xA0\x60", 9));
	rsc_mfree(out);
	CHECK(rsc_image_to_pbm(&pm, 1, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P1\n3 2\n101\n011\n", 15));
	rsc_mfree(out);
	rsc_pixmap_free(&pm);

	/* 9 wide: the second byte holds one pixel; garbage in the high bits of a pixel is ignored */
	CHECK(rsc_pixmap_new(&pm, 9, 1, 1) == 0);
	pm.pix[8] = 3;
	pm.pix[1] = 2;
	CHECK(rsc_image_to_pbm(&pm, 0, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P4\n9 1\n\x00\x80", 9));
	rsc_mfree(out);
	rsc_pixmap_free(&pm);

	/* colour */
	CHECK(rsc_pixmap_new(&pm, 2, 1, 2) == 0);
	pm.pix[0] = 1;
	pm.pix[1] = 3;
	CHECK(rsc_image_to_ppm(&pm, 0, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P6\n2 1\n255\n\xff\x00\x00\x00\x00\x00", 11 + 6));
	rsc_mfree(out);
	CHECK(rsc_image_to_ppm(&pm, 1, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P3\n2 1\n255\n255 0 0 0 0 0\n", 25));
	rsc_mfree(out);
	rsc_pixmap_free(&pm);

	/* 1 plane as a PPM: black and white */
	CHECK(rsc_pixmap_new(&pm, 2, 1, 1) == 0);
	pm.pix[1] = 1;
	CHECK(rsc_image_to_ppm(&pm, 0, &out, &len) == 0);
	CHECK(bytes_are(out, len, "P6\n2 1\n255\n\xff\xff\xff\x00\x00\x00", 17));
	rsc_mfree(out);
	rsc_pixmap_free(&pm);

	/* wide ascii: lines of at most 70 characters, a row starts a line */
	CHECK(rsc_pixmap_new(&pm, 80, 2, 1) == 0);
	for (x = 0; x < 80; x += 3)
		pm.pix[x] = 1;
	CHECK(rsc_image_to_pbm(&pm, 1, &out, &len) == 0);
	{
		size_t	i, line = 0, maxline = 0, lines = 0;

		for (i = 0; i < len; i++)
			if (out[i] == '\n')
			{
				if (line > maxline)
					maxline = line;
				line = 0;
				lines++;
			}
			else
				line++;
		CHECK(maxline == 70);
		CHECK(lines == 2 + 4);		/* "P1", "80 2", and 2 rows of 70 + 10 */
		CHECK(len == 3 + 5 + 2 * (80 + 2));
	}
	rsc_mfree(out);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 40, 3, 8) == 0);
	for (x = 0; x < 120; x++)
		pm.pix[x] = (unsigned char)(x * 7);
	CHECK(rsc_image_to_ppm(&pm, 1, &out, &len) == 0);
	{
		size_t	i, line = 0, maxline = 0;
		struct rsc_pnm	pn;

		for (i = 0; i < len; i++)
			if (out[i] == '\n')
			{
				if (line > maxline)
					maxline = line;
				line = 0;
			}
			else
				line++;
		CHECK(maxline <= 70 && maxline > 60);
		CHECK(rsc_image_from_pnm(out, len, 0, &pn) == 0);
		CHECK(pn.format == 3 && pn.w == 40 && pn.h == 3 && pn.maxval == 255);
		for (x = 0; x < 120; x++)
		{
			int	r, g, b;

			rsc_palette_rgb(8, pm.pix[x], &r, &g, &b);
			CHECK(pn.samples[3 * x] == r && pn.samples[3 * x + 1] == g && pn.samples[3 * x + 2] == b);
		}
		rsc_pnm_free(&pn);
	}
	rsc_mfree(out);
	rsc_pixmap_free(&pm);

	/* errors */
	CHECK(rsc_pixmap_new(&pm, 2, 2, 2) == 0);
	out = (unsigned char *)1;
	CHECK(rsc_image_to_pbm(&pm, 0, &out, &len) == RSC_IMG_EARG && out == NULL && len == 0);
	CHECK(rsc_image_to_pbm(&pm, 0, NULL, &len) == RSC_IMG_EARG);
	CHECK(rsc_image_to_pbm(&pm, 0, &out, NULL) == RSC_IMG_EARG);
	CHECK(rsc_image_to_ppm(NULL, 0, &out, &len) == RSC_IMG_EARG);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 2, 2, 3) == 0);
	CHECK(rsc_image_to_ppm(&pm, 0, &out, &len) == RSC_IMG_ENODEPTH && out == NULL);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 0, 2, 1) == 0);
	CHECK(rsc_image_to_pbm(&pm, 0, &out, &len) == RSC_IMG_EARG);
	CHECK(rsc_image_to_ppm(&pm, 1, &out, &len) == RSC_IMG_EARG);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 5, 4, 1) == 0);
	{
		long	n;

		for (n = 0; n < 20; n++)
		{
			rsc_test_fail_alloc_after(n);
			x = rsc_image_to_pbm(&pm, (int)(n & 1), &out, &len);
			rsc_test_fail_alloc_after(-1);
			if (x == 0)
			{
				rsc_mfree(out);
				break;
			}
			CHECK(x == RSC_IMG_ENOMEM && out == NULL && len == 0);
		}
		CHECK(n >= 1 && n < 20);
	}
	rsc_pixmap_free(&pm);
	CHECK(live() == 0);
}

/* ---- reading PNM --------------------------------------------------------------------- */

#define PARSE(s, p)	rsc_image_from_pnm((const unsigned char *)(s), sizeof(s) - 1, 0, (p))

static int samples_are(const struct rsc_pnm *p, const char *e, size_t n)
{
	return p->samples && memcmp(p->samples, e, n) == 0;
}

static void test_parse_forms(void)
{
	struct rsc_pnm	p;

	/* P1: separated, compact, with comments, CRLF, tabs */
	CHECK(PARSE("P1\n3 2\n1 0 1\n0 1 1\n", &p) == 0);
	CHECK(p.format == 1 && p.w == 3 && p.h == 2 && p.maxval == 1 && p.channels == 1);
	CHECK(samples_are(&p, "\1\0\1\0\1\1", 6));
	rsc_pnm_free(&p);
	CHECK(p.samples == NULL);
	CHECK(PARSE("P1 3 2 101011", &p) == 0 && samples_are(&p, "\1\0\1\0\1\1", 6));
	rsc_pnm_free(&p);
	CHECK(PARSE("P1 # a comment\n# another one\n3 2 #x\n101\n# in the data\n011\n", &p) == 0);
	CHECK(samples_are(&p, "\1\0\1\0\1\1", 6));
	rsc_pnm_free(&p);
	CHECK(PARSE("P1\r\n3\t2\r\n1\t0 1\r\n0 1 1\r\n", &p) == 0 && samples_are(&p, "\1\0\1\0\1\1", 6));
	rsc_pnm_free(&p);
	CHECK(PARSE("P1#c\n1 1\n1", &p) == 0 && p.samples[0] == 1);	/* a comment right after the magic */
	rsc_pnm_free(&p);
	CHECK(PARSE("P1\n1 1\n1 trailing garbage", &p) == 0);		/* bytes after the data are ignored */
	rsc_pnm_free(&p);

	/* P2 */
	CHECK(PARSE("P2\n3 1\n255\n0 128 255\n", &p) == 0);
	CHECK(p.format == 2 && p.maxval == 255 && p.channels == 1 && samples_are(&p, "\x00\x80\xff", 3));
	rsc_pnm_free(&p);
	CHECK(PARSE("P2 2 1 15 # max\n 5\n15", &p) == 0 && p.maxval == 15 && samples_are(&p, "\5\x0f", 2));
	{
		int	r, g, b;

		CHECK(rsc_pnm_rgb(&p, 0, 0, &r, &g, &b) == 0 && r == 85 && g == 85 && b == 85);
		CHECK(rsc_pnm_rgb(&p, 1, 0, &r, &g, &b) == 0 && r == 255 && g == 255 && b == 255);
		CHECK(rsc_pnm_rgb(&p, 2, 0, &r, &g, &b) == RSC_IMG_EARG);
		CHECK(rsc_pnm_rgb(&p, 0, -1, &r, &g, &b) == RSC_IMG_EARG);
		CHECK(rsc_pnm_rgb(NULL, 0, 0, &r, &g, &b) == RSC_IMG_EARG);
		CHECK(rsc_pnm_rgb(&p, 1, 0, NULL, NULL, NULL) == 0);
	}
	rsc_pnm_free(&p);

	/* P3 */
	CHECK(PARSE("P3\n2 1\n255\n255 0 0  0 0 255\n", &p) == 0);
	CHECK(p.format == 3 && p.channels == 3 && samples_are(&p, "\xff\0\0\0\0\xff", 6));
	rsc_pnm_free(&p);

	/* P4: 10 wide = 2 bytes per row; padding bits are ignored */
	CHECK(PARSE("P4\n10 2\n\xaa\xff\x55\x40", &p) == 0);
	CHECK(p.format == 4 && p.maxval == 1);
	CHECK(samples_are(&p, "\1\0\1\0\1\0\1\0\1\1" "\0\1\0\1\0\1\0\1\0\1", 20));
	rsc_pnm_free(&p);
	/* binary data that looks like a comment or whitespace */
	CHECK(PARSE("P4\n8 2\n\x23\x0a", &p) == 0 && samples_are(&p, "\0\0\1\0\0\0\1\1" "\0\0\0\0\1\0\1\0", 16));
	rsc_pnm_free(&p);
	CHECK(PARSE("P4\n8 1\n\x20", &p) == 0 && samples_are(&p, "\0\0\1\0\0\0\0\0", 8));
	rsc_pnm_free(&p);

	/* P5, P6 */
	CHECK(PARSE("P5\n2 2\n255\n\x23\x0a\x20\xff", &p) == 0 && samples_are(&p, "\x23\x0a\x20\xff", 4));
	rsc_pnm_free(&p);
	CHECK(PARSE("P5 # c\n1 1 # d\n200\n\x07", &p) == 0 && p.maxval == 200 && p.samples[0] == 7);
	rsc_pnm_free(&p);
	CHECK(PARSE("P6\n1 2\n15\n\x0f\x00\x05\x01\x02\x03", &p) == 0);
	CHECK(p.format == 6 && p.channels == 3 && samples_are(&p, "\x0f\0\5\1\2\3", 6));
	{
		int	r, g, b;

		CHECK(rsc_pnm_rgb(&p, 0, 0, &r, &g, &b) == 0 && r == 255 && g == 0 && b == 85);
		CHECK(rsc_pnm_rgb(&p, 0, 1, &r, &g, &b) == 0 && r == 17 && g == 34 && b == 51);
	}
	rsc_pnm_free(&p);
	rsc_pnm_free(&p);		/* twice is fine */
	rsc_pnm_free(NULL);

	/* numbers: leading zeros of any number, a huge number of them */
	CHECK(PARSE("P1\n0001 001\n1", &p) == 0 && p.w == 1 && p.h == 1);
	rsc_pnm_free(&p);
	CHECK(live() == 0);
}

static void test_parse_errors(void)
{
	struct rsc_pnm	p;
	long	c0;

	CHECK(rsc_image_from_pnm(NULL, 0, 0, &p) == RSC_IMG_ETRUNC);
	CHECK(rsc_image_from_pnm(NULL, 5, 0, &p) == RSC_IMG_EARG);
	CHECK(rsc_image_from_pnm((const unsigned char *)"P1", 2, 0, NULL) == RSC_IMG_EARG);
	CHECK(PARSE("", &p) == RSC_IMG_ETRUNC && p.samples == NULL);
	CHECK(PARSE("P", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P1", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P1\n", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P1\n3", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P1\n3 ", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("X1\n1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("p1\n1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P0\n1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P7\n1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("Pa\n1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P12 2\n1", &p) == RSC_IMG_EFORMAT);	/* no separator after the magic */
	CHECK(PARSE("P1x1 1 1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n0 1\n1", &p) == RSC_IMG_EFORMAT);	/* zero dimensions */
	CHECK(PARSE("P1\n1 0\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n-1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n+1 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n1.5 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\na 1\n1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n2 2\n10x1", &p) == RSC_IMG_EFORMAT);	/* bad pixel */
	CHECK(PARSE("P1\n2 2\n1 0 2 1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P1\n2 2\n1 0 1", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P2\n1 1\n0\n0", &p) == RSC_IMG_EFORMAT);	/* maxval 0 */
	CHECK(PARSE("P5\n1 1\n0\n\0", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P6\n1 1\n0\n\0\0\0", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P2\n1 1\n256\n0", &p) == RSC_IMG_EMAXVAL);	/* 16 bit */
	CHECK(PARSE("P5\n1 1\n65535\n\0\0", &p) == RSC_IMG_EMAXVAL);
	CHECK(PARSE("P6\n1 1\n65536\n\0\0\0\0\0\0", &p) == RSC_IMG_EMAXVAL);
	CHECK(PARSE("P2\n1 1\n4294967296\n0", &p) == RSC_IMG_EMAXVAL);
	CHECK(PARSE("P2\n1 1\n255\n256", &p) == RSC_IMG_EFORMAT);	/* a sample above maxval */
	CHECK(PARSE("P2\n1 2\n10\n1 11", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P3\n1 1\n10\n1 2 11", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P5\n1 2\n10\n\x01\x0b", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P6\n1 1\n10\n\x01\x02\x0b", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P2\n1 2\n255\n1 x", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P2\n1 2\n255\n1 -1", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P2\n1 2\n255\n1", &p) == RSC_IMG_ETRUNC);
	/* binary: a single whitespace byte is needed before the data */
	CHECK(PARSE("P5\n1 1\n255", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P5\n1 1\n255\n", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P5\n1 1\n255x\0", &p) == RSC_IMG_EFORMAT);
	CHECK(PARSE("P4\n1 1\n", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P4\n1 1", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P4\n9 2\n\0\0\0", &p) == RSC_IMG_ETRUNC);

	/* hostile dimensions: refused before anything is allocated */
	rsc_test_fail_alloc_after(-1);
	c0 = rsc_test_alloc_count();
	CHECK(c0 == 0);
	CHECK(PARSE("P1\n2147483648 1\n1", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P1\n1 2147483648\n1", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P1\n4294967296 1\n1", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P1\n99999999999999999999999999 1\n1", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P5\n2147483648 2147483648\n255\n", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P4\n32768 1\n\0", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P6\n32767 32767\n255\n", &p) == RSC_IMG_ETOOBIG);	/* the budget */
	CHECK(PARSE("P5\n4097 1024\n255\n", &p) == RSC_IMG_ETOOBIG);
	CHECK(PARSE("P5\n4096 1024\n255\n", &p) == RSC_IMG_ETRUNC);	/* within the budget, no data */
	CHECK(PARSE("P3\n1000 1000\n255\n1 2 3", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P2\n1000 1000\n255\n1 2 3", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P1\n1000 1000\n1 0 1", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P4\n4000 1000\n\0\0\0", &p) == RSC_IMG_ETRUNC);
	CHECK(PARSE("P6\n1000 1000\n255\n\0\0\0", &p) == RSC_IMG_ETRUNC);
	CHECK(rsc_test_alloc_count() == c0);
	CHECK(live() == 0);
	/* the budget argument */
	CHECK(rsc_image_from_pnm((const unsigned char *)"P5\n4 3\n255\n123456789012", 24, 11, &p)
	      == RSC_IMG_ETOOBIG);
	CHECK(rsc_image_from_pnm((const unsigned char *)"P5\n4 3\n255\n123456789012", 24, 12, &p) == 0);
	rsc_pnm_free(&p);
	CHECK(rsc_image_from_pnm((const unsigned char *)"P5\n4 3\n255\n123456789012", 24, 1000000000UL, &p) == 0);
	rsc_pnm_free(&p);
	CHECK(live() == 0);
}

/* A buffer of the head, n copies of the byte c, and the tail. */
static unsigned char *build_buf(const char *head, size_t n, unsigned char c, const char *tail, size_t *len)
{
	size_t	hl = strlen(head), tl = strlen(tail);
	unsigned char	*b = malloc(hl + n + tl + 1);

	memcpy(b, head, hl);
	memset(b + hl, c, n);
	memcpy(b + hl + n, tail, tl);
	*len = hl + n + tl;
	return b;
}

static void test_parse_hostile(void)
{
	struct rsc_pnm	p;
	unsigned char	*b;
	size_t	len;
	long	c0;

	rsc_test_fail_alloc_after(-1);
	c0 = rsc_test_alloc_count();
	/* 70000 digits */
	b = build_buf("P1\n", 70000, '9', " 1\n1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_ETOOBIG);
	free(b);
	b = build_buf("P1\n1 ", 70000, '9', "\n1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_ETOOBIG);
	free(b);
	b = build_buf("P2\n1 1\n", 70000, '9', "\n1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_EMAXVAL);
	free(b);
	b = build_buf("P2\n1 1\n255\n", 70000, '9', "\n", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_EFORMAT);
	free(b);
	/* a number that never ends, with no other byte */
	b = build_buf("P1\n", 70000, '1', "", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_ETRUNC);		/* a width, and no height */
	free(b);
	b = build_buf("P1\n", 70000, '1', " 1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_ETOOBIG);
	free(b);
	/* leading zeros are zeros: 70000 of them and a 1 is the number 1 */
	b = build_buf("P1\n", 70000, '0', "1 1\n1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == 0 && p.w == 1 && p.h == 1 && p.samples[0] == 1);
	rsc_pnm_free(&p);
	free(b);
	b = build_buf("P2\n1 1\n", 70000, '0', "5\n5", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == 0 && p.maxval == 5 && p.samples[0] == 5);
	rsc_pnm_free(&p);
	free(b);
	/* 70000 bytes of comment, and of whitespace, in the header and in the data */
	b = build_buf("P1\n# ", 70000, 'x', "\n1 1\n1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == 0 && p.w == 1);
	rsc_pnm_free(&p);
	free(b);
	b = build_buf("P1\n1 1", 70000, ' ', "1", &len);
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == 0 && p.samples[0] == 1);
	rsc_pnm_free(&p);
	free(b);
	b = build_buf("P1\n1 1\n#", 70000, 'x', "", &len);		/* the comment runs to the end */
	CHECK(rsc_image_from_pnm(b, len, 0, &p) == RSC_IMG_ETRUNC);
	free(b);
	CHECK(rsc_test_alloc_count() == c0 + 6);	/* the four parsed ones, and the two that fail while decoding */
	CHECK(live() == 0);

	/* a long run of whitespace is not a quadratic cost: many numbers */
	b = build_buf("P2\n300 300\n255\n", 0, 0, "", &len);
	free(b);
	{
		size_t	n = 300 * 300, i;
		unsigned char	*q = malloc(40 + n * 2);
		size_t	k;

		memcpy(q, "P2\n300 300\n255\n", 15);
		k = 15;
		for (i = 0; i < n; i++)
		{
			q[k++] = (unsigned char)('0' + i % 10);
			q[k++] = (i % 300 == 299) ? '\n' : ' ';
		}
		CHECK(rsc_image_from_pnm(q, k, 0, &p) == 0);
		CHECK(p.samples[299] == 9 && p.samples[n - 1] == 9);
		rsc_pnm_free(&p);
		free(q);
	}

	/* random bytes after a valid header never crash and never leak */
	{
		static const char *heads[6] = { "P1\n7 5\n", "P2\n7 5\n255\n", "P3\n7 5\n255\n", "P4\n7 5\n",
						"P5\n7 5\n255\n", "P6\n7 5\n255\n" };
		int	t, k;
		long	ok = 0;

		for (t = 0; t < 3000; t++)
		{
			unsigned char	buf[200];
			size_t	hl = strlen(heads[t % 6]), n, i;
			unsigned	v;

			memcpy(buf, heads[t % 6], hl);
			v = rnd();
			n = hl + (v % (200 - hl));
			for (i = hl; i < n; i++)
			{
				v = rnd();
				/* mostly text characters, so that the text forms get far */
				buf[i] = (unsigned char)((v & 0x300) ? "0123456789 \n#01"[v & 15] : (v >> 12));
			}
			k = rsc_image_from_pnm(buf, n, 0, &p);
			if (k == 0)
			{
				ok++;
				rsc_pnm_free(&p);
			}
			else
				CHECK(p.samples == NULL && k < 0);
		}
		CHECK(ok > 100);
	}
	CHECK(live() == 0);
}

static void sweep(const char *name, const unsigned char *f, size_t len, size_t lasttok)
{
	struct rsc_pnm	p;
	size_t	n;
	int	e, bad = 0;

	for (n = 0; n < len; n++)
	{
		e = rsc_image_from_pnm(f, n, 0, &p);
		if (e == 0)
			rsc_pnm_free(&p);
		if (n + lasttok < len && e == 0)
			bad++;
		if (e != 0 && e != RSC_IMG_ETRUNC && e != RSC_IMG_EFORMAT)
			bad++;
		if (e != 0 && p.samples != NULL)
			bad++;
	}
	if (bad)
		fprintf(stderr, "truncation sweep of %s: %d bad results\n", name, bad);
	CHECK(bad == 0);
	CHECK(rsc_image_from_pnm(f, len, 0, &p) == 0);
	rsc_pnm_free(&p);
	CHECK(live() == 0);
}

#define SWEEP(name, str, lasttok)	sweep(name, (const unsigned char *)(str), sizeof(str) - 1, lasttok)

static void test_parse_truncation(void)
{
	SWEEP("P1", "P1\n# c\n3 2\n1 0 1\n0 1 1", 0);
	SWEEP("P1 compact", "P1 3 2 101011", 0);
	SWEEP("P2", "P2\n2 2\n# c\n255\n1 22\n3 44", 2);
	SWEEP("P3", "P3\n1 2\n255\n1 2 3\n40 50 60", 2);
	SWEEP("P4", "P4\n10 2\n\xaa\xff\x55\x40", 0);
	SWEEP("P5", "P5\n2 2\n255\n\x01\x02\x03\x04", 0);
	SWEEP("P6", "P6\n1 2\n255\n\x01\x02\x03\x04\x05\x06", 0);
}

/* ---- from PNM to pixmap --------------------------------------------------------------- */

static void test_pnm_to_pixmap(void)
{
	struct rsc_pnm	p;
	struct rsc_pixmap	pm;
	long	bad = 7;

	/* gray to a mask: the default threshold is 50% darkness */
	CHECK(PARSE("P5\n4 1\n255\n\x00\x7f\x80\xff", &p) == 0);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 0, 0, &pm, &bad) == 0 && bad == -1);
	CHECK(pm.planes == 1 && pm.pix[0] == 1 && pm.pix[1] == 1 && pm.pix[2] == 0 && pm.pix[3] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 100, 0, &pm, NULL) == 0);
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 0 && pm.pix[2] == 0 && pm.pix[3] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 1, 0, &pm, NULL) == 0);
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 1 && pm.pix[2] == 1 && pm.pix[3] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 51, 0, &pm, NULL) == 0);	/* 51% dark: 0x7f is 50.2% */
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 101, 0, &pm, NULL) == RSC_IMG_EARG && pm.pix == NULL);
	CHECK(rsc_pnm_to_pixmap(&p, 1, -1, 0, &pm, NULL) == RSC_IMG_EARG);
	CHECK(rsc_pnm_to_pixmap(&p, 3, 0, 0, &pm, NULL) == RSC_IMG_ENODEPTH);
	CHECK(rsc_pnm_to_pixmap(&p, 0, 0, 0, &pm, NULL) == RSC_IMG_EARG);
	CHECK(rsc_pnm_to_pixmap(&p, 9, 0, 0, &pm, NULL) == RSC_IMG_EARG);
	CHECK(rsc_pnm_to_pixmap(NULL, 1, 0, 0, &pm, NULL) == RSC_IMG_EARG);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 0, 0, NULL, NULL) == RSC_IMG_EARG);
	/* gray to colours */
	CHECK(rsc_pnm_to_pixmap(&p, 4, 0, 0, &pm, NULL) == 0);
	CHECK(pm.pix[0] == 15 && pm.pix[3] == 0);		/* black, white */
	CHECK(pm.pix[1] == 8 && pm.pix[2] == 8);		/* 127 and 128 are nearer to the dark grey (109) than to 182 */
	rsc_pixmap_free(&pm);
	rsc_pnm_free(&p);

	/* a bitmap: 1 is black */
	CHECK(PARSE("P1 2 1 10", &p) == 0);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 0, 0, &pm, NULL) == 0 && pm.pix[0] == 1 && pm.pix[1] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 2, 0, 1, &pm, NULL) == 0 && pm.pix[0] == 3 && pm.pix[1] == 0);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 8, 0, 1, &pm, NULL) == 0 && pm.pix[0] == 15 && pm.pix[1] == 0);
	rsc_pixmap_free(&pm);
	rsc_pnm_free(&p);

	/* colour: nearest and exact */
	CHECK(PARSE("P6\n3 1\n255\n\xff\x00\x00\xc8\xc8\xc8\x00\x00\x00", &p) == 0);
	CHECK(rsc_pnm_to_pixmap(&p, 4, 0, 0, &pm, &bad) == 0 && bad == -1);
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 7 && pm.pix[2] == 15);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 4, 0, 1, &pm, &bad) == RSC_IMG_ENOTPAL && bad == 1 && pm.pix == NULL);
	CHECK(rsc_pnm_to_pixmap(&p, 2, 0, 0, &pm, &bad) == 0);
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 0 && pm.pix[2] == 3);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pnm_to_pixmap(&p, 8, 0, 1, &pm, &bad) == RSC_IMG_ENOTPAL && bad == 1);
	rsc_pnm_free(&p);
	CHECK(PARSE("P6\n2 1\n255\n\xff\x00\x00\x00\x00\x00", &p) == 0);
	CHECK(rsc_pnm_to_pixmap(&p, 2, 0, 1, &pm, &bad) == 0 && pm.pix[0] == 1 && pm.pix[1] == 3);
	rsc_pixmap_free(&pm);
	rsc_pnm_free(&p);
	/* a luminance threshold on a colour file */
	CHECK(PARSE("P6\n3 1\n255\n\xff\x00\x00\x00\xff\x00\x00\x00\xff", &p) == 0);
	CHECK(rsc_pnm_to_pixmap(&p, 1, 0, 0, &pm, NULL) == 0);	/* lum 76, 149, 29: dark 179, 106, 226 */
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 0 && pm.pix[2] == 1);
	rsc_pixmap_free(&pm);
	rsc_pnm_free(&p);
	/* allocation failure */
	CHECK(PARSE("P5\n2 2\n255\n\0\0\0\0", &p) == 0);
	rsc_test_fail_alloc_after(0);
	CHECK(rsc_pnm_to_pixmap(&p, 4, 0, 0, &pm, NULL) == RSC_IMG_ENOMEM && pm.pix == NULL);
	rsc_test_fail_alloc_after(-1);
	rsc_pnm_free(&p);
	rsc_test_fail_alloc_after(0);
	CHECK(PARSE("P5\n2 2\n255\n\0\0\0\0", &p) == RSC_IMG_ENOMEM && p.samples == NULL);
	rsc_test_fail_alloc_after(-1);
	CHECK(live() == 0);
}

/* ---- round trips through files ---------------------------------------------------------- */

static void test_file_roundtrip(void)
{
	static const int depths[4] = { 1, 2, 4, 8 };
	long	n = 0;
	int	w, h, d, asc;

	for (d = 0; d < 4; d++)
		for (w = 1; w <= 65; w++)
			for (h = 1; h <= 33; h++)
			{
				int	pl = depths[d];

				for (asc = 0; asc < 2; asc++)
				{
					struct rsc_pixmap	a, b;
					struct rsc_pnm	pn;
					unsigned char	*f1, *f2;
					size_t	l1, l2;
					int	e;

					if (asc && ((w + h) % 5))
						continue;	/* the text forms on a part of the sizes */
					rand_pm(&a, w, h, pl);
					e = pl == 1 ? rsc_image_to_pbm(&a, asc, &f1, &l1) : rsc_image_to_ppm(&a, asc, &f1, &l1);
					CHECK(e == 0);
					CHECK(rsc_image_from_pnm(f1, l1, 0, &pn) == 0);
					CHECK(pn.w == w && pn.h == h);
					CHECK(pn.format == (pl == 1 ? (asc ? 1 : 4) : (asc ? 3 : 6)));
					CHECK(rsc_pnm_to_pixmap(&pn, pl, 0, pl > 1, &b, NULL) == 0);
					e = pl == 1 ? rsc_image_to_pbm(&b, asc, &f2, &l2) : rsc_image_to_ppm(&b, asc, &f2, &l2);
					CHECK(e == 0);
					CHECK(l1 == l2 && memcmp(f1, f2, l1) == 0);
					if (pl <= 4)
						CHECK(pm_equal(&a, &b));
					n++;
					rsc_mfree(f1);
					rsc_mfree(f2);
					rsc_pnm_free(&pn);
					rsc_pixmap_free(&a);
					rsc_pixmap_free(&b);
				}
			}
	CHECK(n > 8000);
	CHECK(live() == 0);

	/* PBM, PGM and PPM of the same picture import to the same image */
	{
		struct rsc_pnm	a, b, c;
		struct rsc_pixmap	pa, pb, pc;

		CHECK(PARSE("P1\n3 1\n1 0 1\n", &a) == 0);
		CHECK(PARSE("P2\n3 1\n255\n0 255 0\n", &b) == 0);
		CHECK(PARSE("P3\n3 1\n1\n0 0 0 1 1 1 0 0 0\n", &c) == 0);
		CHECK(rsc_pnm_to_pixmap(&a, 1, 0, 0, &pa, NULL) == 0);
		CHECK(rsc_pnm_to_pixmap(&b, 1, 0, 0, &pb, NULL) == 0);
		CHECK(rsc_pnm_to_pixmap(&c, 1, 0, 0, &pc, NULL) == 0);
		CHECK(pm_equal(&pa, &pb) && pm_equal(&pb, &pc));
		CHECK(pa.pix[0] == 1 && pa.pix[1] == 0 && pa.pix[2] == 1);
		rsc_pixmap_free(&pa);
		rsc_pixmap_free(&pb);
		rsc_pixmap_free(&pc);
		rsc_pnm_free(&a);
		rsc_pnm_free(&b);
		rsc_pnm_free(&c);
	}
	CHECK(live() == 0);
}

/* ---- the images of a model -------------------------------------------------------------- */

#define ICON_W	20
#define ICON_H	5
#define ICON_WORDS	10	/* one plane of 20 x 5 */

static const unsigned char bb_bytes[6] = { 0xa5, 0x0f, 0x80, 0x01, 0xff, 0x3c };	/* wb 3, hl 2 */

/* The stream of bytes as the reader stores it: words of the machine, an odd
 * last byte as it is. */
static short *stream_words(const unsigned char *b, size_t n)
{
	unsigned char	*p = malloc(n + 1);
	size_t	k;

	for (k = 0; k + 1 < n; k += 2)
	{
		unsigned short	w = (unsigned short)(b[k] << 8 | b[k + 1]);

		memcpy(p + k, &w, 2);
	}
	if (k < n)
		p[k] = b[k];
	return (short *)(void *)p;
}

static struct rsc_imgsel S(int tree, int obj, int ver, int sel, int mask)
{
	struct rsc_imgsel	s;

	s.tree = tree;
	s.obj = obj;
	s.version = ver;
	s.sel = sel;
	s.mask = mask;
	s.planes = 0;
	return s;
}

static struct rsc_imgsel SN(int tree, int obj, int planes, int mask)
{
	struct rsc_imgsel	s = S(tree, obj, RSC_VER_NEW, 0, mask);

	s.planes = planes;
	return s;
}

/* The model of the tests: tree 0 holds
 *   1 G_ICON 20 x 5     2 G_IMAGE wb 3 hl 2     3 G_CICON 20 x 5 with versions
 *   0 (4 planes, selected), 1 (8 planes), 2 (3 planes)     4 G_STRING
 * and the free images 0 (wb 2 hl 4) and 1 (wb 3 hl 1, no data). */
static struct rsc *build(void)
{
	struct rsc	*r = rsc_new();
	short	d[ICON_WORDS], m[ICON_WORDS], cd[80], cm[ICON_WORDS], sd[80];
	short	*sw;
	ICONBLK	ib;
	BITBLK	bb;
	struct rsc_cicon	*c;
	int	t = rsc_tree_add(r, 1);

	if (!r || t != 0)
		return NULL;
	memset(&ib, 0, sizeof ib);
	words(d, ICON_WORDS, 1);
	words(m, ICON_WORDS, 2);
	norm(d, 1, ICON_W, ICON_H);
	norm(m, 1, ICON_W, ICON_H);
	ib.ib_pdata = d;
	ib.ib_pmask = m;
	ib.ib_wicon = ICON_W;
	ib.ib_hicon = ICON_H;
	ib.ib_ptext = (char *)"ICO";
	rsc_obj_add(r, 0, 0, G_ICON);
	rsc_obj_set_iconblk(r, 0, 1, &ib);
	memset(&bb, 0, sizeof bb);
	sw = stream_words(bb_bytes, 6);
	bb.bi_pdata = sw;
	bb.bi_wb = 3;
	bb.bi_hl = 2;
	rsc_obj_add(r, 0, 0, G_IMAGE);
	rsc_obj_set_bitblk(r, 0, 2, &bb);
	free(sw);
	rsc_obj_add(r, 0, 0, G_CICON);
	words(d, ICON_WORDS, 3);
	words(m, ICON_WORDS, 4);
	norm(d, 1, ICON_W, ICON_H);
	norm(m, 1, ICON_W, ICON_H);
	rsc_obj_set_cicon(r, 0, 3, &ib, "CIC");
	c = rsc_obj_cicon(r, 0, 3);
	words(cd, 40, 5);
	words(cm, ICON_WORDS, 6);
	words(sd, 40, 7);
	norm(cd, 4, ICON_W, ICON_H);
	norm(cm, 1, ICON_W, ICON_H);
	norm(sd, 4, ICON_W, ICON_H);
	rsc_cicon_add_version(c, 4, cd, cm, sd, cm);
	words(cd, 80, 8);
	norm(cd, 8, ICON_W, ICON_H);
	rsc_cicon_add_version(c, 8, cd, cm, NULL, NULL);
	words(cd, 30, 9);
	norm(cd, 3, ICON_W, ICON_H);
	rsc_cicon_add_version(c, 3, cd, cm, NULL, NULL);
	rsc_obj_add(r, 0, 0, G_STRING);
	words(cd, 8, 10);
	memset(&bb, 0, sizeof bb);
	bb.bi_pdata = cd;
	bb.bi_wb = 2;
	bb.bi_hl = 4;
	rsc_frimg_add(r, &bb);
	bb.bi_pdata = NULL;
	bb.bi_wb = 3;
	bb.bi_hl = 1;
	rsc_frimg_add(r, &bb);
	return r;
}

static void fhash(uint64_t *h, const void *p, size_t n)
{
	const unsigned char	*b = p;

	while (n--)
	{
		*h ^= *b++;
		*h *= 1099511628211ULL;
	}
}

static void fhash_blk(uint64_t *h, const void *p, size_t n)
{
	int	has = p != NULL;

	fhash(h, &has, sizeof has);
	if (p)
		fhash(h, p, n);
}

/* A fingerprint of every image of the model. */
static uint64_t fp(struct rsc *r)
{
	uint64_t	h = 14695981039346656037ULL;
	OBJECT	*o;
	int	n, i, k;

	o = rsc_tree_objects(r, 0, &n);
	for (i = 0; i < n; i++)
	{
		ICONBLK	*ib = NULL;
		BITBLK	*bb = NULL;

		if (o[i].ob_type == G_ICON)
			ib = o[i].ob_spec.iconblk;
		if (o[i].ob_type == G_IMAGE)
			bb = o[i].ob_spec.bitblk;
		if (o[i].ob_type == G_CICON)
		{
			struct rsc_cicon	*c = rsc_obj_cicon(r, 0, i);
			int	v;

			ib = &c->blk.monoblk;
			for (v = 0; v < rsc_cicon_nversions(c); v++)
			{
				struct rsc_cicon_version	*cv = rsc_cicon_version(c, v);
				size_t	pb = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);

				fhash(&h, &cv->c.num_planes, sizeof cv->c.num_planes);
				fhash(&h, cv->file_ptr, sizeof cv->file_ptr);
				fhash_blk(&h, cv->c.col_data, pb * (size_t)cv->c.num_planes);
				fhash_blk(&h, cv->c.col_mask, pb);
				fhash_blk(&h, cv->c.sel_data, pb * (size_t)cv->c.num_planes);
				fhash_blk(&h, cv->c.sel_mask, pb);
			}
		}
		if (ib)
		{
			size_t	pb = rsc_plane_bytes(ib->ib_wicon, ib->ib_hicon);

			fhash(&h, &ib->ib_wicon, sizeof ib->ib_wicon);
			fhash(&h, &ib->ib_hicon, sizeof ib->ib_hicon);
			fhash_blk(&h, ib->ib_pdata, pb);
			fhash_blk(&h, ib->ib_pmask, pb);
		}
		if (bb)
		{
			fhash(&h, &bb->bi_wb, sizeof bb->bi_wb);
			fhash_blk(&h, bb->bi_pdata, rsc_bitblk_bytes(bb));
		}
	}
	for (k = 0; k < rsc_nfrimg(r); k++)
	{
		BITBLK	*bb = rsc_frimg(r, k);

		fhash(&h, &bb->bi_wb, sizeof bb->bi_wb);
		fhash_blk(&h, bb->bi_pdata, rsc_bitblk_bytes(bb));
	}
	return h;
}

static void test_info(void)
{
	struct rsc	*r = build();
	struct rsc_imginfo	in;
	struct rsc_imgsel	s;

	CHECK(r != NULL);
	s = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 20 && in.h == 5 && in.planes == 1 && in.blank == 0);
	s = S(0, 1, -1, 0, 1);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 20 && in.h == 5 && in.planes == 1);
	s = S(0, 2, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 24 && in.h == 2 && in.planes == 1);
	s = S(0, 2, -1, 0, 1);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOMASK);
	CHECK(in.w == 0 && in.h == 0);
	s = S(RSC_IMG_FREE, 0, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 16 && in.h == 4 && in.planes == 1 && !in.blank);
	s = S(RSC_IMG_FREE, 1, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 24 && in.h == 1 && in.blank == 1);
	s = S(RSC_IMG_FREE, 0, -1, 0, 1);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOMASK);
	s = S(RSC_IMG_FREE, 2, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	s = S(RSC_IMG_FREE, -1, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	/* the colour icon */
	s = S(0, 3, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.w == 20 && in.h == 5 && in.planes == 1);
	s = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 4);
	s = S(0, 3, 0, 0, 1);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 1);
	s = S(0, 3, 0, 1, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 4);
	s = S(0, 3, 0, 1, 1);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 1);
	s = S(0, 3, 1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 8);
	s = S(0, 3, 1, 1, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);	/* no selected image */
	s = S(0, 3, 2, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == 0 && in.planes == 3);
	s = S(0, 3, 3, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	s = S(0, 3, RSC_VER_NEW, 0, 0);
	s.planes = 2;
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	/* not images */
	s = S(0, 4, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	s = S(0, 0, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	s = S(0, 1, 0, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);	/* a version of a G_ICON */
	s = S(0, 1, -1, 1, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);	/* sel of a G_ICON */
	s = S(0, 2, 0, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	s = S(0, 99, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	s = S(0, -1, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	s = S(5, 0, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	s = S(-2, 0, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	s = S(0, 1, -3, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_EARG);
	CHECK(rsc_image_info(r, NULL, &in) == RSC_IMG_EARG);
	CHECK(rsc_image_info(NULL, &s, &in) == RSC_IMG_EARG);
	s = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, NULL) == RSC_IMG_EARG);
	/* an object whose record is gone, and a G_ICON turned into a string */
	rsc_obj_set_iconblk(r, 0, 1, NULL);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	rsc_obj_set_cicon(r, 0, 3, NULL, NULL);
	s = S(0, 3, -1, 0, 0);
	CHECK(rsc_image_info(r, &s, &in) == RSC_IMG_ENOIMAGE);
	rsc_free(r);
	CHECK(live() == 0);
}

static void test_get(void)
{
	struct rsc	*r = build();
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s;
	ICONBLK	*ib = rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk;
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 3);
	int	x, y, ok;
	uint64_t	before = fp(r);

	/* the icon: data and mask */
	s = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.w == 20 && pm.h == 5 && pm.planes == 1);
	ok = 1;
	for (y = 0; y < 5; y++)
		for (x = 0; x < 20; x++)
			ok &= pm.pix[y * 20 + x] == rsc_plane_get(ib->ib_pdata, 1, 20, 5, x, y);
	CHECK(ok);
	rsc_pixmap_free(&pm);
	s = S(0, 1, -1, 0, 1);
	CHECK(rsc_image_get(r, &s, &pm) == 0);
	ok = 1;
	for (y = 0; y < 5; y++)
		for (x = 0; x < 20; x++)
			ok &= pm.pix[y * 20 + x] == rsc_plane_get(ib->ib_pmask, 1, 20, 5, x, y);
	CHECK(ok);
	rsc_pixmap_free(&pm);

	/* the G_IMAGE with an odd row length: the bytes of the file */
	s = S(0, 2, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.w == 24 && pm.h == 2);
	ok = 1;
	for (y = 0; y < 2; y++)
		for (x = 0; x < 24; x++)
			ok &= pm.pix[y * 24 + x] == ((bb_bytes[y * 3 + x / 8] >> (7 - x % 8)) & 1);
	CHECK(ok);
	CHECK(pm.pix[0] == 1 && pm.pix[1] == 0 && pm.pix[2] == 1);	/* 0xa5 = 1010 0101 */
	CHECK(pm.pix[24 + 23] == 0 && pm.pix[24 + 20] == 1);		/* 0x3c = 0011 1100 */
	rsc_pixmap_free(&pm);

	/* the blank free image */
	s = S(RSC_IMG_FREE, 1, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.w == 24 && pm.h == 1);
	for (x = 0, ok = 1; x < 24; x++)
		ok &= pm.pix[x] == 0;
	CHECK(ok);
	rsc_pixmap_free(&pm);
	s = S(RSC_IMG_FREE, 0, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.w == 16 && pm.h == 4);
	rsc_pixmap_free(&pm);

	/* colour versions */
	s = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 4);
	{
		struct rsc_cicon_version	*v = rsc_cicon_version(c, 0);

		ok = 1;
		for (y = 0; y < 5; y++)
			for (x = 0; x < 20; x++)
				ok &= pm.pix[y * 20 + x] == rsc_plane_get(v->c.col_data, 4, 20, 5, x, y);
		CHECK(ok);
	}
	rsc_pixmap_free(&pm);
	s = S(0, 3, 0, 1, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 4);
	CHECK(pm.pix[7] == rsc_plane_get(rsc_cicon_version(c, 0)->c.sel_data, 4, 20, 5, 7, 0));
	rsc_pixmap_free(&pm);
	s = S(0, 3, 1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 8);
	rsc_pixmap_free(&pm);
	s = S(0, 3, 1, 0, 1);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 1);
	rsc_pixmap_free(&pm);
	s = S(0, 3, 1, 1, 0);
	CHECK(rsc_image_get(r, &s, &pm) == RSC_IMG_ENOIMAGE && pm.pix == NULL);
	s = S(0, 3, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 1);
	rsc_pixmap_free(&pm);
	/* the 3 plane version: the mask is there, the data has no palette */
	s = S(0, 3, 2, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == RSC_IMG_ENODEPTH && pm.pix == NULL);
	s = S(0, 3, 2, 0, 1);
	CHECK(rsc_image_get(r, &s, &pm) == 0 && pm.planes == 1);
	rsc_pixmap_free(&pm);
	s = S(0, 2, -1, 0, 1);
	CHECK(rsc_image_get(r, &s, &pm) == RSC_IMG_ENOMASK);
	s = S(0, 4, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_image_get(r, &s, NULL) == RSC_IMG_EARG);
	/* the model is unchanged */
	CHECK(fp(r) == before);
	rsc_free(r);
	CHECK(live() == 0);
}

static void test_set(void)
{
	struct rsc	*r = build();
	struct rsc_pixmap	pm, pm2, bad;
	struct rsc_imgsel	s;
	ICONBLK	*ib = rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk;
	char	*text = ib->ib_ptext;
	uint64_t	before = fp(r), mid;
	long	blocks = live();
	int	x, y, ok;

	/* the icon data: flip one pixel, the rest stays */
	s = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0);
	pm.pix[2 * 20 + 3] ^= 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	mid = fp(r);
	CHECK(mid != before);
	CHECK(ib == rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk && ib->ib_ptext == text);
	CHECK(ib->ib_wicon == 20 && ib->ib_hicon == 5);
	CHECK(live() == blocks + 1);		/* the pixmap */
	CHECK(rsc_image_get(r, &s, &pm2) == 0 && pm_equal(&pm, &pm2));
	rsc_pixmap_free(&pm2);
	/* the mask did not change */
	{
		short	m[ICON_WORDS];

		words(m, ICON_WORDS, 2);
		norm(m, 1, ICON_W, ICON_H);
		CHECK(memcmp(ib->ib_pmask, m, sizeof m) == 0);
	}
	pm.pix[2 * 20 + 3] ^= 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	CHECK(fp(r) == before);			/* back to the start */
	rsc_pixmap_free(&pm);

	/* the mask */
	s = S(0, 1, -1, 0, 1);
	CHECK(rsc_image_get(r, &s, &pm) == 0);
	for (x = 0; x < 100; x++)
		pm.pix[x] = (unsigned char)(x & 1);
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	for (y = 0, ok = 1; y < 5; y++)
		for (x = 0; x < 20; x++)
			ok &= rsc_plane_get(ib->ib_pmask, 1, 20, 5, x, y) == (unsigned)((y * 20 + x) & 1);
	CHECK(ok);
	rsc_pixmap_free(&pm);

	/* errors leave everything as it was */
	mid = fp(r);
	s = S(0, 1, -1, 0, 0);
	CHECK(rsc_pixmap_new(&bad, 21, 5, 1) == 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ESIZE);
	rsc_pixmap_free(&bad);
	CHECK(rsc_pixmap_new(&bad, 20, 4, 1) == 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ESIZE);
	rsc_pixmap_free(&bad);
	CHECK(rsc_pixmap_new(&bad, 20, 5, 2) == 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_EARG);
	s = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_EARG);	/* 2 planes into a 4 plane image */
	rsc_pixmap_free(&bad);
	CHECK(rsc_image_set(r, &s, NULL) == RSC_IMG_EARG);
	memset(&bad, 0, sizeof bad);
	bad.w = 20;
	bad.h = 5;
	bad.planes = 4;
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_EARG);	/* pix NULL */
	CHECK(rsc_pixmap_new(&bad, 20, 5, 4) == 0);
	s = S(0, 3, 2, 0, 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_EARG);	/* the 3 plane version: 4 against 3 */
	rsc_pixmap_free(&bad);
	CHECK(rsc_pixmap_new(&bad, 20, 5, 3) == 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ENODEPTH);
	rsc_pixmap_free(&bad);
	CHECK(rsc_pixmap_new(&bad, 24, 2, 1) == 0);
	s = S(0, 2, -1, 0, 1);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ENOMASK);
	s = S(0, 4, -1, 0, 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ENOIMAGE);
	s = S(0, 3, 5, 0, 0);
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ENOIMAGE);
	s = S(0, 3, 1, 1, 0);		/* a size mismatch is checked, not creating */
	CHECK(rsc_image_set(r, &s, &bad) == RSC_IMG_ESIZE);
	rsc_pixmap_free(&bad);
	CHECK(fp(r) == mid);

	/* the G_IMAGE with an odd row length: set, read the bytes back */
	s = S(0, 2, -1, 0, 0);
	CHECK(rsc_image_get(r, &s, &pm) == 0);
	for (x = 0; x < 48; x++)
		pm.pix[x] = (unsigned char)((x * 5 + x / 7) & 1);
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	{
		BITBLK	*bb = rsc_tree_objects(r, 0, NULL)[2].ob_spec.bitblk;
		unsigned char	*raw = (unsigned char *)bb->bi_pdata;
		size_t	b;

		CHECK(bb->bi_wb == 3 && bb->bi_hl == 2);
		for (b = 0, ok = 1; b < 6; b++)
		{
			unsigned	e = 0, got;
			int	k;

			for (k = 0; k < 8; k++)
				e |= (unsigned)pm.pix[(b / 3) * 24 + (b % 3) * 8 + (size_t)k] << (7 - k);
			{
				unsigned short	w;

				memcpy(&w, raw + (b & ~(size_t)1), 2);
				got = (b & 1) ? (w & 0xffu) : (unsigned)(w >> 8);
			}
			ok &= got == e;
		}
		CHECK(ok);
	}
	CHECK(rsc_image_get(r, &s, &pm2) == 0 && pm_equal(&pm, &pm2));
	rsc_pixmap_free(&pm2);
	rsc_pixmap_free(&pm);

	/* a blank free image gets its block */
	s = S(RSC_IMG_FREE, 1, -1, 0, 0);
	CHECK(rsc_pixmap_new(&pm, 24, 1, 1) == 0);
	pm.pix[5] = 1;
	pm.pix[23] = 1;
	blocks = live();
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	CHECK(live() == blocks + 1);
	CHECK(rsc_frimg(r, 1)->bi_pdata != NULL);
	CHECK(rsc_image_get(r, &s, &pm2) == 0 && pm_equal(&pm, &pm2));
	rsc_pixmap_free(&pm2);
	rsc_pixmap_free(&pm);
	{
		/* 3 bytes per row: the third byte is the odd last byte of the block, kept as it is */
		unsigned char	*raw = (unsigned char *)rsc_frimg(r, 1)->bi_pdata;
		unsigned short	w;

		memcpy(&w, raw, 2);
		CHECK(w == 0x0400 && raw[2] == 0x01);
	}
	s = S(RSC_IMG_FREE, 0, -1, 0, 0);
	CHECK(rsc_pixmap_new(&pm, 16, 4, 1) == 0);
	pm.pix[16 * 3 + 15] = 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	{
		unsigned char	*raw = (unsigned char *)rsc_frimg(r, 0)->bi_pdata;
		unsigned short	w;

		memcpy(&w, raw + 6, 2);
		CHECK(w == 1);			/* the last row's word, pixel 15 = bit 0 */
		memcpy(&w, raw, 2);
		CHECK(w == 0);
	}
	rsc_pixmap_free(&pm);

	/* a G_ICON with a blank mask: set creates it */
	{
		ICONBLK	b2;

		memset(&b2, 0, sizeof b2);
		b2.ib_wicon = 16;
		b2.ib_hicon = 2;
		rsc_obj_add(r, 0, 0, G_ICON);
		rsc_obj_set_iconblk(r, 0, 5, &b2);
		s = S(0, 5, -1, 0, 1);
		{
			struct rsc_imginfo	ii;

			CHECK(rsc_image_info(r, &s, &ii) == 0 && ii.blank == 1);
		}
		CHECK(rsc_pixmap_new(&pm, 16, 2, 1) == 0);
		pm.pix[17] = 1;
		blocks = live();
		CHECK(rsc_image_set(r, &s, &pm) == 0 && live() == blocks + 1);
		CHECK(rsc_tree_objects(r, 0, NULL)[5].ob_spec.iconblk->ib_pdata == NULL);
		rsc_pixmap_free(&pm);
	}
	rsc_free(r);
	CHECK(live() == 0);
}

static void test_versions(void)
{
	struct rsc	*r = build();
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 3);
	struct rsc_pixmap	pm, pm2;
	struct rsc_imgsel	s;
	struct rsc_cicon_version	*v;
	uint64_t	before;
	short	m[ICON_WORDS], cm[ICON_WORDS];
	int	k;
	long	blocks;

	words(m, ICON_WORDS, 4);		/* the mono mask */
	norm(m, 1, ICON_W, ICON_H);
	words(cm, ICON_WORDS, 6);		/* the mask of versions 0 and 1 */
	before = fp(r);

	/* find_version */
	CHECK(rsc_cicon_find_version(c, 4) == 0 && rsc_cicon_find_version(c, 8) == 1
	      && rsc_cicon_find_version(c, 3) == 2 && rsc_cicon_find_version(c, 2) == -1
	      && rsc_cicon_find_version(c, 0) == -1 && rsc_cicon_find_version(NULL, 4) == -1);

	/* a new version by set: the data given, the mask copied from the mono mask */
	s = SN(0, 3, 2, 0);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 2) == 0);
	pm.pix[0] = 1;
	pm.pix[99] = 3;
	pm.pix[40] = 2;
	blocks = live();
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	CHECK(rsc_cicon_nversions(c) == 4);
	k = rsc_cicon_find_version(c, 2);
	CHECK(k == 3);
	v = rsc_cicon_version(c, k);
	CHECK(v->c.num_planes == 2 && v->c.sel_data == NULL && v->c.sel_mask == NULL);
	CHECK(memcmp(v->c.col_mask, m, sizeof m) == 0);
	CHECK(rsc_plane_get(v->c.col_data, 2, 20, 5, 0, 0) == 1);
	CHECK(rsc_plane_get(v->c.col_data, 2, 20, 5, 19, 4) == 3);
	CHECK(rsc_plane_get(v->c.col_data, 2, 20, 5, 0, 2) == 2);
	CHECK(rsc_plane_get(v->c.col_data, 2, 20, 5, 1, 0) == 0);
	CHECK(v->data_bytes == 40 && v->file_ptr[0] == 0);
	CHECK(live() == blocks + 3);		/* the version and its two blocks */
	CHECK(rsc_validate(r, NULL) == 0);
	/* it exists now */
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_EEXIST);
	rsc_pixmap_free(&pm);
	CHECK(rsc_cicon_new_version(c, 2) == RSC_IMG_EEXIST);

	/* a new version by its mask: blank data */
	s = SN(0, 3, 4, 1);
	CHECK(rsc_cicon_remove_version(c, 0) == 0);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 1) == 0);
	pm.pix[1] = 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	k = rsc_cicon_find_version(c, 4);
	v = rsc_cicon_version(c, k);
	CHECK(k == rsc_cicon_nversions(c) - 1);
	CHECK(rsc_plane_get(v->c.col_mask, 1, 20, 5, 1, 0) == 1 && rsc_plane_get(v->c.col_mask, 1, 20, 5, 0, 0) == 0);
	{
		int	i, ok = 1;

		for (i = 0; i < 40; i++)
			ok &= v->c.col_data[i] == 0;
		CHECK(ok);
	}
	rsc_pixmap_free(&pm);

	/* wrong depth, size */
	s = SN(0, 3, 3, 0);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 3) == 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENODEPTH);
	rsc_pixmap_free(&pm);
	s = SN(0, 3, 5, 0);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 4) == 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENODEPTH);
	s = SN(0, 3, 16, 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENODEPTH);
	s = SN(0, 3, 0, 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENODEPTH);
	s = SN(0, 3, -4, 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENODEPTH);
	s = SN(0, 1, 2, 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENOIMAGE);	/* a G_ICON has no versions */
	s = SN(0, 3, 8, 0);
	s.sel = 1;
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_ENOIMAGE);
	rsc_pixmap_free(&pm);
	CHECK(rsc_pixmap_new(&pm, 19, 5, 8) == 0);
	s = SN(0, 3, 8, 0);
	CHECK(rsc_image_set(r, &s, &pm) == RSC_IMG_EEXIST);	/* exists: checked before the size */
	rsc_pixmap_free(&pm);

	/* the selected images are created by set: the other half is copied */
	CHECK(rsc_cicon_find_version(c, 8) >= 0);
	k = rsc_cicon_find_version(c, 8);
	s = S(0, 3, k, 1, 0);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 8) == 0);
	pm.pix[10] = 200;
	blocks = live();
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	CHECK(live() == blocks + 2);
	v = rsc_cicon_version(c, k);
	CHECK(v->c.sel_data && v->c.sel_mask);
	CHECK(rsc_plane_get(v->c.sel_data, 8, 20, 5, 10, 0) == 200);
	CHECK(memcmp(v->c.sel_mask, v->c.col_mask, sizeof m) == 0 && v->c.sel_mask != v->c.col_mask);
	rsc_pixmap_free(&pm);
	/* and by the mask */
	k = rsc_cicon_find_version(c, 2);
	s = S(0, 3, k, 1, 1);
	CHECK(rsc_pixmap_new(&pm, 20, 5, 1) == 0);
	pm.pix[0] = 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	v = rsc_cicon_version(c, k);
	CHECK(rsc_plane_get(v->c.sel_mask, 1, 20, 5, 0, 0) == 1);
	CHECK(memcmp(v->c.sel_data, v->c.col_data, 40) == 0 && v->c.sel_data != v->c.col_data);
	rsc_pixmap_free(&pm);
	CHECK(rsc_validate(r, NULL) == 0);

	/* rsc_cicon_new_version, add_selected, drop_selected */
	CHECK(rsc_cicon_new_version(NULL, 2) == RSC_IMG_EARG);
	CHECK(rsc_cicon_new_version(c, 3) == RSC_IMG_ENODEPTH);
	CHECK(rsc_cicon_new_version(c, 1) == RSC_IMG_ENODEPTH);
	CHECK(rsc_cicon_new_version(c, 16) == RSC_IMG_ENODEPTH);
	CHECK(rsc_cicon_new_version(c, 8) == RSC_IMG_EEXIST);
	CHECK(rsc_cicon_remove_version(c, rsc_cicon_find_version(c, 8)) == 0);
	blocks = live();
	k = rsc_cicon_new_version(c, 8);
	CHECK(k == rsc_cicon_nversions(c) - 1);
	CHECK(live() == blocks + 3);
	v = rsc_cicon_version(c, k);
	CHECK(v->c.num_planes == 8 && v->data_bytes == 160 && memcmp(v->c.col_mask, m, sizeof m) == 0);
	CHECK(v->c.col_mask != c->blk.monoblk.ib_pmask);
	{
		int	i, ok = 1;

		for (i = 0; i < 80; i++)
			ok &= v->c.col_data[i] == 0;
		CHECK(ok);
	}
	CHECK(rsc_cicon_add_selected(c, k) == 0);
	CHECK(v->c.sel_data && v->c.sel_mask && v->c.sel_data != v->c.col_data);
	CHECK(memcmp(v->c.sel_data, v->c.col_data, 160) == 0 && memcmp(v->c.sel_mask, v->c.col_mask, 20) == 0);
	CHECK(rsc_cicon_add_selected(c, k) == RSC_IMG_EEXIST);
	CHECK(rsc_cicon_add_selected(c, 99) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_cicon_add_selected(c, -1) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_cicon_add_selected(NULL, 0) == RSC_IMG_ENOIMAGE);
	CHECK(live() == blocks + 5);
	CHECK(rsc_cicon_drop_selected(c, k) == 0);
	CHECK(v->c.sel_data == NULL && v->c.sel_mask == NULL && live() == blocks + 3);
	CHECK(rsc_cicon_drop_selected(c, k) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_cicon_drop_selected(c, 99) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_cicon_drop_selected(c, -1) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_cicon_drop_selected(NULL, 0) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_validate(r, NULL) == 0);
	(void)before;
	(void)cm;
	rsc_free(r);
	CHECK(live() == 0);

	/* the opaque file fields stay when an image of a version is set */
	r = build();
	c = rsc_obj_cicon(r, 0, 3);
	v = rsc_cicon_version(c, 0);
	v->file_ptr[0] = 77;
	v->file_ptr[4] = 88;
	s = S(0, 3, 0, 0, 0);
	rsc_image_get(r, &s, &pm);
	pm.pix[3] ^= 1;
	CHECK(rsc_image_set(r, &s, &pm) == 0);
	CHECK(v->file_ptr[0] == 77 && v->file_ptr[4] == 88);
	rsc_pixmap_free(&pm);
	rsc_free(r);

	/* the limit of versions */
	{
		struct rsc	*r2 = rsc_new();
		ICONBLK	ib;
		short	z[2];
		int	i;

		memset(&ib, 0, sizeof ib);
		memset(z, 0, sizeof z);
		ib.ib_wicon = 16;
		ib.ib_hicon = 1;
		rsc_tree_add(r2, 1);
		rsc_obj_add(r2, 0, 0, G_CICON);
		rsc_obj_set_cicon(r2, 0, 1, &ib, "L");
		c = rsc_obj_cicon(r2, 0, 1);
		for (i = 0; i < RSC_MAX_VERSIONS; i++)
		{
			short	zz[3];

			memset(zz, 0, sizeof zz);
			CHECK(rsc_cicon_add_version(c, 3, zz, z, NULL, NULL) == i);
		}
		CHECK(rsc_cicon_new_version(c, 2) == RSC_IMG_ELIMIT);
		s = SN(0, 1, 2, 0);
		CHECK(rsc_pixmap_new(&pm, 16, 1, 2) == 0);
		CHECK(rsc_image_set(r2, &s, &pm) == RSC_IMG_ELIMIT);
		CHECK(rsc_cicon_nversions(c) == RSC_MAX_VERSIONS);
		rsc_pixmap_free(&pm);
		rsc_free(r2);
	}
	CHECK(live() == 0);
}

static void test_copy(void)
{
	struct rsc	*r = build();
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 3);
	struct rsc_pixmap	a, b, e;
	struct rsc_imgsel	from, to;
	struct rsc_cicon_version	*v;
	uint64_t	before;
	long	inex = -1;
	short	m[ICON_WORDS];
	int	x, y, ok, k;

	words(m, ICON_WORDS, 2);		/* the mask of the G_ICON */
	norm(m, 1, ICON_W, ICON_H);

	/* mono to a colour version, mask included: set pixel -> black, clear -> white */
	from = S(0, 1, -1, 0, 0);
	to = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == 0 && inex == 0);
	v = rsc_cicon_version(c, 0);
	CHECK(v->c.num_planes == 4);
	for (y = 0, ok = 1; y < 5; y++)
		for (x = 0; x < 20; x++)
		{
			unsigned	e1 = rsc_plane_get(rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pdata, 1, 20, 5, x, y);

			ok &= rsc_plane_get(v->c.col_data, 4, 20, 5, x, y) == (e1 ? 15u : 0u);
		}
	CHECK(ok);
	CHECK(memcmp(v->c.col_mask, m, sizeof m) == 0);
	CHECK(v->c.sel_data != NULL);		/* the selected image is not touched */

	/* data only: the mask stays */
	{
		short	cm[ICON_WORDS];

		words(cm, ICON_WORDS, 6);
		norm(cm, 1, ICON_W, ICON_H);
		to = S(0, 3, 1, 0, 0);
		CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
		v = rsc_cicon_version(c, 1);
		CHECK(memcmp(v->c.col_mask, cm, sizeof cm) == 0);
		CHECK(rsc_plane_get(v->c.col_data, 8, 20, 5, 0, 0) == (rsc_plane_get(rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pdata, 1, 20, 5, 0, 0) ? 255u : 0u));
	}

	/* 4 planes to 8 planes by the nearest colour */
	from = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_get(r, &from, &a) == 0);
	to = S(0, 3, 1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
	CHECK(rsc_image_get(r, &to, &b) == 0);
	CHECK(rsc_pixmap_convert(&a, 8, &e, NULL) == 0 && pm_equal(&e, &b));
	rsc_pixmap_free(&a);
	rsc_pixmap_free(&b);
	rsc_pixmap_free(&e);

	/* N to N.sel: the same depth is a copy; the mask: copied to the selected mask */
	from = S(0, 3, 0, 0, 0);
	to = S(0, 3, 0, 1, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == 0 && inex == 0);
	v = rsc_cicon_version(c, 0);
	CHECK(memcmp(v->c.sel_data, v->c.col_data, 160 / 2) == 0);
	CHECK(memcmp(v->c.sel_mask, v->c.col_mask, 20) == 0);

	/* colour to mono, to a G_ICON, and the mask copy flag with a mask selector */
	to = S(0, 1, -1, 0, 0);
	from = S(0, 3, 1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
	CHECK(rsc_image_get(r, &from, &a) == 0 && rsc_image_get(r, &to, &b) == 0);
	CHECK(rsc_pixmap_convert(&a, 1, &e, NULL) == 0 && pm_equal(&e, &b));
	rsc_pixmap_free(&a);
	rsc_pixmap_free(&b);
	rsc_pixmap_free(&e);
	from = S(0, 3, 0, 0, 1);
	to = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == RSC_IMG_EARG);
	to = S(0, 1, -1, 0, 1);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);	/* a mask to a mask */
	CHECK(memcmp(rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pmask,
		     rsc_cicon_version(c, 0)->c.col_mask, 20) == 0);

	/* into a G_IMAGE or a free image: sizes must be equal */
	from = S(0, 1, -1, 0, 0);
	to = S(0, 2, -1, 0, 0);
	before = fp(r);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == RSC_IMG_ESIZE);
	to = S(RSC_IMG_FREE, 0, -1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == RSC_IMG_ESIZE);
	CHECK(fp(r) == before);
	/* the same image */
	CHECK(rsc_image_copy(r, &from, &from, 0, &inex) == RSC_IMG_EARG);
	to = S(0, 3, -1, 0, 0);
	CHECK(rsc_image_copy(r, &to, &to, 0, &inex) == RSC_IMG_EARG);
	/* depths without a palette */
	from = S(0, 3, 2, 0, 0);
	to = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == RSC_IMG_ENODEPTH);
	CHECK(rsc_image_copy(r, &to, &from, 0, &inex) == RSC_IMG_ENODEPTH);
	from = S(0, 3, 2, 0, 1);		/* the mask of that version is a mask */
	to = S(0, 1, -1, 0, 1);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
	/* other errors */
	from = S(0, 1, -1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 2, &inex) == RSC_IMG_EARG);
	CHECK(rsc_image_copy(r, NULL, &to, 0, &inex) == RSC_IMG_EARG);
	CHECK(rsc_image_copy(r, &from, NULL, 0, &inex) == RSC_IMG_EARG);
	CHECK(rsc_image_copy(NULL, &from, &to, 0, &inex) == RSC_IMG_EARG);
	to = S(0, 4, -1, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == RSC_IMG_ENOIMAGE);
	CHECK(rsc_image_copy(r, &to, &from, 0, &inex) == RSC_IMG_ENOIMAGE);
	to = S(0, 2, -1, 0, 1);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == RSC_IMG_ENOMASK);
	CHECK(rsc_image_copy(r, &to, &from, 0, &inex) == RSC_IMG_ENOMASK);

	/* a new version: from the mono icon, mask copied, depth by the selector */
	rsc_cicon_remove_version(c, rsc_cicon_find_version(c, 8));
	from = S(0, 1, -1, 0, 0);
	to = SN(0, 3, 8, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == 0 && inex == 0);
	k = rsc_cicon_find_version(c, 8);
	CHECK(k >= 0);
	v = rsc_cicon_version(c, k);
	CHECK(memcmp(v->c.col_mask, rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pmask, sizeof m) == 0);
	CHECK(rsc_plane_get(v->c.col_data, 8, 20, 5, 0, 0) ==
	      (rsc_plane_get(rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pdata, 1, 20, 5, 0, 0) ? 255u : 0u));
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == RSC_IMG_EEXIST);
	/* without the flag the new version has the mono mask of its icon */
	rsc_cicon_remove_version(c, k);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
	v = rsc_cicon_version(c, rsc_cicon_find_version(c, 8));
	{
		short	mm[ICON_WORDS];

		words(mm, ICON_WORDS, 4);
		norm(mm, 1, ICON_W, ICON_H);
		CHECK(memcmp(v->c.col_mask, mm, sizeof mm) == 0);
	}
	/* a new version from a colour one: 4 to 2 planes, losing colours */
	to = SN(0, 3, 2, 0);
	from = S(0, 3, 0, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == 0);
	CHECK(inex >= 0);
	CHECK(rsc_image_get(r, &from, &a) == 0);
	CHECK(rsc_pixmap_convert(&a, 2, &e, &inex) == 0);
	to = S(0, 3, rsc_cicon_find_version(c, 2), 0, 0);
	CHECK(rsc_image_get(r, &to, &b) == 0 && pm_equal(&e, &b));
	rsc_pixmap_free(&a);
	rsc_pixmap_free(&b);
	rsc_pixmap_free(&e);
	/* a new version by the mask of an image */
	rsc_cicon_remove_version(c, rsc_cicon_find_version(c, 4));
	from = S(0, 1, -1, 0, 1);
	to = SN(0, 3, 4, 1);
	CHECK(rsc_image_copy(r, &from, &to, 0, &inex) == 0);
	k = rsc_cicon_find_version(c, 4);
	CHECK(k >= 0);
	CHECK(memcmp(rsc_cicon_version(c, k)->c.col_mask, rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pmask,
		     20) == 0);
	/* to the selected image that does not exist yet */
	k = rsc_cicon_find_version(c, 8);
	to = S(0, 3, k, 1, 0);
	from = S(0, 3, k, 0, 0);
	CHECK(rsc_image_copy(r, &from, &to, RSC_COPY_MASK, &inex) == 0);
	v = rsc_cicon_version(c, k);
	CHECK(v->c.sel_data && v->c.sel_mask);
	CHECK(memcmp(v->c.sel_data, v->c.col_data, 160) == 0 && memcmp(v->c.sel_mask, v->c.col_mask, 20) == 0);
	CHECK(rsc_validate(r, NULL) == 0);
	rsc_free(r);
	CHECK(live() == 0);
}

/* ---- allocation failure injection ---------------------------------------------------------- */

typedef int (*opfn)(struct rsc *);

static long injected;

/* Runs op on fresh models with every allocation number failing in turn:
 * a failed call is RSC_IMG_ENOMEM and leaves the model and the count of
 * blocks as they were; the first call that succeeds is checked by verify. */
static void inject(const char *name, opfn op, opfn verify)
{
	long	n;

	for (n = 0; n < 300; n++)
	{
		struct rsc	*r = build();
		uint64_t	before = fp(r);
		long	live0 = live();
		int	err;

		rsc_test_fail_alloc_after(n);
		err = op(r);
		rsc_test_fail_alloc_after(-1);
		if (err == RSC_IMG_ENOMEM)
		{
			injected++;
			CHECK(fp(r) == before);
			CHECK(live() == live0);
			CHECK(rsc_validate(r, NULL) == 0);
		}
		else
		{
			CHECK(err == 0);
			CHECK(verify(r) == 0);
			CHECK(rsc_validate(r, NULL) == 0);
			rsc_free(r);
			CHECK(live() == 0);
			break;
		}
		rsc_free(r);
		CHECK(live() == 0);
	}
	if (n < 1 || n >= 300)
		fprintf(stderr, "inject %s: %ld\n", name, n);
	CHECK(n >= 1 && n < 300);
}

static int pm_set_pix(struct rsc_pixmap *pm, int w, int h, int planes, int at, int v)
{
	int	e = rsc_pixmap_new(pm, w, h, planes);

	if (e == 0)
		pm->pix[at] = (unsigned char)v;
	return e;
}

static int op_set_icon(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 1, -1, 0, 0);
	int	e = pm_set_pix(&pm, 20, 5, 1, 41, 1);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_icon(struct rsc *r)
{
	return rsc_plane_get(rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pdata, 1, 20, 5, 1, 2) == 1 ? 0 : 1;
}

static int op_set_bitblk(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 2, -1, 0, 0);
	int	e = pm_set_pix(&pm, 24, 2, 1, 30, 1);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_bitblk(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 2, -1, 0, 0);
	int	e = rsc_image_get(r, &s, &pm);

	if (e == 0)
		e = !(pm.pix[30] == 1 && pm.pix[29] == 0 && pm.pix[31] == 0);
	rsc_pixmap_free(&pm);
	return e;
}

static int op_set_free(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(RSC_IMG_FREE, 1, -1, 0, 0);
	int	e = pm_set_pix(&pm, 24, 1, 1, 7, 1);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_free(struct rsc *r)
{
	return rsc_frimg(r, 1)->bi_pdata == NULL;
}

static int op_set_mono_mask(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 3, -1, 0, 1);
	int	e = pm_set_pix(&pm, 20, 5, 1, 5, 1);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_mono_mask(struct rsc *r)
{
	return rsc_plane_get(rsc_obj_cicon(r, 0, 3)->blk.monoblk.ib_pmask, 1, 20, 5, 5, 0) == 1 ? 0 : 1;
}

static int op_set_version(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 3, 0, 0, 0);
	int	e = pm_set_pix(&pm, 20, 5, 4, 9, 13);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_version(struct rsc *r)
{
	return rsc_plane_get(rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 0)->c.col_data, 4, 20, 5, 9, 0) == 13 ? 0 : 1;
}

static int op_create_sel(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 3, 1, 1, 0);
	int	e = pm_set_pix(&pm, 20, 5, 8, 3, 99);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_create_sel(struct rsc *r)
{
	struct rsc_cicon_version	*v = rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 1);

	return v->c.sel_data && v->c.sel_mask && rsc_plane_get(v->c.sel_data, 8, 20, 5, 3, 0) == 99 ? 0 : 1;
}

static int op_new_version(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = SN(0, 3, 2, 0);
	int	e = pm_set_pix(&pm, 20, 5, 2, 8, 2);

	if (e == 0)
		e = rsc_image_set(r, &s, &pm);
	rsc_pixmap_free(&pm);
	return e;
}

static int ver_new_version(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 3);
	int	k = rsc_cicon_find_version(c, 2);

	return k == 3 && rsc_plane_get(rsc_cicon_version(c, k)->c.col_data, 2, 20, 5, 8, 0) == 2 ? 0 : 1;
}

static int op_copy_new(struct rsc *r)
{
	struct rsc_imgsel	from = S(0, 3, 0, 0, 0), to = SN(0, 3, 2, 0);

	return rsc_image_copy(r, &from, &to, RSC_COPY_MASK, NULL);
}

static int ver_copy_new(struct rsc *r)
{
	struct rsc_cicon	*c = rsc_obj_cicon(r, 0, 3);

	return rsc_cicon_find_version(c, 2) == 3 ? 0 : 1;
}

static int op_copy_both(struct rsc *r)
{
	struct rsc_imgsel	from = S(0, 1, -1, 0, 0), to = S(0, 3, 1, 1, 0);

	return rsc_image_copy(r, &from, &to, RSC_COPY_MASK, NULL);
}

static int ver_copy_both(struct rsc *r)
{
	struct rsc_cicon_version	*v = rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 1);

	return v->c.sel_data && v->c.sel_mask
	       && memcmp(v->c.sel_mask, rsc_tree_objects(r, 0, NULL)[1].ob_spec.iconblk->ib_pmask, 20) == 0 ? 0 : 1;
}

static int op_copy_plain(struct rsc *r)
{
	struct rsc_imgsel	from = S(0, 3, 0, 0, 0), to = S(0, 3, 1, 0, 0);

	return rsc_image_copy(r, &from, &to, RSC_COPY_MASK, NULL);
}

static int ver_copy_plain(struct rsc *r)
{
	return memcmp(rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 1)->c.col_mask,
		      rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 0)->c.col_mask, 20) == 0 ? 0 : 1;
}

static int op_new_blank(struct rsc *r)
{
	return rsc_cicon_new_version(rsc_obj_cicon(r, 0, 3), 2) >= 0 ? 0 : RSC_IMG_ENOMEM;
}

static int ver_new_blank(struct rsc *r)
{
	return rsc_cicon_find_version(rsc_obj_cicon(r, 0, 3), 2) == 3 ? 0 : 1;
}

static int op_add_sel(struct rsc *r)
{
	return rsc_cicon_add_selected(rsc_obj_cicon(r, 0, 3), 1);
}

static int ver_add_sel(struct rsc *r)
{
	struct rsc_cicon_version	*v = rsc_cicon_version(rsc_obj_cicon(r, 0, 3), 1);

	return v->c.sel_data && v->c.sel_mask ? 0 : 1;
}

static int op_get(struct rsc *r)
{
	struct rsc_pixmap	pm;
	struct rsc_imgsel	s = S(0, 2, -1, 0, 0);
	int	e = rsc_image_get(r, &s, &pm);

	CHECK(e == 0 || (pm.pix == NULL && pm.w == 0));
	rsc_pixmap_free(&pm);
	return e;
}

static int op_none(struct rsc *r)
{
	(void)r;
	return 0;
}

static void test_inject(void)
{
	inject("set icon", op_set_icon, ver_icon);
	inject("set bitblk", op_set_bitblk, ver_bitblk);
	inject("set free", op_set_free, ver_free);
	inject("set mono mask", op_set_mono_mask, ver_mono_mask);
	inject("set version", op_set_version, ver_version);
	inject("create sel", op_create_sel, ver_create_sel);
	inject("new version", op_new_version, ver_new_version);
	inject("copy to new", op_copy_new, ver_copy_new);
	inject("copy both", op_copy_both, ver_copy_both);
	inject("copy plain", op_copy_plain, ver_copy_plain);
	inject("new blank", op_new_blank, ver_new_blank);
	inject("add selected", op_add_sel, ver_add_sel);
	inject("get", op_get, op_none);
	CHECK(injected > 30);
}

/* ---- through the file ------------------------------------------------------------------------ */

static void test_via_file(void)
{
	struct rsc	*r = build(), *r2;
	struct rsc_pixmap	a, b;
	struct rsc_imgsel	s;
	uint8_t	*buf;
	size_t	len;
	struct rsc_error	err;
	int	k, e;
	static const struct { int tree, obj, ver, sel, mask; } sels[] = {
		{ 0, 1, -1, 0, 0 }, { 0, 1, -1, 0, 1 }, { 0, 2, -1, 0, 0 }, { RSC_IMG_FREE, 0, -1, 0, 0 },
		{ 0, 3, -1, 0, 0 }, { 0, 3, -1, 0, 1 }, { 0, 3, 0, 0, 0 }, { 0, 3, 0, 1, 0 }, { 0, 3, 0, 1, 1 },
		{ 0, 3, 1, 0, 0 }, { 0, 3, 1, 0, 1 }
	};

	/* change them all, write, read, compare */
	for (k = 0; k < (int)(sizeof sels / sizeof sels[0]); k++)
	{
		s = S(sels[k].tree, sels[k].obj, sels[k].ver, sels[k].sel, sels[k].mask);
		CHECK(rsc_image_get(r, &s, &a) == 0);
		a.pix[a.w * a.h / 2 + 1] = (unsigned char)((1u << a.planes) - 1);
		a.pix[a.w * a.h - 1] = 1;
		a.pix[0] = (unsigned char)(1u << (a.planes - 1));
		CHECK(rsc_image_set(r, &s, &a) == 0);
		rsc_pixmap_free(&a);
	}
	e = rsc_atari_write(r, 0, 0, &buf, &len, &err);
	CHECK(e == 0);
	r2 = rsc_atari_read(buf, len, 0, 0, &err);
	CHECK(r2 != NULL);
	if (r2)
	{
		for (k = 0; k < (int)(sizeof sels / sizeof sels[0]); k++)
		{
			s = S(sels[k].tree, sels[k].obj, sels[k].ver, sels[k].sel, sels[k].mask);
			CHECK(rsc_image_get(r, &s, &a) == 0);
			CHECK(rsc_image_get(r2, &s, &b) == 0);
			CHECK(pm_equal(&a, &b));
			CHECK(a.pix[0] == (1u << (a.planes - 1)) && a.pix[a.w * a.h - 1] == 1);
			rsc_pixmap_free(&a);
			rsc_pixmap_free(&b);
		}
		/* the whole thing again through PNM files */
		s = S(0, 3, 0, 0, 0);
		rsc_image_get(r2, &s, &a);
		{
			unsigned char	*f;
			size_t	fl;
			struct rsc_pnm	pn;

			CHECK(rsc_image_to_ppm(&a, 0, &f, &fl) == 0);
			CHECK(rsc_image_from_pnm(f, fl, 0, &pn) == 0);
			CHECK(rsc_pnm_to_pixmap(&pn, 4, 0, 1, &b, NULL) == 0);
			CHECK(pm_equal(&a, &b));
			rsc_pixmap_free(&b);
			rsc_pnm_free(&pn);
			rsc_mfree(f);
		}
		rsc_pixmap_free(&a);
		rsc_free(r2);
	}
	rsc_mfree(buf);
	rsc_free(r);
	CHECK(live() == 0);
}

int main(void)
{
	test_palette();
	test_planes_layout();
	test_planes_roundtrip();
	test_pixmap();
	test_convert_depths();
	test_write();
	test_parse_forms();
	test_parse_errors();
	test_parse_hostile();
	test_parse_truncation();
	test_pnm_to_pixmap();
	test_file_roundtrip();
	test_info();
	test_get();
	test_set();
	test_versions();
	test_copy();
	test_inject();
	test_via_file();
	printf("test_image: %ld checks, %ld failures, %ld allocation failures injected\n", checks, failures,
	       injected);
	return failures ? 1 : 0;
}
