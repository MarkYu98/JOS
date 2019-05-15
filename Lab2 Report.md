## Lab2 Report
俞一凡 Yifan Yu 1600012998

### Lab Environment
```
Host Machine:		MacBook Pro (Retina 13-inch Early 2015)
OS:					macOS Mojave 10.14.1
CPU:				2.7 GHz Intel Core i5
Memory:				8 GB

Virtual Machine:	Oracle VM VirtualBox 6.0
Memory:				1.5 GB
OS: 				Ubuntu 16.04 LTS (32bit x86) 
Gcc:            	Gcc 5.4.0
Make:           	GNU Make 4.1
Gdb:            	GNU gdb 7.11.1
```

New source files and contents for this lab:

* `inc/memlayout.h`: the layout of the virtual address space that we should implement by modifying `pmap.c`.
* `kern/pmap.c`
* `kern/pmap.h`: Together with `memlayout.h` define the `PageInfo` structure to keep track of which physical pages are free.
* `kern/kclock.h` and `kern/kclock.c`: manipulate the PC's battery-backed clock and CMOS RAM hardware, in which the BIOS records the amount of physical memory the PC contains, among other things.

## Part 1: Physical Page Management

JOS manages the PC's physical memory with *page granularity* so that it can use the MMU to map and protect each piece of allocated memory. Now we need to write the **physical page allocator**, which keeps track of which pages are free with a linked list of struct `PageInfo` objects each corresponding to a physical page.

> **Exercise 1.** In the file `kern/pmap.c`, you must implement code for the following functions (probably in the order given).
>
> `boot_alloc()`  
> `mem_init()` (only up to the call to `check_page_free_list(1)`)  
> `page_init()`  
> `page_alloc()`  
> `page_free()`  
>  
> `check_page_free_list()` and `check_page_alloc()` test your physical page allocator. You should boot JOS and see whether `check_page_alloc()` reports success. Fix your code so that it passes.

Understanding the following global variables are important for this exercise:

```c
pde_t *kern_pgdir;		// Kernel's initial page directory
struct PageInfo *pages;		// Physical page state array
static struct PageInfo *page_free_list;	// Free list of physical pages
```
`kern_pgdir` is a pointer to the kernel's initial page directory, `pages` is an array that stores `PageInfo`s of all physical pages,
`page_free_list` as its name.

Now, under the instruction of given comments, it's not hard to finish the required functions:

In `boot_alloc()`:

```c
	...
	// Allocate a chunk large enough to hold 'n' bytes, then update
	// nextfree.  Make sure nextfree is kept aligned
	// to a multiple of PGSIZE.
	//
	// LAB 2: Your code here.
	char *curfree = nextfree;
	nextfree += ROUNDUP(n, PGSIZE);

	return curfree;
	...
```

In `mem_init()`:

```c
	...
	//////////////////////////////////////////////////////////////////////
	// Allocate an array of npages 'struct PageInfo's and store it in 'pages'.
	// The kernel uses this array to keep track of physical pages: for
	// each physical page, there is a corresponding struct PageInfo in this
	// array.  'npages' is the number of physical pages in memory.  Use memset
	// to initialize all fields of each struct PageInfo to 0.
	// Your code goes here:

	pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));
	memset(pages, 0, npages * sizeof(struct PageInfo));
	
	//////////////////////////////////////////////////////////////////////
	// Now that we've allocated the initial kernel data structures, we set
	// up the list of free physical pages. Once we've done so, all further
	// memory management will go through the page_* functions. In
	// particular, we can now map memory using boot_map_region
	// or page_insert
	page_init();

	check_page_free_list(1);
	...
```

In `page_init()`, the `if` conditions check the scenarios stated by the given comment:

