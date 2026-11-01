#include <gem.h>
#include <stdio.h>
#include <string.h>
#include "rcs.h"
#include "rcsaddr.h"
#include "rcsdata.h"
#include "rcsdefs.h"
#include "rcsread.h"
#include "rcswrite.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: roundtrip in.rsc out.rsc\n");
        return 2;
    }
    buff_size = 0x10000L;
    head = rcs_alloc(buff_size);
    ini_buff();
    strcpy(rcs_rfile, argv[1]);
    rcs_rhndl = fopen(rcs_rfile, "rb");
    if (!rcs_rhndl || !read_files()) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    strcpy(rcs_rfile, argv[2]);
    if (!write_files()) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
