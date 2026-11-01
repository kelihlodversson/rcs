#include <gem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rcs.h"
#include "rcsaddr.h"
#include "rcsdata.h"
#include "rcsfiles.h"
#include "rcsdefs.h"
#include "rcsread.h"
#include "rcswrite.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: roundtrip in.rsc out.rsc\n");
        return 2;
    }
    /* rcs_rfile and rcs_dfile have 80 bytes, as on the Atari */
    if (strlen(argv[1]) > 70 || strlen(argv[2]) > 70) {
        fprintf(stderr, "file names are limited to 70 characters\n");
        return 2;
    }
    /* the program's own resource, as the Atari program loads RCS.RSC: the
     * goldens were made at a character height of 16, so this is the one */
    {
        const char *own = getenv("RCS_OWN_RSC");

        if (!rsrc_load(own ? own : "tests/golden/rcs.rsc")) {
            fprintf(stderr, "cannot load the program's resource\n");
            return 2;
        }
    }
    buff_size = 0x10000L;
    head = rcs_alloc(buff_size);
    ini_buff();
    /* what the command line load of the Atari program does: the definition
     * file is NAME.DFN next to the resource, and when there is none the
     * alert of open_files() (hoststubs.c answers its default button,
     * "continue without") lets the load go on without a definition file */
    strcpy(rcs_rfile, argv[1]);
    r_to_xfile(rcs_dfile, "DFN");
    if (!open_files(true) || !read_files()) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    /* the Atari program wrote OUT.RSC and OUT.DFN: here NAME.out.rsc and
     * NAME.out.DFN, in the same directory (build/host), not next to the input */
    strcpy(rcs_rfile, argv[2]);
    r_to_xfile(rcs_dfile, "DFN");
    if (!write_files()) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
