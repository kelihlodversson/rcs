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

/* an address held in a table entry or a record field, 4 bytes in the file
 * image: a long on m68k and ARM, so that nothing changes there */
#ifdef RCS_HOST
/* the data of a resource file is 2-aligned, as on the Atari */
typedef unsigned int rentry __attribute__((aligned(2)));
#else
typedef long rentry;
#endif
/* RS_ENTRY keeps the type of sizeof(long) the code used to have (size_t), so
 * that the arithmetic around it compiles to the same code on m68k and ARM */
#define RS_ENTRY	sizeof(rentry)

/* the rentry at an address, and the same read back as a signed long, so
 * that the -1 which means "none" stays -1 on the host */
#define RFIELD(a)	(*(rentry *)RP(a))
#define RGET(a)		((long)(int)RFIELD(a))

#endif
