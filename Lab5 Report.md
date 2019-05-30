## Lab5 Report
俞一凡 Yifan Yu 1600012998

*I completed this lab with 1 challenge, reported at the end of this report.*

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

The main new component for this part of the lab is the file system environment, located in the new `fs` directory. New source files for this lab:

* `fs/fs.c`: Code that mainipulates the file system's on-disk structure.
* `fs/bc.c`: A simple block cache built on top of our user-level page fault handling facility.
* `fs/ide.c`: Minimal PIO-based (non-interrupt-driven) IDE driver code.
* `fs/serv.c`: The file system server that interacts with client environments using file system IPCs.
* `lib/fd.c`: Code that implements the general UNIX-like file descriptor interface.
* `lib/file.c`: The driver for on-disk file type, implemented as a file system IPC client.
* `lib/console.c`: The driver for console input/output file type.
* `lib/spawn.c`: Code skeleton of the spawn library call.

The main purpose of this lab is to implement a much simpler but powerful enough file system to provide the basic features: creating, reading, writing, and deleting files organized in a hierarchical directory structure.

### File system preliminaries

Before starting, following the instruction on the Lab webpage and get clear of the following concepts of **On-Disk File System Structure**:

* Sectors and Blocks
* Superblocks
* File Meta-data
* Directories versus Regular Files

--
### The File System
The file system functions needs to implement are: reading blocks into the block cache and flushing them back to disk; allocating disk blocks; mapping file offsets to disk blocks; and implementing read, write, and open in the IPC interface. 

#### Disk Access
The file system environment in our operating system needs to be able to access the disk, so we first need to implement the disk access functionality.

It is easy to implement disk access in user space this way as long as we rely on polling, "programmed I/O" (PIO)-based disk access and do not use disk interrupts. The x86 processor uses the IOPL bits in the EFLAGS register to determine whether protected-mode code is allowed to perform special device I/O instructions such as the IN and OUT instructions.

> **Exercise 1.** `i386_init` identifies the file system environment by passing the type `ENV_TYPE_FS` to your environment creation function, `env_create`. Modify `env_create` in `env.c`, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.
> 
> Make sure you can start the file environment without causing a General Protection fault. You should pass the "fs i/o" test in `make grade`.

Only need to do this:

```c
	// LAB 5: Your code here.
	if (type == ENV_TYPE_FS)
		e->env_tf.tf_eflags |= FL_IOPL_3;
```

--
> **Question 1.** Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?

No. `EFLAGS` register will be saved and restored in `struct Trapframe` during environment switching as done in earlier labs.

--

### The Block Cache
In our file system, a simple "buffer cache" (a block cache) will be implemented with the help of the processor's virtual memory system. The code for the block cache is in `fs/bc.c`.

The file system will be limited to handling disks of size 3GB or less. A large, fixed 3GB region of the file system environment's address space is reserved, from 0x10000000 (`DISKMAP`) up to 0xD0000000 (`DISKMAP+DISKMAX`), as a "memory mapped" version of the disk. The `diskaddr` function in `fs/bc.c` implements this translation from disk block numbers to virtual addresses (along with some sanity checking).

Since it would take a long time to read the entire disk into memory, we'll implement a form of demand paging, wherein we only allocate pages in the disk map region and read the corresponding block from the disk in response to a page fault in this region.

After the block cache is initialized, simply store pointers into the disk map region in the `super` global variable. After this point, we can simply read from the `super` structure as if they were in memory and our page fault handler will read them from disk as necessary.

