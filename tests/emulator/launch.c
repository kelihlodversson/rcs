#include <gem.h>
int main(void)
{
    static char tail[] = "\x07" "RCS.RSC";
    if (appl_init() < 0) return 1;
    shel_write(1, 1, 1, "C:\\RCS.PRG", tail);
    appl_exit();
    return 0;
}
