#include <inc/lib.h>

void umain(int argc, char **argv)
{
    envid_t env;
    float f1 = 1, f2 = 2;
    int i;

    cprintf("I am the parent.  Forking the child...\n");
    if ((env = fork()) == 0) {
        asm volatile("flds %0" :: "m" (f2));
        sys_yield();
        asm volatile("fsts %0" : "=m" (f2));
        if (f2 != 2)
            panic("f2 changed to %d", (int)f2);
        return;
    }

    for (i = 0; i < 20; i++) {
        asm volatile("flds %0" :: "m" (f1));
        sys_yield();
    }
    cprintf("I am the parent with f2 = %d, env = %d\n", (int)f2, env);

    cprintf("I am the parent.  Killing the child...\n");
    sys_env_destroy(env);
}
