/* RCSADDR.H	addresses of the data of a resource file.
 * RCS keeps them as longs.  On m68k and ARM they are addresses; on the host
 * they are offsets into one arena, so that they still fit 32 bits. */
#ifndef RCSADDR_H
#define RCSADDR_H

typedef unsigned long raddr;

#ifdef RCS_HOST
extern unsigned char *rcs_base;
#define RP(a)	((void *)(rcs_base + (raddr)(a)))
#else
#define RP(a)	((void *)(a))
#endif

extern raddr rcs_alloc(unsigned long bytes);
extern void rcs_release(raddr a);

#endif
