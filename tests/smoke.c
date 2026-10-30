/*
 * smoke.c - the smallest program that needs the whole toolchain
 *
 * Links against gemlib and libcmini, to show that the compiler, the two
 * libraries and the startup code of a target work together.  It is not run.
 * The sources of RCS replace it in the Makefile as they are ported.
 */
#include <gem.h>

int main(void)
{
    if (appl_init() < 0)
        return 1;
    form_alert(1, "[1][RCS build smoke test][OK]");
    appl_exit();
    return 0;
}
