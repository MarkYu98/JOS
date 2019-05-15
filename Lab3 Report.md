## Lab3 Report
俞一凡 Yifan Yu 1600012998

*I completed this lab with 3 challenges, challenge 1 is detailed after Exercise 4, and the other two are detailed at the end of this report.*

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

Major new source files and content layout for this lab:

* `inc/`
	* `env.h`: Public definitions for user-mode environments
	* `trap.h`: Public definitions for trap handling
	* `syscall.h`: Public definitions for system calls from user environments to the kernel
	* `lib.h`: Public definitions for the user-mode support library
* `kern/`
	* `env.h`: Kernel-private definitions for user-mode environments
	* `env.c`: Kernel code implementing user-mode environments
	* `trap.h`: Kernel-private trap handling definitions
	* `trap.c`: Trap handling code
	* `trapentry.S`: Assembly-language trap handler entry-points
	* `syscall.h`: Kernel-private definitions for system call handling
	* `syscall.c`: System call implementation code
* `lib/`
	* `entry.S`: Assembly-language entry-point for user environments
	* `libmain.c`: User-mode library setup code called from `entry.S`
	* `syscall.c`: User-mode system call stub functions
	* `console.c`: User-mode implementations of putchar and getchar, providing console I/O
* `user/`: Various test programs to check kernel lab 3 code

This lab mainly implements the basic kernel facilities required to get a protected user-mode environment ("process") running. The JOS kernel will be enhanced to keep track of user environments, create user environment, load a program image into it, and start it running. The JOS kernel will also be capable of handling system calls and exceptions coming from user environments.


## Part A: User Environments and Exception Handling

#### Environment State

The kernel uses the `Env` (defined in `inc/env.h`) data structure to keep track of each user environment. This lab requires create just one environment initially and then design the kernel to support multiple environments.

In `kern/env.c`, three main global variables are designed to keep track of environments:

```c
struct Env *envs = NULL;		// All environments
struct Env *curenv = NULL;		// The current env
static struct Env *env_free_list;	// Free environment list
```

In our design, the JOS kernel will support a maximum of `NENV` (defined in `inc/env.h`) simultaneously active environments.

#### Allocating the Environments Array

> **Exercise 1.** Modify `mem_init()` in `kern/pmap.c` to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in `inc/memlayout.h`) so user processes can read from this array.
> 
> You should run your code and make sure `check_kern_pgdir()` succeeds.

Similar to initializing `kern_pgdir`, so simply add:

```c
	// Make 'envs' point to an array of size 'NENV' of 'struct Env'.
	// LAB 3: Your code here.
	envs = (struct Env *) boot_alloc(NENV * sizeof(struct Env));
```

And the code passes the `check_kern_pgdir()`.

--
#### Creating and Running Environments

Because JOS does not have a filesystem yett, the kernel will need to load a static binary image that is embedded within the kernel itself as a ELF executable image.

The `GNUmakefile` generates a number of binary images in the `obj/user/` directory. The `-b binary` option on the linker command line causes these files to be linked in as "raw" uninterpreted binary files.

> **Exercise 2.** In the file `env.c`, finish coding the following functions:
> 
> * `env_init()`  
> Initialize all of the `Env` structures in the `envs` array and add them to the `env_free_list`. Also calls `env_init_percpu`, which configures the segmentation hardware with separate segments for privilege level 0 (kernel) and privilege level 3 (user).  
> * `env_setup_vm()`  
> Allocate a page directory for a new environment and initialize the kernel portion of the new environment's address space.
> * `region_alloc()`  
> Allocates and maps physical memory for an environment.
> * `load_icode()`  
> You will need to parse an ELF binary image, much like the boot loader already does, and load its contents into the user address space of a new environment.
> * `env_create()`  
> Allocate an environment with `env_alloc` and call `load_icode` to load an ELF binary into it.
> * `env_run()`  
> Start a given environment running in user mode.  
> 
> As you write these functions, you might find the new `cprintf` verb `%e` useful -- it prints a description corresponding to an error code. For example,
> 
```c
r = -E_NO_MEM;
panic("env_alloc: %e", r);
```
> will panic with the message "env_alloc: out of memory".

It's quite useful to carefully read the instructions in the comment given. It's not hard to write the code for this exercise following the instructions:

