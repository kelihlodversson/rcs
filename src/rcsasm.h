   /* external function declarations from assembler functions */
#ifndef MISCASM_H

#define MISCASM_H



#include <string.h>

#define umul_div(x, y, z)	(short)((long)(x) * (long)(y) / (long)(z))
#define mul_div(x, y, z)	(short)((long)(x) * (long)(y) / (long)(z))
#define B_MOVE(s,l,d)	memcpy((void *)(d),(const void *)(s),l)


#endif
