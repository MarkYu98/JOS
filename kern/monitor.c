// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display the backtrace of function calls", mon_backtrace },
	{ "showmappings", "Show mappings of physical pages in the given physical address range", mon_showmappings },
	{ "dumpmem", "Dump the contents of a range of memory given either a virtual or physical address range", mon_dumpmem },
	{ "chperm", "change the permissions of any mapping", mon_chperm }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	unsigned int *ebp = (unsigned int*) read_ebp();
    cprintf("Stack backtrace:\n");

    while (ebp) {
    	unsigned int eip = ebp[1];
        struct Eipdebuginfo info;
        debuginfo_eip(eip, &info);

    	cprintf("ebp %08x eip %08x args", ebp, eip);
       	for(int i = 0; i < 5; i++)
            cprintf(" %08x", ebp[i+2]);
        cprintf("\n");
        cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line,
        	info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);

        ebp = (unsigned int*)(*ebp);
    }
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf) 	// Lab2 Challenge
{
	if (argc != 3) {
		cprintf("Wrong number of arguments!\n");
		cprintf("Usage: showmappings begin_va end_va\n");
		return 1;
	}
	physaddr_t begin_va = strtol(argv[1], NULL, 0);
	physaddr_t end_va = strtol(argv[2], NULL, 0);
	physaddr_t va = begin_va;
	pte_t *pte_p = pgdir_walk(kern_pgdir, (void *) va, 0);

	if (!pte_p) va = (va >> PTXSHIFT) << PTXSHIFT;
	else if (*pte_p & PTE_PS) va = (va >> PDXSHIFT) << PDXSHIFT;
	else va = (va >> PTXSHIFT) << PTXSHIFT;

	cprintf("VPA\t\tPPA\tPermission(Kernel/User) Page Size\n");

	while (va <= end_va) {
		pte_p = pgdir_walk(kern_pgdir, (void *) va, 0);
		cprintf("0x%08x\t", va);
		if (!pte_p || !(*pte_p & PTE_P)) {
			cprintf("NULL\t\t---\t---\n");
			va += PGSIZE;
		}
		else {
			cprintf("0x%08x\t", PTE_ADDR(*pte_p));
			cprintf("R%c/%c%c", (*pte_p & PTE_W) ? 'W' : '-',
								(*pte_p & PTE_U) ? 'R' : '-',
								((*pte_p & PTE_U) &&
								(*pte_p & PTE_W)) ? 'W' : '-');
			if (*pte_p & PTE_PS) {
				cprintf("\t\t4MB\n");
				va += PTSIZE;
			}
			else {
				cprintf("\t\t4KB\n");
				va += PGSIZE;
			}
		}
	}
	return 0;
}

int
mon_dumpmem(int argc, char **argv, struct Trapframe *tf) 	// Lab2 Challenge
{
	if (argc != 4) {
		cprintf("Wrong number of arguments!\n");
		cprintf("Usage: dumpmem -[pv] begin_addr end_addr\n");
		return 1;
	}
	if (argv[1][0] != '-' ||
		((argv[1][1] != 'p') && (argv[1][1] != 'v'))) {
		cprintf("Unrecognized flag: %s\n", argv[1]);
		cprintf("Usage: dumpmem -[pv] begin_addr end_addr\n");
		return 1;
	}
	physaddr_t begin_addr = strtol(argv[2], NULL, 0);
	physaddr_t end_addr = strtol(argv[3], NULL, 0);

	bool pa = (argv[1][1] == 'p');
	if (pa) {
		physaddr_t pgnum, offset;
		unsigned char *va;
		struct PageInfo *pp;

		if (begin_addr & 0xf)
			cprintf("0x%08x: ", begin_addr);
		for (physaddr_t i = begin_addr; i < end_addr; i++) {
			if (!(i & 0xf))
				cprintf("\n0x%08x: ", i);

			pgnum = i >> PGSHIFT;
			offset = PGOFF(i);

			va = (unsigned char *)(page2kva(&pages[pgnum]) + offset);
			cprintf("%02x ", *va);
		}
		cprintf("\n");
	}
	else {
		if (begin_addr & 0xf)
			cprintf("0x%08x: ", begin_addr);
		for (uintptr_t i = begin_addr; i < end_addr; i++) {
			if (!(i & 0xf))
				cprintf("\n0x%08x: ", i);

			cprintf("%02x ", *(unsigned char *) i);
		}
		cprintf("\n");
	}
	return 0;
}

