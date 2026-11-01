#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "rcsaddr.h"

#define ARENA_SIZE	(1UL << 30)

unsigned char *rcs_base;
static unsigned long used = 8;		/* offset 0 is never handed out */

static void init(void)
{
    void *p = mmap(0, ARENA_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        abort();
    rcs_base = p;
}

raddr rcs_alloc(unsigned long bytes)
{
    raddr a;

    if (!rcs_base)
        init();
    bytes = (bytes + 7) & ~7UL;
    if (bytes > ARENA_SIZE - used)
        return 0;
    a = used;
    used += bytes;
    return a;			/* the pages are zeroed by the system */
}

void rcs_release(raddr a)
{
    (void)a;
}