```c
	...
	// Change the code to reflect this.
	// NB: DO NOT actually touch the physical memory corresponding to
	// free pages!
	size_t i;
	size_t kmemtop = PADDR(boot_alloc(0)) >> PGSHIFT;
	page_free_list = NULL;
	for (i = 0; i < npages; i++) {
		pages[i].pp_ref = 0;
		if ((i > 0 && i < npages_basemem) ||
			(i >= (EXTPHYSMEM >> PGSHIFT) && i >= kmemtop)) {
			pages[i].pp_link = page_free_list;
			page_free_list = &pages[i];
		}
		else
			pages[i].pp_link = NULL;
	}
	...
```

In `page_alloc()`:

```c
struct PageInfo *
page_alloc(int alloc_flags)
{
	if (!page_free_list) return NULL;

	struct PageInfo *pp = page_free_list;
	page_free_list = pp->pp_link;
	pp->pp_link = NULL;

	if (alloc_flags & ALLOC_ZERO)
		memset(page2kva(pp), 0, PGSIZE);

	return pp;
}
```

In `page_free()`:

```c
void
page_free(struct PageInfo *pp)
{
	// Hint: You may want to panic if pp->pp_ref is nonzero or
	// pp->pp_link is not NULL.
	if (pp->pp_ref)
		panic("Try to free page kva: 0x%x with non_zero refcount!",
			page2kva(pp));
	if (pp->pp_link)
		panic("Try to free page kva: 0x%x with non-NULL pp_link!",
			page2kva(pp));
	pp->pp_link = page_free_list;
	page_free_list = pp;
}
```

Boot JOS and it works well:

```shell
...
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
...
```

--