* In `env_init()`:

	```c
		// Set up envs array
		// LAB 3: Your code here.
		for (ssize_t i = NENV-1; i >= 0; i--) {
			envs[i].env_link = env_free_list;
			env_free_list = &envs[i];
	
			envs[i].env_id = 0;
			envs[i].env_type = ENV_TYPE_USER;
			envs[i].env_status = ENV_FREE;
			envs[i].env_runs = 0;
			envs[i].env_pgdir = NULL;
		}
	
		// Per-CPU part of the initialization
		env_init_percpu();
	```
	
* In `env_setup_vm()`: Since all mapping above UTOP need to be mapped the same as `kern_pgdir`， we can use `memcpy()` to finish this task.

	```c
		// LAB 3: Your code here.
		e->env_pgdir = (pde_t *) page2kva(p);
		p->pp_ref++;
		memcpy(e->env_pgdir + PDX(UTOP), kern_pgdir + PDX(UTOP),
					PGSIZE - PDX(UTOP) * sizeof(pde_t));
	
		// UVPT maps the env's own page table read-only.
		// Permissions: kernel R, user R
		e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;
	```
	
* `region_alloc()`:
	
	```c
	static void
	region_alloc(struct Env *e, void *va, size_t len)
	{
		// LAB 3: Your code here.
		// (But only if you need it for load_icode.)
		//
		// Hint: It is easier to use region_alloc if the caller can pass
		//   'va' and 'len' values that are not page-aligned.
		//   You should round va down, and round (va + len) up.
		//   (Watch out for corner-cases!)
	
		void *rva = ROUNDDOWN(va, PGSIZE), *rend = ROUNDUP(va + len, PGSIZE);
		while (rva != rend) {
			struct PageInfo *pp = page_alloc(0);
			if (pp == NULL)
				panic("region_alloc: no free pages");
			int ret = page_insert(e->env_pgdir, pp, rva, PTE_W | PTE_U);
			if (ret != 0)
				panic("region_alloc: %e", ret);
			rva += PGSIZE;
		}
	}
	```

