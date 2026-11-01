/* Host stand-in for <stdlib.h>.  On a hosted system stdlib.h declares the
 * POSIX wait(), which clashes with the GRECT variable wait of rcsdefs.h.  The
 * libc declaration is read under another name, then the real header is done. */
#ifndef HOST_STDLIB_H
#define HOST_STDLIB_H
#define wait rcs_posix_wait
#include_next <stdlib.h>
#undef wait
#endif
