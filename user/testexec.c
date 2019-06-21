#include <inc/lib.h>

void umain(int argc, char **argv)
{
    int r;

    if ((r = exec(argv[1], (const char **)&argv[1])) < 0) {
        cprintf("testexec: exec fail: %e\n", r);
    }
}
