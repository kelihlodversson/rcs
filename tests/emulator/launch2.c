#include <gem.h>
int main(void)
{
    static char tail[] = "\x08" "DESK.RSC";
    if (appl_init() < 0) return 1;
    shel_write(1, 1, 1, "C:\\RCS.PRG", tail);
    appl_exit();
    return 0;
}