int
mon_chperm(int argc, char **argv, struct Trapframe *tf) 	// Lab2 Challenge
{
	if (argc != 4) {
		cprintf("Wrong number of arguments!\n");
		cprintf("Usage: chperm -[pv][p] [pv]addr/pagenum [UWR]/[0-7]\n");
		return 1;
	}
	if (argv[1][0] != '-' ||
		((argv[1][1] != 'p') && (argv[1][1] != 'v'))) {
		cprintf("Unrecognized flag: %s\n", argv[1]);
		cprintf("Usage: chperm -[pv][p] [pv]addr/pagenum [UWR]/[0-7]\n");
		return 1;
	}
	bool pp = (argv[1][1] == 'p');
	bool pgnum = (argv[1][2] == 'p');
	physaddr_t va;

	ppn_t pn = strtol(argv[2], NULL, 0);
	if (!pgnum)
		pn = pn >> PGSHIFT;

	if (pp)
		va = (uintptr_t) page2kva(&pages[pn]);
	else
		va = (uintptr_t) pn << PGSHIFT;

	pte_t *pte_p = pgdir_walk(kern_pgdir, (void *) va, 0);
	if (!(*pte_p & PTE_P)) {
		cprintf("Error: The virtual address has no current mapping.");
		return 1;
	}

	// For lab2 PTE_PS challenge
	if (*pte_p & PTE_PS)
		va = PDX(va) << PDXSHIFT;

	pte_t oldpte = *pte_p;

	if (argv[3][0] >= '0' && argv[3][0] <= '7') {
		*pte_p = ((*pte_p) & ~(PTE_U | PTE_W | PTE_P)) | strtol(argv[3], NULL, 0);
		if (!(*pte_p & PTE_P)) {
			*pte_p |= PTE_P;
			cprintf("Disabling a PTE is not allowed.\n");
		}
	}
	else {
		if (argv[3][0] == 'U')
			*pte_p |= PTE_U;
		else if (argv[3][0] == '-')
			*pte_p &= ~PTE_U;
		if (argv[3][1] == 'W')
			*pte_p |= PTE_W;
		else if (argv[3][1] == '-')
			*pte_p &= ~PTE_W;
		if (argv[3][2] == '-')
			cprintf("Disabling a PTE is not allowed.\n");
	}

	cprintf("Page VA: 0x%08x\n", va);
	cprintf("Page PA: 0x%08x\n", PTE_ADDR(*pte_p));
	cprintf("Permission before (Kernel/User): ");
	cprintf("R%c/%c%c\n", (oldpte & PTE_W) ? 'W' : '-',
						(oldpte & PTE_U) ? 'R' : '-',
						((oldpte & PTE_U) &&
						(oldpte & PTE_W)) ? 'W' : '-');
	cprintf("Permission now (Kernel/User): ");
	cprintf("R%c/%c%c\n", (*pte_p & PTE_W) ? 'W' : '-',
						(*pte_p & PTE_U) ? 'R' : '-',
						((*pte_p & PTE_U) &&
						(*pte_p & PTE_W)) ? 'W' : '-');

	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	/*
	cprintf("\n--- Testing text/background coloring for Lab1 challenge: ---\n");
	cprintf("\e[31mRed \033[32mGreen \x1b[33mYellow \e[34mBlue \e[35mMagenta \e[36mCyan \e[37mWhite \033[0m\n");
	cprintf("\e[47;30mWhite Background\n");
	cprintf("Black \e[31mRed \033[32mGreen \x1b[33mYellow \e[34mBlue \e[35mMagenta \e[36mCyan \033[0m\n");
	cprintf("--- Test finished ---\n\n");
	*/

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
