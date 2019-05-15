#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    envid_t child;
    int cnt = 0;
    if ((child = fork()) == 0) {
        while (1) {
            cprintf("Child runs: %d\n", cnt++);
            if (cnt >= 100) return;
            sys_yield();
        }
    }

    sys_env_set_tickets(sys_getenvid(), 100);
    sys_env_set_tickets(child, 10);

    while (1) {
        cprintf("Parent runs: %d\n", cnt++);
        if (cnt >= 100) return;
        sys_yield();
    }
}
