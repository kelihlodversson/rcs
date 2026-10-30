/* RCSCICON.H	colour icons (G_CICON, CICONBLK) kept through load and save */
#ifndef RCSCICON_H

#define RCSCICON_H

/* bit 2 of the version word of the header: the file has the extension
 * [length of the file, offset of the table of CICONBLKs, 0] at rsh_rssize */
#define RCS_NEW_FORMAT	4

/* RCS edits and draws the monochrome icon of a colour icon, as an ICONBLK of
 * a G_ICON, and keeps what it does not edit, the colour versions of the icon,
 * as they are in the file, until the file is written again. */

/* What is kept of a colour icon: the parts are in the byte order of the file. */
struct cicon_kept {
	unsigned char	*head;		/* the first 12 bytes of its ICONBLK and the number of colour versions */
	unsigned char	*versions;	/* the colour versions of the icon, as in the file */
	long	vlen;
	short	wicon, hicon;		/* the size of the mono icon they fit */
	short	words;			/* words in one plane */
	short	ncolour;		/* number of colour versions */
	short	used;			/* is to be written as a colour icon */
};

/* forget what the last file had */
extern	void	cicon_reset(void);

/* The image of a file, as it was read from disk (it is not yet turned into
 * the form of the host), has its CICONBLKs in a table at table_off. */
extern	void	cicon_scan(long image, long size, unsigned long table_off);

/* the ICONBLK that stands for CICONBLK number index in the table, or 0 */
extern	long	cicon_iconblk(unsigned long index);

/* is this ICONBLK the mono part of a colour icon?  Returns what to keep,
 * or 0 when it is not, or has changed in a way that does not fit the
 * colour versions any more (its size). */
extern	struct cicon_kept	*cicon_kept(long iconblk);

/* bytes the colour icons took in the file that was read */
extern	long	cicon_loaded_bytes(void);

/* choose the icons that are written as colour icons, so that the colour
 * versions take at most budget bytes */
extern	void	cicon_prepare(long budget);

#endif
