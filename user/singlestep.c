#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    asm volatile("int $3");
    cprintf("Continue to execute: 1\n");
    cprintf("Continue to execute: 2\n");
    cprintf("Continue to execute: 3\n");
    cprintf("Continue to execute: 4\n");
    cprintf("Continue to execute: 5\n");
    cprintf("Exiting!\n");
}