<div STYLE="page-break-after: always;"></div>
## Part 2: Virtual Memory
> **Exercise 2.** Look at chapters 5 and 6 of the [Intel 80386 Reference Manual](https://pdos.csail.mit.edu/6.828/2018/readings/i386/toc.htm), if you haven't done so already. Read the sections about page translation and page-based protection closely (5.2 and 6.4).

The given reference manual contains basic conceptions of virtual memory and page translation and protection mechanisms in x86.

--

#### Virtual, Linear, and Physical Addresses
A C pointer is the "offset" component of the virtual address. In `boot/boot.S`, we installed a Global Descriptor Table (GDT) that effectively disabled segment translation by setting all segment base addresses to `0` and limits to `0xffffffff`. Hence the "selector" has no effect and the linear address always equals the offset of the virtual address in JOS.

> **Exercise 3.** While GDB can only access QEMU's memory by virtual address, it's often useful to be able to inspect physical memory while setting up virtual memory. Review the QEMU monitor commands from the lab tools guide, especially the `xp` command, which lets you inspect physical memory. To access the QEMU monitor, press `Ctrl-a c` in the terminal (the same binding returns to the serial console).
> 
> Use the `xp` command in the QEMU monitor and the `x` command in GDB to inspect memory at corresponding physical and virtual addresses and make sure you see the same data.

Inspecting memory at physical address starting from `0x100000` and virtual address from `0xf0100000` show the same data. (Since now we only mapped 4MB of memory using the simple page table in lab1).

--
From code executing on the CPU, once we're in protected mode, *all* memory references are interpreted as virtual addresses and translated by the MMU, which means all pointers in C are virtual addresses.

In JOS, to help document the code, the JOS source distinguishes the two types of addresses: the type `uintptr_t` represents opaque virtual addresses, and `physaddr_t` represents physical addresses. Both are really just synonyms for 32-bit integers (`uint32_t`).

> **Question 1.** Assuming that the following JOS kernel code is correct, what type should variable `x` have, `uintptr_t` or `physaddr_t`?
> 
> ```c
	mystery_t x;
	char* value = return_a_pointer();
	*value = 10;
	x = (mystery_t) value;
```

Since `value` is a pointer to a virtual address, `mystery_t` should be `uintptr_t`.

--

For memory which the kernel knows only the physical address, translation to virtual address that the kernel can actually read and write can be simply done by adding `0xf0000000` (`KERNBASE`). This should be done by using the `KADDR(pa)` macro in code. On the contrary, to turn a virtual address in this region into a physical address, the kernel can simply subtract `0xf0000000`, which should be done by `PADDR(va)`.

#### Reference counting
We need to keep a count of the number of references to each physical page in the `pp_ref` field of the `struct PageInfo` corresponding to the physical page. When this count goes to zero for a physical page, that page can be freed because it is no longer used. When this count goes to zero for a physical page, that page can be freed. This count should be equal to the number of times the physical page appears **below** `UTOP` in all page tables (the mappings above `UTOP` are mostly set up at boot time by the kernel and should never be freed).

When doing this part of the lab, we need to be careful whether the `pp_ref` is correctly incremented/decremented, sometimes this is done by other functions while sometimes this should be done directly after calling `page_alloc()`.

Now we'll get to map the first 256MB of physical memory starting at virtual address `0xf0000000` and to map a number of other regions of the virtual address space. Write routines to manage page tables: to insert and remove linear-to-physical mappings, and to create page table pages when needed.

> **Exercise 4.** In the file `kern/pmap.c`, you must implement code for the following functions.
>
> `pgdir_walk()`  
> `boot_map_region()`  
> `page_lookup()`   
> `page_remove()`  
> `page_insert()`  
>  
> `check_page()`, called from `mem_init()`, tests your page table management routines. You should make sure it reports success before proceeding.

Like before, carefully following the instruction comments, it's not hard to complete these routines.

In `pgdir_walk()`:

```c
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	pde_t pde = pgdir[PDX(va)];
	if (pde & PTE_P) 
		return (pte_t *) KADDR(PTE_ADDR(pde)) + PTX(va);

	// Page table not_exist
	if (!create) return NULL;

	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (!pp) return NULL;
	pp->pp_ref++;

	pgdir[PDX(va)] = page2pa(pp) | PTE_P | PTE_W | PTE_U;
	return (pte_t *) KADDR(page2pa(pp)) + PTX(va);
}
```

In `boot_map_region()`:

```c
static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	for (size_t i = 0; i < size; i += PGSIZE) {
	        pte_t *pte = pgdir_walk(pgdir, (void *) va, 1);
	        *pte = pa | PTE_P | perm;
	        va += PGSIZE;
	        pa += PGSIZE; 
	}
}
```

In `page_lookup()`:

```c
struct PageInfo *
page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t *pte = pgdir_walk(pgdir, va, 0);
	if (!pte || !(*pte & PTE_P)) return NULL;

	if (pte_store)
		*pte_store = pte;

	return pa2page(PTE_ADDR(*pte));
}
```

In `page_remove()`:

```c
void
page_remove(pde_t *pgdir, void *va)
{
	pte_t *pte_p;
	struct PageInfo *pp = page_lookup(pgdir, va, &pte_p);
	if (pp) {
		page_decref(pp);
		*pte_p = 0;
		tlb_invalidate(pgdir, va);
	}
}
```

In `page_insert()`:

```c
int
page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
{
	pte_t *pte = pgdir_walk(pgdir, va, 1);
	if (!pte) return -E_NO_MEM;

	pp->pp_ref++;
	page_remove(pgdir, va);
	*pte = page2pa(pp) | perm | PTE_P;
	return 0;
}
```
Run JOS again and now we can pass the `check_page()` tests.

--
## Part 3: Kernel Address Space

`inc/memlayout.h` is very useful in this part of the lab.

#### Permissions and Fault Isolation
Since kernel and user memory are both present in each environment's address space, we will have to use permission bits in our x86 page tables to allow user code access only to the user part of the address space. Specifically, the given code defines `PTE_U`, `PTE_W` to specify the user's read and write permission to a page pointed by a PTE. `PTE_W` also specifies kernel's write permission. The detailed memory permission layout can be found in `inc/memlayout.h`.

#### Initializing the Kernel Address Space
Set up the address space above `UTOP`: the kernel part of the address space, using the functions just wrote in above exercises.

> **Exercise 5.** Fill in the missing code in `mem_init()` after the call to `check_page()`.
> 
> Your code should now pass the `check_kern_pgdir()` and `check_page_installed_pgdir()` checks.

```c
	//////////////////////////////////////////////////////////////////////
	// Now we set up virtual memory

	//////////////////////////////////////////////////////////////////////
	// Map 'pages' read-only by the user at linear address UPAGES
	// Permissions:
	//    - the new image at UPAGES -- kernel R, user R
	//      (ie. perm = PTE_U | PTE_P)
	//    - pages itself -- kernel RW, user NONE
	// Your code goes here:
	boot_map_region(kern_pgdir, UPAGES,
					ROUNDUP(npages * sizeof(struct PageInfo), PGSIZE),
					PADDR(pages), PTE_U | PTE_P);

	//////////////////////////////////////////////////////////////////////
	// Use the physical memory that 'bootstack' refers to as the kernel
	// stack.  The kernel stack grows down from virtual address KSTACKTOP.
	// We consider the entire range from [KSTACKTOP-PTSIZE, KSTACKTOP)
	// to be the kernel stack, but break this into two pieces:
	//     * [KSTACKTOP-KSTKSIZE, KSTACKTOP) -- backed by physical memory
	//     * [KSTACKTOP-PTSIZE, KSTACKTOP-KSTKSIZE) -- not backed; so if
	//       the kernel overflows its stack, it will fault rather than
	//       overwrite memory.  Known as a "guard page".
	//     Permissions: kernel RW, user NONE
	// Your code goes here:
	boot_map_region(kern_pgdir, KSTACKTOP-KSTKSIZE,
					ROUNDUP(KSTKSIZE, PGSIZE), PADDR(bootstack),
					PTE_W | PTE_P);

	//////////////////////////////////////////////////////////////////////
	// Map all of physical memory at KERNBASE.
	// Ie.  the VA range [KERNBASE, 2^32) should map to
	//      the PA range [0, 2^32 - KERNBASE)
	// We might not have 2^32 - KERNBASE bytes of physical memory, but
	// we just set up the mapping anyway.
	// Permissions: kernel RW, user NONE
	// Your code goes here:
	boot_map_region(kern_pgdir, KERNBASE,
					-KERNBASE, 0, PTE_W | PTE_P);
```

Run JOS again, and it should be able to pass all the checks in this lab.

--

> **Question 2.** What entries (rows) in the page directory have been filled in at this point? What addresses do they map and where do they point? In other words, fill out this table as much as possible:

<center>

| Entry | Base VA | Points to (logically) |
| --- | --- | --- |
| 1023 | 0xffc00000 |	Page table for the top 4MB of phys. memory |
| ... | ... | ... |
| 960 | 0xf000000 | Page table for bottom 4MB of phys. memory |
| 959 | 0xefc0000 | Page table for the kernel stack (32KB) |
| ... | ... | ... |
| 957 | 0xef400000 | `kern_pgdir`: kernel page directory |
| 956 | 0xef000000 | `pages`: physical pages info array |
| ... | ... | ... |
| 0 | 0x0 | / |

</center>
--

> **Question 3.** We have placed the kernel and user environment in the same address space. Why will user programs not be able to read or write the kernel's memory? What specific mechanisms protect the kernel memory?

When a program tries to access a memory address, the address will first be translated by MMU. During this process, MMU will check if the program have the corresponding permission. With the correct permission bits set, user programs will not be able to read or write kernel's memory.

--
> **Question 4.** What is the maximum amount of physical memory that this operating system can support? Why?

JOS can support a maximum of 256MB physical memory. JOS maps virtual address [0xf0000000, 0xffffffff) to physical address in order to provide access to the physical memory, the size of this region is 256MB.

--
> **Question 5.** How much space overhead is there for managing memory, if we actually had the maximum amount of physical memory? How is this overhead broken down?

The overhead is 272K, consists of: a kernel page directory, 64 page tables for remapped physical memory, and one page each for kernel stack, read-only `kern_pgdir` and `pages`.

--
> **Question 6.** Revisit the page table setup in `kern/entry.S` and `kern/entrypgdir.c`. Immediately after we turn on paging, `EIP` is still a low number (a little over 1MB). At what point do we transition to running at an `EIP` above `KERNBASE`? What makes it possible for us to continue executing at a low `EIP` between when we enable paging and when we begin running at an `EIP` above `KERNBASE`? Why is this transition necessary?

`jmp *%eax` in `kern/entry.S` is exactly where we transition to running above `KERNBASE`. We can execute correctly because in `kern/entrypgdir.c` we mapped virtual addresses [0, 4MB) and [KERNBASE, KERNBASE + 4MB) both to physical address [0, 4MB). This transition is necessary because [0, 4MB) in the virtual memory space is only temporary and will be used by user programs in the future. 

---
### Challenge 1
> ***Challenge!*** We consumed many physical pages to hold the page tables for the `KERNBASE` mapping. Do a more space-efficient job using the `PTE_PS` ("Page Size") bit in the page directory entries. This bit was not supported in the original 80386, but is supported on more recent x86 processors. You will therefore have to refer to [Volume 3 of the current Intel manuals](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3A.pdf). Make sure you design the kernel to use this optimization only on processors that support it!

To enable this functionality, we have to first detect whether the CPU support the `PTE_PS` bit. I wrote the following routine in `pmap.c`:

```cpp
// For lab2 challenge
static bool PS_enabled;
static void
detect_PS_support()
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t cr4;

	cpuid(1, &eax, &ebx, &ecx, &edx);

	// No.3 bit of edx represents PS support
	PS_enabled = !!(edx & (1 << 3));
	if (PS_enabled) {
		// enable PSE in register cr4
		cr4 = rcr4();
		cr4 |= CR4_PSE;
		lcr4(cr4);
	}
}
```

The global boolean variable `PS_enabled` is used to identify whether the current CPU has support for `PTE_PS`.

Then to make the `KERNBASE` mapping correct using 4MB pages, we have to modify `boot_map_region()`, which handles not 4MB-aligned pages first, then map 4MB pages, and finally finish by mapping the remaining regular pages:

```c
static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	// Modified to use PTE_PS for lab2 challenge
	pte_t *pte_p;
	if (PS_enabled && (va & (PTSIZE-1)) == (pa & (PTSIZE-1))) {
		// handle pages that are not 4Mb aligned first
		while (size > 0 && (va & (PTSIZE-1)) != 0) {
			pte_p = pgdir_walk(pgdir, (void *) va, true);
			*pte_p = pa | perm | PTE_P;
			va += PGSIZE;
			pa += PGSIZE;
			size -= PGSIZE;
		}

		// handle pages that are 4Mb aligned
		while (size >= PTSIZE) {
			pgdir[PDX(va)] = pa | PTE_PS | perm | PTE_P;
			va += PTSIZE;
			pa += PTSIZE;
			size -= PTSIZE;
		}
	}

	// normal pages
	while (size > 0) {
		pte_p = pgdir_walk(pgdir, (void *) va, true);
		*pte_p = pa | perm | PTE_P;
		va += PGSIZE;
		pa += PGSIZE;
		size -= PGSIZE;
	}
}
```

Additionally, we have to manually change the `check_va2pa()` routine so that it can correctly check virtual address corresponding to a 4MB page by going over one level of PTE instead of two (PDE and PTE):

```c
static physaddr_t
check_va2pa(pde_t *pgdir, uintptr_t va)
{
	pte_t *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;
	// Modified for lab2 challenge to pass
	if (*pgdir & PTE_PS)
		return PTE_ADDR(*pgdir) | (PTX(va) << PGSHIFT);
	p = (pte_t*) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}
```

For testing, set `QEMUEXTRA="-cpu qemu32,-pse"` when `make` to disable PSE support, and `QEMUEXTRA="-cpu qemu32,+pse"` to enable it (this is also the default case). In both case, the modification passed all tests.

---

### Challenge 2
> ***Challenge!*** Extend the JOS kernel monitor with commands to:
>
> * Display in a useful and easy-to-read format all of the physical page mappings (or lack thereof) that apply to a particular range of virtual/linear addresses in the currently active address space. For example, you might enter 'showmappings 0x3000 0x5000' to display the physical page mappings and corresponding permission bits that apply to the pages at virtual addresses 0x3000, 0x4000, and 0x5000.
> * Explicitly set, clear, or change the permissions of any mapping in the current address space.
> * Dump the contents of a range of memory given either a virtual or physical address range. Be sure the dump code behaves correctly when the range extends across page boundaries!
> * Do anything else that you think might be useful later for debugging the kernel. (There's a good chance it will be!)

For this challenge, I added 3 commands for the JOS monitor in `kern/monitor.c`:

* `showmappings`: Show mappings of physical pages in the given physical address range;
* `dumpmem`: Dump the contents of a range of memory given either a virtual or physical address range;
* `chperm`: change the permission bits of any mapping.

The corresponding implementations are in `mon_showmappings()`, `mon_dumpmem()`, `mon_chperm()` respectively. All three commands support the previous challenge (using 4MB pages if enabled), and provided basic sanity/safety/error checks. The code is fairly readable so I won't paste and explain here, see `monitor.c` for details. Here I'll show the actual function and usage of the three commands in this document.

When doing this challenge, some provided functions like `strtol()` becomes very handy.

* `showmappings`: usage `showmappings begin_va end_va`, will show the mapping (if any, `NULL` otherwise) of each pages from the given virtual address range to corresponding physical address, the permission bits and the page size (regular 4KB or 4MB).  

	Examples:   
	When `PTE_PS` is enabled:
	
	```shell
	K> showmappings 0x3000 0x5000
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0x00003000	     NULL	     	     ---	     ---
0x00004000	     NULL	     	     ---	     ---
0x00005000	     NULL	     	     ---	     ---
K> showmappings 0xf0100000 0xf0800000
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0xf0000000	     0x00000000	     RW/--	     	     4MB
0xf0400000	     0x00400000	     RW/--	     	     4MB
0xf0800000	     0x00800000	     RW/--	     	     4MB
K> 
```
	When `PTE_PS` is not enabled:
	
	```shell
	K> showmappings 0x3000 0x5000
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0x00003000	     NULL	     	     ---	     ---
0x00004000	     NULL	     	     ---	     ---
0x00005000	     NULL	     	     ---	     ---
K> showmappings 0xf0100000 0xf0108000
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0xf0100000	     0x00100000	     RW/--	     	     4KB
0xf0101000	     0x00101000	     RW/--	     	     4KB
0xf0102000	     0x00102000	     RW/--	     	     4KB
0xf0103000	     0x00103000	     RW/--	     	     4KB
0xf0104000	     0x00104000	     RW/--	     	     4KB
0xf0105000	     0x00105000	     RW/--	     	     4KB
0xf0106000	     0x00106000	     RW/--	     	     4KB
0xf0107000	     0x00107000	     RW/--	     	     4KB
0xf0108000	     0x00108000	     RW/--	     	     4KB
K>
```

* `dumpmem`: usage `dumpmem -[pv] begin_addr end_addr`, `-p` means the given address range is physical address, while `-v` the virtual address. 

	*Please note that this command does not check the validity of given virtual address (due to too many detailed cases), nor if the given physical address is actually in the physical memory. The system will abort on illegal inputs (triple faults). This should be taken care of by the system administrator who uses this command. A safe way to examine virtual memory is to `showmappings` first to check that the address in the range do have mappings.* 
	
	Example:
	
	```shell
	K> dumpmem -v 0xefffff00 0xf0000000  
0xefffff00: 00 80 0b f0 a0 80 0b f0 00 0f 00 00 00 00 00 00 
0xefffff10: 94 00 01 00 30 00 00 00 38 5f 11 f0 ed 2d 10 f0 
0xefffff20: bd 41 10 f0 44 5f 11 f0 38 5f 11 f0 00 ff ff ef 
0xefffff30: 04 00 00 00 80 5f 11 f0 68 5f 11 f0 3b 0d 10 f0 
0xefffff40: bd 41 10 f0 f0 00 00 00 10 00 00 00 58 37 10 f0 
0xefffff50: dc 44 10 f0 74 5f 11 f0 88 5f 11 f0 04 00 00 00 
0xefffff60: 04 00 00 00 04 00 00 00 d8 5f 11 f0 62 0e 10 f0 
0xefffff70: 04 00 00 00 80 5f 11 f0 00 00 00 00 04 f0 3f f0 
0xefffff80: 80 85 11 f0 88 85 11 f0 8b 85 11 f0 96 85 11 f0 
0xefffff90: 00 00 00 00 00 10 00 00 00 10 00 00 02 00 00 00 
0xefffffa0: 00 00 00 00 00 80 00 00 00 a0 11 f0 00 00 04 00 
0xefffffb0: 00 00 00 00 00 00 00 00 d8 5f 11 f0 00 f0 3f f0 
0xefffffc0: 20 3e 10 f0 e4 5f 11 f0 00 00 00 00 94 00 01 00 
0xefffffd0: 94 00 01 00 00 00 00 00 f8 5f 11 f0 86 00 10 f0 
0xefffffe0: 00 00 00 00 ac 1a 00 00 60 06 00 00 00 00 00 00 
0xeffffff0: 00 00 00 00 00 00 00 00 00 00 00 00 3e 00 10 f0 
K> 
	```

* `chperm`: usage `chperm -[pv][p] [pv]addr/pagenum [UWR]/[0-7]`. The first parameter specifies whether the address/page number is physical or virtual, and whether an address or page number will be provided next. The third parameter specifies the expected permission after change, [0-7] corresponds to the 3-bit representation in the order of `PTE_U PTE_W PTE_P`.

	*Please note that this command does not check the validity of given virtual address, nor if the given physical address is actually in the physical memory too. The system will abort on illegal inputs (triple faults). This should be taken care of by the system administrator who uses this command. A safe way to examine virtual memory is to `showmappings` first to check that the address in the range do have mappings.* 

	Example:
	
	```shell
	K> showmappings 0xefff8000 0xf0000000
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0xefff8000	     0x0010e000	     RW/--	     	     4KB
0xefff9000	     0x0010f000	     RW/--	     	     4KB
0xefffa000	     0x00110000	     RW/--	     	     4KB
0xefffb000	     0x00111000	     RW/--	     	     4KB
0xefffc000	     0x00112000	     RW/--	     	     4KB
0xefffd000	     0x00113000	     RW/--	     	     4KB
0xefffe000	     0x00114000	     RW/--	     	     4KB
0xeffff000	     0x00115000	     RW/--	     	     4KB
0xf0000000	     0x00000000	     RW/--	     	     4MB
K> chperm -v 0xefff8080 UWR
Page VA: 0xefff8000
Page PA: 0x0010e000
Permission before (Kernel/User): RW/--
Permission now (Kernel/User): RW/RW
K> showmappings 0xefff8000 0xefff9090
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0xefff8000	     0x0010e000	     RW/RW	     	     4KB
0xefff9000	     0x0010f000	     RW/--	     	     4KB
K> chperm -v 0xefff9090 --R
Page VA: 0xefff9000
Page PA: 0x0010f000
Permission before (Kernel/User): RW/--
Permission now (Kernel/User): R-/--
K> showmappings 0xefff8080 0xefff9090
VPA	     	     PPA	     Permission(Kernel/User) Page Size
0xefff8000	     0x0010e000	     RW/RW	     	     4KB
0xefff9000	     0x0010f000	     R-/--	     	     4KB
K> 
```

---

### Lab 2 IS COMPLETED.