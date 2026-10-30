/* RCSMONO.H	one-plane images in memory */
#ifndef RCSMONO_H

#define RCSMONO_H

#include <gem.h>

/* The images the editor works on (the data and the mask of an icon, an
 * image, their copies for undo and the clipboard) are one-plane images in
 * the format of the data of an ICONBLK or a BITBLK in memory: a row is a
 * whole number of words, the words are those of the host, the top bit of a
 * word is the left pixel, and a set bit is a pixel that is drawn.  They do not depend on how the screen
 * keeps its pixels (planes, 16 or 32 bits), which is not known to the
 * editor.  An MFDB of such an image has one plane and fd_stand set. */

/* the colour the editor draws the pixels that are set in: the data of an
 * icon is black, the data of an image has the colour of the image */
extern	short	edit_colour(void);

extern	short	is_mono(const MFDB *m);

/* a raster operation (the modes of vro_cpyfm) between two such images; s is
 * not used by the modes that do not read it (0 and 15) */
extern	void	mono_rop(short mode, GRECT *sa, MFDB *s, GRECT *da, MFDB *d);

/* the pixels of the screen in sa that have the colour, as the set pixels of
 * d in da */
extern	void	scrn_to_mono(GRECT *sa, MFDB *d, GRECT *da, short colour);

/* the set pixels of s in sa on the screen in da, in colour on white */
extern	void	mono_to_scrn(GRECT *sa, MFDB *s, GRECT *da, short colour);

/* The screen says what a pixel is with a number (v_get_pixel) that is not
 * the VDI colour index everywhere: it is the pixel value, and on a screen
 * with many colours the index is not known at all.  So the editor finds out
 * how its 16 colours are reported by drawing them once, where the picture is
 * going to be redrawn anyway, and compares with that afterwards. */
extern	void	calibrate_colours(short x, short y);

/* the colour (0-15) of a pixel of the screen; white if it is none of them */
extern	short	pixel_colour(short x, short y);

#endif