* `load_icode()`:  The code to load kernel program in `boot/main.c` is a really good reference to the function need to implement here. When allocating memory regions, need to use the correct page directory (in fact in this implementation the environment's page directory is used first by setting the `CR3` register using `lcr3(PADDR(e->env_pgdir))`, and then when finished, the kernel's page directory is switched back using `lcr3(PADDR(kern_pgdir))`.
	
	```c
		// LAB 3: Your code here.
		struct Elf *elfhdr = (struct Elf *) binary;
		if (elfhdr->e_magic != ELF_MAGIC)
			panic("load_icode: %p not valid ELF image!", binary);
	
		struct Proghdr *ph, *eph;
		ph = (struct Proghdr *)((uint8_t *)elfhdr + elfhdr->e_phoff);
		eph = ph + elfhdr->e_phnum;
		lcr3(PADDR(e->env_pgdir));
		for (; ph < eph; ph++)
			if (ph->p_type == ELF_PROG_LOAD) {
				region_alloc(e, (void *)ph->p_va, ph->p_memsz);
				memcpy((void *)ph->p_va, (uint8_t *)elfhdr + ph->p_offset,
						ph->p_filesz);
				memset((void *)ph->p_va + ph->p_filesz, 0,
						ph->p_memsz - ph->p_filesz);
			}
		lcr3(PADDR(kern_pgdir));
		e->env_tf.tf_eip = elfhdr->e_entry;
	
		// Now map one page for the program's initial stack
		// at virtual address USTACKTOP - PGSIZE.
	
		// LAB 3: Your code here.
		region_alloc(e, (void *)(USTACKTOP - PGSIZE), PGSIZE);
	```
	
* `env_create()`: 
	Quite simple for this one after the above helper functions are done.
	
	```c
		// LAB 3: Your code here.
		struct Env *e;
		env_alloc(&e, 0);
		load_icode(e, binary);
		e->env_type = type;
	```

* `env_run()`: 
	Set up the environment and run.
	
	```c
		// LAB 3: Your code here.
		if (curenv != NULL && curenv->env_status == ENV_RUNNING)
			curenv->env_status = ENV_RUNNABLE;
		curenv = e;
		curenv->env_status = ENV_RUNNING;
		curenv->env_runs++;
		lcr3(PADDR(curenv->env_pgdir));
	
		env_pop_tf(&curenv->env_tf);
	```

Now this exercise is finished.

The entire call graph of the code up to the point where the user code is invoked looks like this:

> * `start (kern/entry.S)`
> * `i386_init (kern/init.c)`
> 	* `cons_init`
> 	* `mem_init`
> 	* `env_init`
> 	* `trap_init` (still incomplete at this point)
> 	* `env_create`
> 	* `env_run`
> 		* `env_pop_tf`

Compile the kernel and run under QEMU. Using GDB to set a breakpoint at `env_pop_tf` and single step afterwards, the processor enters user mode after the `iret` instruction. Use `b *0x...` to set a breakpoint at the `int $0x30` in `sys_cputs()` in `hello`, and the system runs all fine before the breakpoint. Continue executing results in a triple fault because system calls has not been implemented yet.

--

#### Handling Interrupts and Exceptions

Now we need to implement basic exception and system call handling for the kernel to recover control of the processor from user-mode code.

> **Exercise 3.** Read [Chapter 9, Exceptions and Interrupts](https://pdos.csail.mit.edu/6.828/2018/readings/i386/c09.htm) in the [80386 Programmer's Manual](https://pdos.csail.mit.edu/6.828/2018/readings/i386/toc.htm) (or Chapter 5 of the [IA-32 Developer's Manual](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3A.pdf)), if you haven't already.

This exercise requires read docs to get familiar with the x86 interrupt and exception mechanism.

--

#### Basics of Protected Control Transfer
Exceptions and interrupts are both "protected control transfers", this protection to prevent the user-mode code from interfering with the functioning of the kernel or other environments are provided by two mechanisms on x86:
 
1. **The Interrupt Descriptor Table.**
The x86 allows up to 256 different interrupt or exception entry points into the kernel, each with a different *interrupt vector*, which contains:
	* The value to load into the instruction pointer (EIP) register, pointing to the kernel handler code.
	* the value to load into the code segment (CS) register, which includes in bits 0-1 the privilege level at which the exception handler is to run. (In JOS, all exceptions are handled in kernel mode, privilege level 0.)
2. **The Task State Segment.** When an x86 processor takes an interrupt or trap that causes a privilege level change from user to kernel mode, it also switches to a stack in the kernel's memory. A structure called the *task state segment* (TSS) specifies the segment selector and address where this stack lives. 

#### Types of Exceptions and Interrupts and Examples
See the original instruction on the lab webpage.

#### Setting Up the IDT
We now set up the IDT to handle interrupt vectors 0-31 (the processor exceptions). Each exception or interrupt will have its own handler in `trapentry.S`, and `trap_init()` initializes the IDT with the addresses of these handlers. Each of the handlers build a `struct Trapframe` (see `inc/trap.h`) on the stack and call `trap()`.

> **Exercise 4.** Edit `trapentry.S` and `trap.c` and implement the features described above. The macros `TRAPHANDLER` and `TRAPHANDLER_NOEC` in `trapentry.S` should help you, as well as the `T_*` defines in `inc/trap.h`. You will need to add an entry point in `trapentry.S` (using those macros) for each trap defined in `inc/trap.h`, and you'll have to provide `_alltraps` which the `TRAPHANDLER` macros refer to. You will also need to modify `trap_init()` to initialize the idt to point to each of these entry points defined in `trapentry.S`; the `SETGATE` macro will be helpful here.
> 
> Your `_alltraps` should:
> 
> 1. push values to make the stack look like a `struct Trapframe`
> 2. load `GD_KD` into `%ds` and `%es`
> 3. `pushl %esp` to pass a pointer to the `Trapframe` as an argument to `trap()`
> 4. call `trap` (can `trap` ever return?)  
> 
> Consider using the `pushal` instruction; it fits nicely with the layout of the struct Trapframe.
> 
> Test your trap handling code using some of the test programs in the `user` directory that cause exceptions before making any system calls, such as `user/divzero`. You should be able to get `make grade` to succeed on the `divzero`, `softint`, and `badsegment` tests at this point.

Using the macros provided, creating an exception handler is easy:

* An exception with error code, for example `TRAPHANDLER(dblflt_handler, T_DBLFLT)`
* An exception without  error code, for example `TRAPHANDLER_NOEC(divide_handler, T_DIVIDE)`

In `trap_init()`, register these handler using the `SETGATE` macro like:

```c
extern void divide_handler();
extern void debug_handler();
...
extern void simderr_handler();
extern void syscall_handler();

SETGATE(idt[T_DIVIDE], 1, GD_KT, divide_handler, 0);
SETGATE(idt[T_DEBUG], 1, GD_KT, debug_handler, 0);
...
SETGATE(idt[T_SIMDERR], 1, GD_KT, debug_handler, 0);
SETGATE(idt[T_SYSCALL], 1, GD_KT, debug_handler, 3);
```

All the `*_handler()` are symbols defined in `trapentry.S`.

After these, the code is able to pass the tests.

---
### Challenge 1
> ***Challenge!*** You probably have a lot of very similar code right now, between the lists of `TRAPHANDLER` in `trapentry.S` and their installations in `trap.c`. Clean this up. Change the macros in `trapentry.S` to automatically generate a table for `trap.c` to use. Note that you can switch between laying down code and data in the assembler by using the directives `.text` and `.data`.

As suggested by the instruction, we can automatically generate a table in `trapentry.S` by changing the macros to:

```asm
#define TRAPHANDLER(name, num)						\
	.text;									\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps; 							\
	.data;									\
	.long name;

#define TRAPHANDLER_NOEC(name, num)					\
	.text;								\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps; 						\
	.data;								\
	.long name;
```

Notice the use of `.text` and `.data` directives.

Now create a symbol of the table for `trap.c` to use:

```asm
.data
.global	idttable
idttable:

TRAPHANDLER_NOEC(divide_handler, T_DIVIDE)
...
TRAPHANDLER_NOEC(syscall_handler, T_SYSCALL)
```

And now the code in `trap_init()` can become very elegant:

```c
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.
	extern uintptr_t idttable[];
	for (int i = 0; i < 20; i++) {
		if (i == T_BRKPT)
			SETGATE(idt[i], 1, GD_KT, idttable[i], 3)
		else
			SETGATE(idt[i], 1, GD_KT, idttable[i], 0)
	}
	SETGATE(idt[T_SYSCALL], 1, GD_KT, idttable[20], 3)

	// Per-CPU setup
	trap_init_percpu();
}
```

---

> **Questions**: 
> 
> 1. What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)
> 2. Did you have to do anything to make the user/softint program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says `int $14`. Why should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

1. I think there are mainly two reasons:
	1. By definition, some exceptions have error code while others don't.
	2. DPL (descriptor privilege level) might be different for each handler.
	
2. Yes, all exceptions need to be set with `DPL=0` except `T_SYSCALL` and `T_BRKPT`(with `DPL=3`, which allows direct call by user environment). The user program cannot directly invoke the page fault exception handlers. So calling `int $14` will result in a general protection fault.
	If the kernel allows `int $14` call to invoke page fault handler, buggy/malicious program will be able to mess with physical memory which might be used by other environments or the kernel.
	
--

### This concludes part A of the lab.

<div STYLE="page-break-after: always;"></div>

## Part B: Page Faults, Breakpoints Exceptions, and System Calls

Now refine the kernel to provide important operating system primitives that depend on exception handling.

#### Handling Page Faults & The Breakpoint Exception
> **Exercise 5.** Modify `trap_dispatch()` to dispatch page fault exceptions to `page_fault_handler()`. You should now be able to get make grade to succeed on the `faultread`, `faultreadkernel`, `faultwrite`, and `faultwritekernel` tests. If any of them don't work, figure out why and fix them. Remember that you can boot JOS into a particular user program using `make run-x` or `make run-x-nox`. For instance, `make run-hello-nox` runs the hello user program.
> 
> **Exercise 6.** Modify `trap_dispatch()` to make breakpoint exceptions invoke the kernel monitor. You should now be able to get make grade to succeed on the breakpoint test.

The code to implement `trap_dispatch()` is quite simple:

```c
	// LAB 3: Your code here.
	if (tf->tf_trapno == T_PGFLT) {
		page_fault_handler(tf);
		return;
	}
	if (tf->tf_trapno == T_BRKPT) {
		monitor(tf);
		return;
	}
```

--
> **Question 3.** The break point test case will either generate a break point exception or a general protection fault depending on how you initialized the break point entry in the IDT (i.e., your call to `SETGATE` from `trap_init`). Why? How do you need to set it up in order to get the breakpoint exception to work as specified above and what incorrect setup would cause it to trigger a general protection fault?

The DPL of the breakpoint handler need to be set to 3 in the IDT to work properly. This is because that user environment use `int $3` to directly invoke the handler. It will trigger a general protection fault otherwise.

--

> **Question 4.** What do you think is the point of these mechanisms, particularly in light of what the `user/softint` test program does?

These mechanisms are designed with both convenience and safety/protection considered. Exceptions like breakpoint can help programmers to debug, while others need to be carefully protected.

--
#### System Calls
> User processes ask the kernel to do things for them by invoking system calls. When the user process invokes a system call, the processor enters kernel mode, the processor and the kernel cooperate to save the user process's state, the kernel executes appropriate code in order to carry out the system call, and then resumes the user process.

In JOS, `int $0x30` is the system call interrupt.

The application will pass the system call number and the system call arguments in registers. The kernel won't need to grub around in the user environment's stack or instruction stream. The system call number will go in `%eax`, and the arguments (up to five of them) will go in `%edx`, `%ecx`, `%ebx`, `%edi`, and `%esi`, respectively. The kernel passes the return value back in `%eax`. This is what the `syscall()` in `lib/syscall.c` does:

```c
	asm volatile("int %1\n"
			     : "=a" (ret)
			     : "i" (T_SYSCALL),
			       "a" (num),
			       "d" (a1),
			       "c" (a2),
			       "b" (a3),
			       "D" (a4),
			       "S" (a5)
			     : "cc", "memory");
```

> **Exercise 7.** Add a handler in the kernel for interrupt vector `T_SYSCALL`. You will have to edit `kern/trapentry.S` and `kern/trap.c`'s `trap_init()`. You also need to change `trap_dispatch()` to handle the system call interrupt by calling `syscall()` (defined in `kern/syscall.c`) with the appropriate arguments, and then arranging for the return value to be passed back to the user process in `%eax`. Finally, you need to implement `syscall()` in `kern/syscall.c`. Make sure syscall() returns `-E_INVAL` if the system call number is invalid. You should read and understand `lib/syscall.c` (especially the inline assembly routine) in order to confirm your understanding of the system call interface. Handle all the system calls listed in `inc/syscall.h` by invoking the corresponding kernel function for each call.
> 
> Run the `user/hello` program under your kernel (make run-hello). It should print "hello, world" on the console and then cause a page fault in user mode. If this does not happen, it probably means your system call handler isn't quite right. You should also now be able to get make grade to succeed on the `testbss` test.

The `syscall` trap handler code is already taken care of in previous exercises, see above. The job of `syscall()` in `kern/syscall.c` is simply dispatch the call to corresponding kernel routines:

```c
// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((char *) a1, (size_t) a2);
		return a1;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		return sys_getenvid();
	case SYS_env_destroy:
		return sys_env_destroy((envid_t) a1);
	default:
		return -E_INVAL;
	}
}
```

Run `user/hello` under the kernel, it will now print "hello, world" on the console and then cause a page fault in user mode. And `make grade` also passes the tests for now.

--
#### User-mode startup
A user program starts running at the top of `lib/entry.S`. After some setup, this code calls `libmain()`, in `lib/libmain.c`. Now modify `libmain()` to initialize the global pointer `thisenv` to point at this environment's `struct Env` in the `envs[]` array.

> **Exercise 8.** Add the required code to the user library, then boot your kernel. You should see `user/hello` print "hello, world" and then print `i am environment 00001000`. `user/hello` then attempts to "exit" by calling `sys_env_destroy()` (see `lib/libmain.c` and `lib/exit.c`). Since the kernel currently only supports one user environment, it should report that it has destroyed the only environment and then drop into the kernel monitor. You should be able to get `make grade` to succeed on the `hello `test.

In `libmain()`, simply add:

```c
	// LAB 3: Your code here.
	thisenv = &envs[ENVX(sys_getenvid())];
```

--
#### Page faults and memory protection
> **Exercise 9.** Change `kern/trap.c` to panic if a page fault happens in kernel mode.
> 
> Hint: to determine whether a fault happened in user mode or in kernel mode, check the low bits of the `tf_cs`.
> 
> Read `user_mem_assert` in `kern/pmap.c` and implement `user_mem_check` in that same file.
> 
> Change `kern/syscall.c` to sanity check arguments to system calls.
> 
> Boot your kernel, running `user/buggyhello`. The environment should be destroyed, and the kernel should not panic. You should see:
> 
> ```
[00001000] user_mem_check assertion failure for va 00000001
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```
> 
> Finally, change `debuginfo_eip` in `kern/kdebug.c` to call `user_mem_check` on `usd`, `stabs`, and `stabstr`. If you now run `user/breakpoint`, you should be able to run `backtrace` from the kernel monitor and see the backtrace traverse into `lib/libmain.c` before the kernel panics with a page fault. What causes this page fault? You don't need to fix it, but you should understand why it happens.

In `page_fault_handler()`, detect and panic on kernel-mode page fault:

```c
	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if (!(tf->tf_cs & 3))
		panic("Kernel-mode page faults!");
```

For the `user_mem_check`, thoroughly consider all address conditions and permisions, also take care of overflow:

```c
int
user_mem_check(struct Env *env, const void *va, size_t len, int perm)
{
	// LAB 3: Your code here.
	size_t endva = ROUNDUP((size_t) va + len, PGSIZE);
	size_t curva = (size_t) va;
	if (curva + len < curva) {	// Overflow
		user_mem_check_addr = curva;
		return -E_FAULT;
	}
	while (curva < endva) {
		if (curva >= ULIM) {
			user_mem_check_addr = MAX((size_t) va, ULIM);
			return -E_FAULT;
		}
		pte_t *pte_p = pgdir_walk(env->env_pgdir, (void *)curva, 0);
		if (!pte_p || !(*pte_p & PTE_P) || (*pte_p & perm) != perm) {
			user_mem_check_addr = curva;
			return -E_FAULT;
		}
		curva += PGSIZE;
		curva = ROUNDDOWN(curva, PGSIZE);
	}
	return 0;
}
```

Sanity check in `sys_cputs()` in `kern/syscall.c`:

```c
	// LAB 3: Your code here.
	user_mem_assert(curenv, (void *)s, len, PTE_U);
```

The `backtrace` will cause a page fault is because backtrace runs in kernel mode, and it touches the page [USTACKTOP - PGSIZE, USTACKTOP)，which is not allocated for protection.

--
> **Exercise 10.** Boot your kernel, running `user/evilhello`. The environment should be destroyed, and the kernel should not panic. You should see:
>
	[00000000] new env 00001000
	...
	[00001000] user_mem_check assertion failure for va f010000c
	[00001000] free env 00001000

Run the kernel, it does as the above. 

Run `make grade` and all tests are passed by now.

---
### Challenge 2
> ***Challenge!*** Modify the JOS kernel monitor so that you can 'continue' execution from the current location (e.g., after the `int 3`, if the kernel monitor was invoked via the breakpoint exception), and so that you can single-step one instruction at a time. You will need to understand certain bits of the `EFLAGS` register in order to implement single-stepping.

The `TF` bit (Trap flag, mask `0x0100`) is used for the single-step control provided by the processor. So all we need to do is to add two commands to the kernel monitor, in `monitor.c`:

```c
/***** Lab3 breakpoint continue and single-step challenge *****/

int
mon_continue(int argc, char **argv, struct Trapframe *tf) {
	if (!tf) {
		cprintf("Error: No program running!\n");
		return 1;	// Error
	}

	tf->tf_eflags &= ~(1 << 8);
	env_run(curenv);

	// should never reach here
	return 0;
}

int
mon_step(int argc, char **argv, struct Trapframe *tf) {
	if (!tf) {
		cprintf("Error: No program running!\n");
		return 1;	// Error
	}

	tf->tf_eflags |= (1 << 8);
	env_run(curenv);

	// should never reach here
	return 0;
}
```

I also wrote a test program `user/singlestep.c`:

```c
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
```
Change the `Makefrag` to compile this and link the binary into kernel, test using the monitor and all is good. The info printed by the `print_trapframe()` may be too much and therefore very distracting, but to keep with the grading script and the main part of the lab, I did not change this.

---
### Challenge 3
> ***Challenge!*** Implement system calls using the `sysenter` and `sysexit` instructions instead of using `int 0x30` and `iret`.
>
> The `sysenter`/`sysexit` instructions were designed by Intel to be faster than `int`/`iret`. They do this by using registers instead of the stack and by making assumptions about how the segmentation registers are used. The exact details of these instructions can be found in [Volume 2B of the Intel reference manuals](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3B.pdf).
>
> The easiest way to add support for these instructions in JOS is to add a `sysenter_handler` in `kern/trapentry.S` that saves enough information about the user environment to return to it, sets up the kernel environment, pushes the arguments to `syscall()` and calls `syscall()` directly. Once `syscall()` returns, set everything up for and execute the `sysexit` instruction. You will also need to add code to `kern/init.c` to set up the necessary model specific registers (MSRs). Section 6.1.2 in Volume 2 of the AMD Architecture Programmer's Manual and the reference on SYSENTER in Volume 2B of the Intel reference manuals give good descriptions of the relevant MSRs. You can find an implementation of `wrmsr` to add to `inc/x86.h` for writing to these MSRs [here](http://ftp.kh.edu.tw/Linux/SuSE/people/garloff/linux/k6mod.c).

Follow the fairly thorough instructions and read the reference manuals, I first initialize related MSRs in `trap_init()` (instead of in `kern/init.c` that calls `trap_init()` which makes more sense):

```c
	// Lab3 sysenter/sysexit challenge
	wrmsr(IA32_SYSENTER_CS, GD_KT, 0);  // IA32_SYSENTER_CS = GD_KT
    wrmsr(IA32_SYSENTER_ESP, KSTACKTOP, 0); // IA32_SYSENTER_ESP = KSTACKTOP
    wrmsr(IA32_SYSENTER_EIP, (uint32_t)sysenter_handler, 0);  // IA32_SYSENTER_EIP = sysenter_handler
``` 

Where `IA32_SYSENTER_*` and `wrmsr` are defined in `kern/x86.h`:

```c
#define rdmsr(msr,val1,val2) \
	__asm__ __volatile__("rdmsr" \
	: "=a" (val1), "=d" (val2) \
	: "c" (msr))

#define wrmsr(msr,val1,val2) \
	__asm__ __volatile__("wrmsr" \
	: /* no outputs */ \
	: "c" (msr), "a" (val1), "d" (val2))

/***** For lab3 sysenter challenge *****/
#define	IA32_SYSENTER_CS	372
#define IA32_SYSENTER_ESP	373
#define IA32_SYSENTER_EIP	374
```

And `sysenter_handler` is a symbol defined later in `trapentry.S`, so should be defined as `extern void sysenter_handler(void);` in `trap.c`.

Now add a `sysenter_handler` in `trapentry.S`:

```asm
/*
 * lab 3 challenge: sysenter/sysexit
 * - take care of interrupt
 */

.text
.globl sysenter_handler
.type name, @function
.align 2
sysenter_handler:
	pushl 	%esi
	pushl 	%ebp
	pushl 	%edi
	pushl 	%ebx
	pushl 	%ecx
	pushl 	%edx
	pushl 	%eax
	movw	$(GD_KD),%ax
	movw	%ax,%es
	movw 	%ax,%ds
	call	syscall

	movw	$(GD_UD),%bx
	movw	%bx,%es
	movw 	%bx,%ds
	addl	$20,%esp
	popl	%ecx
	popl	%edx
	sysexit
```
What this handler does is: first push user environment's `%eip` and `%esp` into stack, and then change the data segment to kernel data segment. After setting up the environment, it pushes syscall arguments and call `syscall` in `lib/syscall.c`. Finally, it changes the data segment to user data segment, restore user environment's `%eip` and `%esp` and return using `sysexit`.

In `lib/syscall.c`, I added a `sysenter` function similar to the original `syscall`, replacing the inline assembly part with:

```c
	asm volatile(
			"pushl %%ebp\n"
			"movl %%esp,%%ebp\n"
			"leal after_sysenter_label, %%esi\n"
			"sysenter\n"
			"after_sysenter_label:\n"
			"popl %%ebp\n"
		     : "=a" (ret)
		     : "a" (num),
		       "d" (a1),
		       "c" (a2),
		       "b" (a3),
		       "D" (a4)
		     : "%esi", "cc", "memory");
```

Finally, change each syscall functions in `lib/syscall.c` to call `sysenter()` instead of `syscall()` to perform the syscall into kernel.

And this challenge is finished, simply run by `make grade` to see that everything is still good.

---
### Lab 3 IS COMPLETED.
