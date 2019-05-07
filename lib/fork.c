// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & FEC_WR) || !(uvpt[PGNUM(addr)] & PTE_P) || !(uvpt[PGNUM(addr)] & PTE_COW))
		panic("pgfault: Faulting access is not write or not to a COW page!");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, (void *) PFTEMP, PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_alloc error: %d!", r);
	memcpy((void *) PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, (void *) PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_map error: %d!", r);
	if ((r = sys_page_unmap(0, (void *) PFTEMP)) < 0)
		panic("pgfault: sys_page_unmap error: %d!", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	// LAB 4: Your code here.
	pte_t pte = uvpt[pn];
	void *va = (void *)(pn << PGSHIFT);
	if (pte & (PTE_COW | PTE_W)) {
		if ((r = sys_page_map(0, va, envid, va, PTE_COW | PTE_U)) < 0)
			return r;
		if ((r = sys_page_map(0, va, 0, va, PTE_COW | PTE_U)) < 0)
			return r;
	}
	else {
		if ((r = sys_page_map(0, va, envid, va, PTE_U)) < 0)
			return r;
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int r;

	// Set up our page fault handler appropriately.
	set_pgfault_handler(pgfault);

	// Create a child.
	envid_t cid = sys_exofork();
	if (cid < 0)
		return cid; // Error
	if (cid == 0) {
		// Now the child here
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	for (uintptr_t va = 0; va < USTACKTOP; va += PGSIZE) {
		if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_U))
			duppage(cid, PGNUM(va));
	}

	if ((r = sys_page_alloc(cid, (void *)(UXSTACKTOP-PGSIZE), PTE_U | PTE_W)) < 0)
		return r;

	extern void _pgfault_upcall(void);
	// Copy page fault handler setup to the child.
	if ((r = sys_env_set_pgfault_upcall(cid, _pgfault_upcall)) < 0)
		return r;
	// Then mark the child as runnable and return.
	if ((r = sys_env_set_status(cid, ENV_RUNNABLE)) < 0)
		return r;
	return cid;
}

// Lab4 Challenge!
int
sfork(void)
{
	int r;

	// page fault handler (not really useful for sfork except stack).
	set_pgfault_handler(pgfault);

	// Create a child.
	envid_t cid = sys_exofork();
	if (cid < 0)
		return cid; // Error
	if (cid == 0) {
		// Now the child here
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	for (uintptr_t va = 0; va < USTACKTOP; va += PGSIZE) {
		if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_U)) {
			unsigned pn = PGNUM(va);
			pte_t pte = uvpt[pn];
			if (va == USTACKTOP-PGSIZE) { // Normal COW for stack
				if ((r = sys_page_map(0, (void *) va, cid, (void *) va, PTE_COW | PTE_U)) < 0)
					return r;
				if ((r = sys_page_map(0, (void *) va, 0, (void *) va, PTE_COW | PTE_U)) < 0)
					return r;
			}
			else {
				if (pte & PTE_COW) {
					// First copy and make it a Write page
					if ((r = sys_page_alloc(0, (void *) PFTEMP, PTE_U | PTE_W)) < 0)
						panic("sfork: sys_page_alloc error: %d!", r);
					memcpy((void *) PFTEMP, (void *) va, PGSIZE);
					if ((r = sys_page_map(0, (void *) PFTEMP, 0, (void *) va, PTE_U | PTE_W)) < 0)
						panic("sfork: sys_page_map error: %d!", r);
					if ((r = sys_page_unmap(0, (void *) PFTEMP)) < 0)
						panic("sfork: sys_page_unmap error: %d!", r);
				}
				// Then copy mapping to the son
				if ((r = sys_page_map(0, (void *) va, cid, (void *) va, pte & (PTE_U | PTE_W))) < 0)
					return r;
			}
		}
	}

	// Exception Stack
	if ((r = sys_page_alloc(cid, (void *)(UXSTACKTOP-PGSIZE), PTE_U | PTE_W)) < 0)
		return r;

	extern void _pgfault_upcall(void);
	// Copy page fault handler setup to the child.
	if ((r = sys_env_set_pgfault_upcall(cid, _pgfault_upcall)) < 0)
		return r;
	// Then mark the child as runnable and return.
	if ((r = sys_env_set_status(cid, ENV_RUNNABLE)) < 0)
		return r;
	return cid;
}