> **Exercise 2.** Implement the `bc_pgfault` and `flush_block` functions in `fs/bc.c`. `bc_pgfault` is a page fault handler, just like the one your wrote in the previous lab for copy-on-write fork, except that its job is to load pages in from the disk in response to a page fault. When writing this, keep in mind that (1) `addr` may not be aligned to a block boundary and (2) `ide_read` operates in sectors, not blocks.
> 
> The `flush_block` function should write a block out to disk if necessary. `flush_block` shouldn't do anything if the block isn't even in the block cache (that is, the page isn't mapped) or if it's not dirty. We will use the VM hardware to keep track of whether a disk block has been modified since it was last read from or written to disk. To see whether a block needs writing, we can just look to see if the `PTE_D` "dirty" bit is set in the `uvpt` entry. (The `PTE_D` bit is set by the processor in response to a write to that page; see 5.2.4.3 in [chapter 5](http://pdos.csail.mit.edu/6.828/2011/readings/i386/s05_02.htm) of the 386 reference manual.) After writing the block to disk, `flush_block` should clear the `PTE_D` bit using `sys_page_map`.
> 
> Use `make grade` to test your code. Your code should pass "check_bc", "check_super", and "check_bitmap".

In `pc_bgfault`:

```c
	// ...
	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary. fs/ide.c has code to read
	// the disk.
	//
	// LAB 5: you code here:
	addr = (void *) ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, addr, PTE_W | PTE_U | PTE_P)) < 0)
		panic("in bc_pgfault, sys_page_alloc: %e", r);
	if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("in bc_pgfault, ide_read: %e", r);
	// ...
```

`flush_block`:

```c
void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.
	if (!va_is_mapped(addr) || !va_is_dirty(addr))
		return;
	addr = (void *) ROUNDDOWN(addr, PGSIZE);
	if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("in flush_block, ide_write: %e", r);
	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		
```
--

### The Block Bitmap
After `fs_init` sets the `bitmap` pointer, we can treat `bitmap` as a packed array of bits, one for each block on the disk, indicating whether the block is free.

> **Exercise 3.** Use `free_block` as a model to implement `alloc_block` in `fs/fs.c`, which should find a free disk block in the bitmap, mark it used, and return the number of that block. When you allocate a block, you should immediately flush the changed `bitmap` block to disk with `flush_block`, to help file system consistency.
> 
> Use `make grade` to test your code. Your code should now pass "alloc_block".

Use `free_block` as a model to write `alloc_block`:

```c
int
alloc_block(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE blocks.  There are
	// super->s_nblocks blocks in the disk altogether.

	// LAB 5: Your code here.
	uint32_t blockno = (super->s_nblocks - 1) / BLKBITSIZE + 3;
	for (; blockno < super->s_nblocks; blockno++) {
		if (bitmap[blockno/32] & (1<<(blockno%32))) {
			bitmap[blockno/32] &= ~(1<<(blockno%32));
			flush_block((void *)DISKMAP + (2 + blockno / BLKBITSIZE) * BLKSIZE);
			return blockno;
		}
	}
	return -E_NO_DISK;
```
--

### File Operations
> **Exercise 4.** Implement `file_block_walk` and `file_get_block`. `file_block_walk` maps from a block offset within a file to the pointer for that block in the `struct File` or the indirect block, very much like what `pgdir_walk` did for page tables. `file_get_block` goes one step further and maps to the actual disk block, allocating a new one if necessary.
> 
> Use make grade to test your code. Your code should pass "file_open", "file\_get\_block", and "file\_flush/file\_truncated/file\_rewrite", and "testfile".

Very similar to the operations walking page directory and page tables, e.g. `pgdir_walk`. See `fs/fs.c` for code.

--

### The file system interface
Since other environments can't directly call functions in the file system environment, we'll expose access to the file system environment via a *remote procedure call*, or RPC, abstraction, built atop JOS's IPC mechanism. Graphically, a call to the file system server (say, read) looks like

```
       Regular env           FS env
   +---------------+   +---------------+
   |      read     |   |   file_read   |
   |   (lib/fd.c)  |   |   (fs/fs.c)   |
...|.......|.......|...|.......^.......|...............
   |       v       |   |       |       | RPC mechanism
   |  devfile_read |   |  serve_read   |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |     fsipc     |   |     serve     |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |   ipc_send    |   |   ipc_recv    |
   |       |       |   |       ^       |
   +-------|-------+   +-------|-------+
           |                   |
           +-------------------+
```

> **Exercise 5.** Implement serve_read in `fs/serv.c`.
> 
> `serve_read`'s heavy lifting will be done by the already-implemented `file_read` in `fs/fs.c` (which, in turn, is just a bunch of calls to `file_get_block`). `serve_read` just has to provide the RPC interface for file reading. Look at the comments and code in `serve_set_size` to get a general idea of how the server functions should be structured.
> 
> Use `make grade` to test your code. Your code should pass "serve\_open/file\_stat/file\_close" and "file_read" for a score of 70/150.

```c
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;

	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Lab 5: Your code here:
	struct OpenFile *o;
	int r;

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	r = file_read(o->o_file, (void *)ret->ret_buf, req->req_n, o->o_fd->fd_offset);
	if (r >= 0)
		o->o_fd->fd_offset += r;
	return r;
}
```

--

> **Exercise 6.** Implement serve_write in `fs/serv.c` and devfile_write in `lib/file.c`.

Use `make grade` to test your code. Your code should pass "file_write", "file\_read after file\_write", "open", and "large file" for a score of 90/150.

```c
int
serve_write(envid_t envid, struct Fsreq_write *req)
{
	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// LAB 5: Your code here.
	struct OpenFile *o;
	int r;

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	r = file_write(o->o_file, (void *)req->req_buf, req->req_n, o->o_fd->fd_offset);
	if (r >= 0)
		o->o_fd->fd_offset += r;
	return r;
}
```

--

### Spawning Processes
`spawn` (see `lib/spawn.c`) creates a new environment, loads a program image from the file system into it, and then starts the child environment running this program. The parent process then continues running independently of the child. The `spawn` function effectively acts like a `fork` in UNIX followed by an immediate `exec` in the child process.

The reason that `spawn` is implemented rather than a UNIX-style `exec` is that `spawn` is easier to implement from user space in "exokernel fashion", without special help from the kernel. 

> **Exercise 7.** `spawn` relies on the new syscall `sys_env_set_trapframe` to initialize the state of the newly created environment. Implement `sys_env_set_trapframe` in `kern/syscall.c` (don't forget to dispatch the new system call in `syscall()`).
> 
> Test your code by running the `user/spawnhello` program from `kern/init.c`, which will attempt to spawn `/hello` from the file system.
> 
> Use `make grade`to test your code.

```c
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
    struct Env *env;
    if (envid2env(envid, &env, 1) == -E_BAD_ENV)
        return -E_BAD_ENV;

    user_mem_assert(curenv, (const void *) tf, sizeof(struct Trapframe), PTE_U | PTE_W);

    env->env_tf = *tf;
    env->env_tf.tf_cs = GD_UT | 3;
    env->env_tf.tf_eflags |= FL_IF;
    env->env_tf.tf_eflags &= ~FL_IOPL_MASK;
	return 0;
}
```
Remeber to dispatch this new system call in `syscall()`.

--

### Sharing library state across fork and spawn
The UNIX file descriptors are a general notion that also encompasses pipes, console I/O, etc. In JOS, each of these device types has a corresponding `struct Dev`, with pointers to the functions that implement read/write/etc. for that device type. `lib/fd.c` implements the general UNIX-like file descriptor interface on top of this. Each `struct Fd` indicates its device type, and most of the functions in `lib/fd.c` simply dispatch operations to functions in the appropriate `struct Dev`.

`lib/fd.c` also maintains the file descriptor table region in each application environment's address space, starting at `FDTABLE`. This area reserves a page's worth (4KB) of address space for each of the up to `MAXFD` (currently 32) file descriptors the application can have open at once. At any given time, a particular file descriptor table page is mapped if and only if the corresponding file descriptor is in use. Each file descriptor also has an optional "data page" in the region starting at `FILEDATA`, which devices can use if they choose.

File descriptor state will be shared across fork and spawn. Right now, on fork, the memory will be marked copy-on-write, so the state will be duplicated rather than shared. On spawn, the memory will be left behind, not copied at all. We will change fork to know that certain regions of memory are used by the "library operating system" and should always be shared. This is what `PTE_SHARE` bit in `inc/lib.h` is for. 

> **Exercise 8.** Change `duppage` in `lib/fork.c` to follow the new convention. If the page table entry has the `PTE_SHARE` bit set, just copy the mapping directly. (You should use `PTE_SYSCALL`, not `0xfff`, to mask out the relevant bits from the page table entry. `0xfff` picks up the accessed and dirty bits as well.)
> 
> Likewise, implement `copy_shared_pages` in `lib/spawn.c`. It should loop through all page table entries in the current process (just like fork did), copying any page mappings that have the `PTE_SHARE` bit set into the child process.

In `lib/fork.c`, just add this condition to `duppage`:

```c
	if (pte & PTE_SHARE) {
		if ((r = sys_page_map(0, va, envid, va, pte & PTE_SYSCALL)) < 0)
			return r;
	}
```

In `lib/spawn.c`:

```c
static int
copy_shared_pages(envid_t child)
{
	// LAB 5: Your code here.
	int r;

	for (uintptr_t va = 0; va < UTOP; va += PGSIZE) {
		if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_SHARE) &&
			(uvpt[PGNUM(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_U)) {
			int perm = uvpt[PGNUM(va)] & PTE_SYSCALL;
			if ((r = sys_page_map(0, (void *)va, child, (void *)va, perm)) < 0)
				return r;
		}
	}
	return 0;
}
```

Run relevant tests and `make grade`, the results meet expectations.

---
#### Fix for Lab 4's `sfork` Challenge
For Lab 4's `sfork` challenge, I redefined the `thisenv` global symbol to be a macro, which always dereference the top of the user stack (`USTACKTOP-4`) as a `struct Env *`, and always put the address of the pointer to current `Env`'s at the top of its stack. 

For this to work with Lab 5, I have to change the corresponding place in `spawn()`, specifically these two places:

```c
// -4 to work with lab4 sfork's thisenv setting
string_store = (char*) UTEMP + PGSIZE - string_size - 4; 
```

and

```c
// -4 to work with lab4 sfork's thisenv setting
assert(string_store == (char*)UTEMP + PGSIZE - 4); 
```

---

--
### The keyboard interface
For the shell to work, we need a way to type at it. In QEMU, input typed in the graphical window appear as input from the keyboard to JOS, while input typed to the console appear as characters on the serial port. `kern/console.c` already contains the keyboard and serial drivers that have been used by the kernel monitor since lab 1, now attach these to the rest of the system.

> Exercise 9. In your `kern/trap.c`, call `kbd_intr` to handle trap `IRQ_OFFSET+IRQ_KBD` and `serial_intr` to handle trap `IRQ_OFFSET+IRQ_SERIAL`.

In `trap_dispatch()`:

```c
	// ...
	// Handle keyboard and serial interrupts.
	// LAB 5: Your code here.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
		kbd_intr();
		return;
	}

	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
		serial_intr();
		return;
	}
	// ...
```

--

### The Shell
> **Exercise 10.** The shell doesn't support I/O redirection. It would be nice to run `sh <script` instead of having to type in all the commands in the script by hand, as you did above. Add I/O redirection for `<` to `user/sh.c`. 
> 
> Test your implementation by typing `sh <script` into your shell
> 
> Run make `run-testshell` to test your shell. testshell simply feeds the above commands (also found in `fs/testshell.sh`) into the shell and then checks that the output matches `fs/testshell.key`.

In `user/sh.c`'s `runcmd()`, simply:

```c
		case '<':	// Input redirection
			// Grab the filename from the argument list
			if (gettoken(0, &t) != 'w') {
				cprintf("syntax error: < not followed by word\n");
				exit();
			}
			// Open 't' for reading as file descriptor 0
			// (which environments use as standard input).
			// We can't open a file onto a particular descriptor,
			// so open the file as 'fd',
			// then check whether 'fd' is 0.
			// If not, dup 'fd' onto file descriptor 0,
			// then close the original 'fd'.
	
			// LAB 5: Your code here.
			if ((fd = open(t, O_RDONLY)) < 0) {
				cprintf("open %s for read: %e", t, fd);
				exit();
			}
			if (fd) {
				dup(fd, 0);
				close(fd);
			}
			break;
```
--
---
### Challenge
> ***Challenge!*** Implement Unix-style `exec`.

Because `exec` will cover the address space of the current process, we cannot implement `exec` without the help of kernel. My `exec` will read the ELF program file into memory, then kernel will take the rest work to finish ELF loading.

`exec` is quite similar to `spawn`, basically does the following steps (see `lib/exec.c` for code):

* Open the ELF file with filename `prog`
* Do sanity check on the ELF header
* Set up `void *nextpg = ROUNDUP((void *)end, PGSIZE)`
	* `end` points to the end of current program's `.bss` segment, and `nextpg` always points to next available virtual page
* alloc a page at `UTEMP` to store the mapping of segments and the stack (`struct SegmentInfo` in `inc/syscall.h`), which will be passed to the kernel
* Set up `Trapframe`
* Set up the stack, and store the stack page
* Set up segments in the ELF file
* Call `sys_env_load_elf` system call and pass the `Trapframe` and the mapping to the kernel

`sys_env_load_elf` sets the `Trapframe` of the current environment and maps pages according to the mapping. Now loading is finished and the current process will start running the new program.

For testing, I provide the test program `testexec`, which simply run call `exec` and pass the arguments it received. Run `make run-icode-nox` and type `testexec echo hello world`, the result is the same as running `echo hello world`.

Also, Lab 4's `sfork`'s conflict with `spawn` also exists here with `exec`, fix it as described above after Ex.8.

---

### Lab 5 IS COMPLETED.
