## Lab4 Report
俞一凡 Yifan Yu 1600012998

*I completed this lab with 4 challenges. All challenges are reported at the end of this report.*

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

New source files for this lab:

* `kern/cpu.h`: Kernel-private definitions for multiprocessor support
* `kern/mpconfig.c`: Code to read the multiprocessor configuration
* `kern/lapic.c`: Kernel code driving the local APIC unit in each processor
* `kern/mpentry.S`: Assembly-language entry code for non-boot CPUs
* `kern/spinlock.h`: Kernel-private definitions for spin locks, including the big kernel lock
* `kern/spinlock.c`: Kernel code implementing spin locks
* `kern/sched.c`: Code skeleton of the scheduler to be implemented.

This lab is divided into three parts: **Part A** implements the multiprocessor support and cooperative multitasking, allowing user-level environments to create additional new environments, with cooperative round-robin scheduling. **Part B** implements the copy-on-write `fork()`, to reduce the cost when copying the parent's address space when forking a child. **Part C** implements preemptive multitasking and Inter-Process Communication (IPC), enabling kernel to take back control from uncooperative environments, and allowing environments to pass messages to each other explicitly.


## Part A: Multiprocessor Support and Cooperative Multitasking

#### Multiprocessor Support

JOS uses "symmetric multiprocessing" (SMP) multiprocessor model, in which all CPUs have equivalent access to system resources such as memory and I/O buses. All CPUs are functionally identical, but classified into two types during the boot process: the bootstrap processor (BSP) to initialize the system and boot the OS, and the application processors (APs) are activated by the BSP after the OS is up and running. All previous JOS code has been running on the BSP.

In an SMP system, each CPU has an accompanying local APIC (LAPIC) unit to deliever interrupts throughout the system. In this lab, the following basic functionality of the LAPIC unit will be used (in `kern/lapic.c`):

* Reading the LAPIC identifier (APIC ID) to tell which CPU our code is currently running on (`cpunum()`).
* Sending the STARTUP interprocessor interrupt (IPI) from the BSP to the APs to bring up other CPUs (`lapic_startap()`).
* In part C, LAPIC's built-in timer will be used to trigger clock interrupts to support preemptive multitasking (`apic_init()`).

A processor accesses its LAPIC using memory-mapped I/O (MMIO). In MMIO, a portion of physical memory is hardwired to the registers of some I/O devices, so the same load/store instructions used to access memory can be used to access device registers. The LAPIC lives in a hole starting at physical address `0xFE000000` (32MB short of 4GB), so it's too high to access using direct map at KERNBASE. The JOS virtual memory map leaves a 4MB gap at MMIOBASE to map devices like this. 

> **Exercise 1.** Implement `mmio_map_region` in `kern/pmap.c`. To see how this is used, look at the beginning of `lapic_init` in `kern/lapic.c`. You'll have to do the next exercise, too, before the tests for `mmio_map_region` will run.

Simply use the previously implemented `boot_map_region` with the corresponding parameters to map the region, add some validition checks.

```c
void *
mmio_map_region(physaddr_t pa, size_t size)
{
	// ...
	static uintptr_t base = MMIOBASE;
	// ...
	// Your code here:
	uintptr_t curbase = base;
	size = ROUNDUP(size, PGSIZE);

	if (base + size > MMIOLIM)
		panic("mmio_map_region: overflow MMIOLIM!");

	boot_map_region(kern_pgdir, base, size, pa, PTE_PCD | PTE_PWT | PTE_W);
	base += size;
	return (void *) curbase;
}
```

--
#### Application Processor Bootstrap

Before booting up APs, the BSP should first collect information about the multiprocessor system: total number of CPUs, APIC IDs, MMIO address of the LAPIC unit. The `mp_init()` function in `kern/mpconfig.c` does this by reading the MP configuration table that resides in the BIOS's region of memory.

The `boot_aps()` (in `kern/init.c`) drives the AP bootstrap process. APs start in real mode, much like how the bootloader started in `boot/boot.S`, so `boot_aps()` copies the AP entry code (`kern/mpentry.S`) to a memory location that is real-mode-addressable. The entry code is copied to `0x7000` (`MPENTRY_PADDR`), but any unused, page-aligned physical address below `640KB` would work.

After that, `boot_aps()` activates APs by sending `STARTUP` IPIs to the LAPIC unit of the AP, along with an initial `CS:IP` address at which the AP should start running its entry code (`MPENTRY_PADDR`). The entry code in `kern/mpentry.S` is quite similar to `boot/boot.S`. After some brief setup, it puts the AP into protected mode with paging enabled, and then calls the C setup routine `mp_main()` (also in `kern/init.c`). `boot_aps()` waits for the AP to signal a `CPU_STARTED` flag in `cpu_status` field of its `struct CpuInfo` before going on to wake up the next one.

> **Exercise 2.** Read `boot_aps()` and `mp_main()` in `kern/init.c`, and the assembly code in `kern/mpentry.S`. Make sure you understand the control flow transfer during the bootstrap of APs. Then modify your implementation of `page_init()` in `kern/pmap.c` to avoid adding the page at `MPENTRY_PADDR` to the free list, so that we can safely copy and run AP bootstrap code at that physical address. Your code should pass the updated `check_page_free_list()` test (but might fail the updated `check_kern_pgdir()` test, which we will fix soon).

Just follow the instruction, avoid adding the page at `MPENTRY_PADDR` to the free list. The code pass the updated `check_page_free_list()`.

--

> **Question 1.** Compare `kern/mpentry.S` side by side with `boot/boot.S`. Bearing in mind that `kern/mpentry.S` is compiled and linked to run above `KERNBASE` just like everything else in the kernel, what is the purpose of macro `MPBOOTPHYS`? Why is it necessary in `kern/mpentry.S` but not in `boot/boot.S`? In other words, what could go wrong if it were omitted in `kern/mpentry.S`? 
> 
> Hint: recall the differences between the link address and the load address that we have discussed in Lab 1.

Because `kern/mpentry.S` is linked above `KERNBASE`, which APs in real mode after startup cannot access directly. Macro `MPBOOTPHYS(s)` is needed to calculate the low physical address based on the high virtual address of the symbol `s`. `boot/boot.S` is loaded at `0x7c00`, a low physical address, it does not need this macro to translate the addresses.

--
#### Per-CPU State and Initialization

Since JOS becomes multi-processor, it's necessary to distinguish between per-CPU state and global state. Most of the per-CPU state are defined in `kern/cpu.h`'s `struct CpuInfo`.

The following per-CPU states are the most important:

* Per-CPU kernel stack: the array `percpu_kstacks[NCPU][KSTKSIZE]` reserves space for `NCPU`'s worth of kernel stacks.
* Per-CPU TSS and TSS descriptor: A per-CPU task state segment (TSS) is needed to specify where each CPU's kernel stack lives. The TSS for CPU i is stored in `cpus[i].cpu_ts`, and the corresponding TSS descriptor is defined in the GDT entry `gdt[(GD_TSS0 >> 3) + i]`.
* Per-CPU current environment pointer: Now the symbol `curenv` refers to `cpus[cpunum()].cpu_env` (or `thiscpu->cpu_env`).
* Per-CPU system registers: Functions `env_init_percpu()` and `trap_init_percpu()` initialize each CPU's private registers.

> **Exercise 3.** Modify `mem_init_mp()` (in `kern/pmap.c`) to map per-CPU stacks starting at `KSTACKTOP`, as shown in `inc/memlayout.h`. The size of each stack is `KSTKSIZE` bytes plus `KSTKGAP` bytes of unmapped guard pages. Your code should pass the new check in `check_kern_pgdir()`.

Just follow the instructions and use `boot_map_region`, quite simple:

```c
static void
mem_init_mp(void)
{
	// LAB 4: Your code here:
	for (int i = 0; i < NCPU; i++) {
		uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
		boot_map_region(kern_pgdir, kstacktop_i - KSTKSIZE,
					ROUNDUP(KSTKSIZE, PGSIZE), PADDR(percpu_kstacks[i]),
					PTE_W | PTE_P);
	}
}
```
--
> **Exercise 4.** The code in `trap_init_percpu()` (`kern/trap.c`) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs. (Note: your new code should not use the global `ts` variable any more.)

Now each CPU has its own kernel stack, and own TSS slot, we just have to change the code by calculating the stack top for each kernel stack, and using corresponding CPU's `cpu_ts` instead of a global `ts` to do this:

```c
	// ...
	// LAB 4: Your code here:
	uintptr_t kstacktop_i = KSTACKTOP - cpunum() * (KSTKSIZE + KSTKGAP);

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	thiscpu->cpu_ts.ts_esp0 = kstacktop_i;
	thiscpu->cpu_ts.ts_ss0 = GD_KD;
	thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

	// Initialize the TSS slot of the gdt.
	gdt[(GD_TSS0 >> 3) + cpunum()] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
					sizeof(struct Taskstate) - 1, 0);
	gdt[(GD_TSS0 >> 3) + cpunum()].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0 + (cpunum() << 3));

	// Load the IDT
	lidt(&idt_pd);
	
	// ...
```

Also, for ***Lab 3's sysenter/sysexit challenge*** to continue to work properly, need to change the `IA32_SYSENTER_ESP` of each CPU's model specific register (MSR) to CPU's own stack top, i.e. previously:

```c
wrmsr(IA32_SYSENTER_ESP, KSTACKTOP, 0); // IA32_SYSENTER_ESP = KSTACKTOP
```

Now becomes:

```c
wrmsr(IA32_SYSENTER_ESP, kstacktop_i, 0); // IA32_SYSENTER_ESP = kstacktop_i
```

And for ***Lab2's 4MB Page challenge***, initialize the `PS` bit support for each CPU at corresponding place: See `enable_PS_percpu()` in `kern/pmap.c`, which is called in `mem_init()` for the BSP and `mp_main()` for APs.

Run `make qemu-nox CPUS=4`, and get:

```shell
...
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 2 starting
SMP: CPU 3 starting
```
--

#### Locking
JOS uses the simplest way to avoid racing (multiple CPUs run kernel code simultaneously): a *big kernel lock*. Thus, environments in user mode can run concurrently on any available CPUs, but no more than one environment can run in kernel mode; any other environments that try to enter kernel mode are forced to wait.

The big kernel lock `kernel_lock` is defined in `kern/spinlock.h`, use `lock_kernel()` and `unlock_kernel()` to acquire and release the lock. The big lock is applied at four locations:

* In `i386_init()`, acquire the lock before the BSP wakes up the other CPUs.
* In `mp_main()`, acquire the lock after initializing the AP, and then call `sched_yield()` to start running environments on this AP.
* In `trap()`, acquire the lock when trapped from user mode, indicated by the low bits of the `tf_cs`.
* In `env_run()`, release the lock before switching to user mode. 

> **Exercise 5.** Apply the big kernel lock as described above, by calling `lock_kernel()` and `unlock_kernel()` at the proper locations.

In addition to the four locations above, to make ***Lab 3's sysenter/sysexit challenge*** to work properly, the workflow of `sysenter_handler` in `kern/trapentry.S` has to be modified to:

*disable interrupts $\rightarrow$ acquire the kernel lock $\rightarrow$ call syscall $\rightarrow$ release the kernel lock $\rightarrow$ enable interrupts $\rightarrow$ call sysexit*

See `kern/trapentry.S` for detail.

--
> **Question 2.** It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.

Before acquiring the big kernel lock, a CPU has to save the context (`TrapFrame`) of the trapping user environment in the kernel stack. If all CPU shares one kernel stack and one CPU is saving the context when another user program traps and tries to save in the kernel stack, the stack would be a total mess.

--
#### Round-Robin Scheduling

* The function `sched_yield()` in the new `kern/sched.c` is responsible for selecting a new environment to run. It searches sequentially through the `envs[]` array in circular fashion, starting just after the previously running environment (or at the beginning of the array if there was no previously running environment), picks the first environment it finds with a status of `ENV_RUNNABLE`, and calls `env_run()` to jump into that environment.
* `sched_yield()` must never run the same environment on two CPUs at the same time. If an environment is currently running on some CPU (possibly the current CPU), the environment's status will be `ENV_RUNNING`.
* User environments can call a new system call `sys_yield()` to invoke the kernel's `sched_yield()` function and thereby voluntarily give up the CPU to a different environment.

> Exercise 6. Implement round-robin scheduling in `sched_yield()` as described above. Don't forget to modify `syscall()` to dispatch `sys_yield()`.
> Make sure to invoke `sched_yield()` in `mp_main`.
> Modify `kern/init.c` to create three (or more!) environments that all run the program `user/yield.c`.

* In `sched_yield()`, choose the `Env` to run in a round-robin fashion:

```c
void
sched_yield(void)
{
	// ...
	// LAB 4: Your code here.
	int cur_env_id = -1;
	if (curenv)
		cur_env_id = ENVX(curenv->env_id);
	for (int i = cur_env_id + 1; i < NENV; i++)
		if (envs[i].env_status == ENV_RUNNABLE)
			env_run(&envs[i]);
	for (int i = 0; i < cur_env_id; i++)
		if (envs[i].env_status == ENV_RUNNABLE)
			env_run(&envs[i]);
	if (cur_env_id >= 0 && envs[cur_env_id].env_status == ENV_RUNNING)
		env_run(&envs[cur_env_id]);

	// sched_halt never returns
	sched_halt();
}
```

* In `mp_main()`:

```c
void
mp_main(void)
{
	// ...
	// Now that we have finished some basic setup, call sched_yield()
	// to start running processes on this CPU.  But make sure that
	// only one CPU can enter the scheduler at a time!
	//
	// Your code here:
	lock_kernel();
	sched_yield();
}
```

* To test, in `kern/init.c`'s `i386_init()`, create 3 environments that all run `user/yield.c`:

```c
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	// ENV_CREATE(user_primes, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
#endif // TEST*
```

Now run JOS, the result matches expectations.

--

> **Question 3.** In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch?

Because `kern_pgdir` and environment's `e->env_pgdir` have the same mapping in the area above `KERNBASE`, where the `envs[]` array is stored in memory. So, switching the page directory does not affect dereferencing pointer `e`.

--
> **Question 3.** Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?

Kernel and environments all need to use CPU's registers to run properly, and have their own registers' states when being switched. Environments are not able to save these states (context) by themselves, so this must be done by the kernel. When interrupt happens during an environment is running, or the environment makes a syscall, CPU will dump the values of all registers to the Trapframe of this enviornment.

--
#### System Calls for Environment Creation
Now implement the necessary JOS system calls to allow user environments to create and start other new user environments.

JOS uses a more primitive set of system calls for creating new user-mode environments. The functionality of Unix-like `fork()` will be implemented entirely in user space.

* `sys_exofork`: This system call creates a new environment with an almost blank slate: nothing is mapped in the user portion of its address space, and it is not runnable.
* `sys_env_set_status`: Sets the status of a specified environment to `ENV_RUNNABLE` or `ENV_NOT_RUNNABLE`.
* `sys_page_alloc `: Allocates a page of physical memory and maps it at a given virtual address in a given environment's address space.
* `sys_page_map`: Copy a page mapping (not the contents of a page!) from one environment's address space to another, leaving a memory sharing arrangement in place so that the new and the old mappings both refer to the same page of physical memory.
* `sys_page_unmap `: Unmap a page mapped at a given virtual address in a given environment.

For system calls that accept environment IDs, a value of 0 means the current environment, as implemented in `envid2env()` in `kern/env.c`.

> **Exercise 7.** Implement the system calls described above in `kern/syscall.c` and make sure `syscall()` calls them. You will need to use various functions in `kern/pmap.c` and `kern/env.c`, particularly `envid2env()`. For now, whenever you call `envid2env()`, pass `1` in the `checkperm` parameter. Be sure you check for any invalid system call arguments, returning `-E_INVAL` in that case. Test your JOS kernel with `user/dumbfork` and make sure it works before proceeding.

Just follow the instruction and function helper comments, see `kern/syscall.c` for implementation details. Checking invalid arguments can be tricky, need to take care of that. Dispatch the system calls in `syscall()`.

Test: `user/dumbfork` works as expected.

--
Now Part A of the lab is completed. Run `make grade` and get `5/5` on part A.

## Part B: Copy-on-Write Fork

Unix `fork()` system call copies the address space of the calling process (the parent) to create a new process (the child). However, simply copying the parent's address space into the child could be very expensive (as in xv6 Unix), and in fact a call to `fork()` is frequently followed almost immediately by a call to `exec()` in the child process, which replaces the child's memory with a new program (typically what a shell does), making the copy operation pretty much wasted.

To address this, later versions of Unix took the Copy-on-write approach: allow the parent and child to *share* the memory mapped into their respective address spaces until one of the processes actually modifies it.

In this part, a "proper" Unix-like `fork()` with copy-on-write will be implemented as a *user space library routine*.

#### User-level page fault handling

A user-level copy-on-write `fork()` needs to know about page faults on write-protected pages. Now implement such handler.

#### Setting the Page Fault Handler

In order to handle its own page faults, a user environment will need to register a page fault handler entrypoint with the JOS kernel. The user environment registers its page fault entrypoint via the new `sys_env_set_pgfault_upcall` system call.

> **Exercise 8.** Implement the `sys_env_set_pgfault_upcall` system call. Be sure to enable permission checking when looking up the environment ID of the target environment, since this is a "dangerous" system call.

Simply set `env_pgfault_upcall` of the environments:

```c
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *env;
	if (envid2env(envid, &env, 1) == -E_BAD_ENV)
		return -E_BAD_ENV;

	env->env_pgfault_upcall = func;
	return 0;
}
```
--

#### Normal and Exception Stacks in User Environments
When a page fault occurs in user mode, the kernel will restart the user environment running a designated user-level page fault handler on a different stack from the normal stack, namely the *user exception stack* (from `UXSTACKTOP-PGSIZE` through `UXSTACKTOP-1` inclusive).

Each user environment that wants to support user-level page fault handling will need to allocate memory for its own exception stack, using the `sys_page_alloc()` system call introduced in part A.

#### Invoking the User Page Fault Handler
To handle page faults: If there is no page fault handler registered, the JOS kernel destroys the user environment with a message as before. Otherwise, the kernel sets up a trap frame on the exception stack that looks like a struct `UTrapframe` from `inc/trap.h`.

The kernel then arranges for the user environment to resume execution with the page fault handler running on the exception stack with this stack frame.

If the user environment is already running on the user exception stack (`tf->tf_esp` already in the range between `UXSTACKTOP-PGSIZE` and `UXSTACKTOP-1`, inclusive) when an exception occurs, then the page fault handler itself has faulted. In this case, the new stack frame should start just under the current `tf->tf_esp` rather than at `UXSTACKTOP`. First push an empty 32-bit word, then a `struct UTrapframe`.

> **Exercise 9.** Implement the code in `page_fault_handler` in `kern/trap.c` required to dispatch page faults to the user-mode handler. Be sure to take appropriate precautions when writing into the exception stack. (What happens if the user environment runs out of space on the exception stack?)

See `page_fault_handler` in `kern/trap.c`. As hinted, could use `user_mem_assert` to check permissions before writing into the exception stack. 

> What happens if the user environment runs out of space on the exception stack?

It will be destroyed when it tries to write on the guarded page below the exception stack.

--

#### User-mode Page Fault Entrypoint
> **Exercise 10.** Implement the `_pgfault_upcall routine` in `lib/pfentry.S`. The interesting part is returning to the original point in the user code that caused the page fault. You'll return directly there, without going back through the kernel. The hard part is simultaneously switching stacks and re-loading the `EIP`.

This excercise is quite tricky since we cannot directly use registers and instructions as usual. The following is a workable solution where all the magic numbers are relevant to the structure of `struct UTrapframe`.

```asm
// LAB 4: Your code here.
movl 48(%esp), %eax		// utf_esp
subl $4, %eax
movl 40(%esp), %ebx
movl %ebx, (%eax)
movl %eax, 48(%esp)
addl $8, %esp

// Restore the trap-time registers.  After you do this, you
// can no longer modify any general-purpose registers.
// LAB 4: Your code here.
popal

// Restore eflags from the stack.  After you do this, you can
// no longer use arithmetic operations or anything else that
// modifies eflags.
// LAB 4: Your code here.
addl $4, %esp
popfl

// Switch back to the adjusted trap-time stack.
// LAB 4: Your code here.
popl %esp

// Return to re-execute the instruction that faulted.
// LAB 4: Your code here.
ret
```
--

Now the C user library side of the user-level page fault handling mechanism:

> **Exercise 11.** Finish `set_pgfault_handler()` in `lib/pgfault.c`.

```c
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		if (sys_page_alloc(0, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_W) < 0)
            panic("set_pgfault_handler: could not alloc exception stack!");
        if (sys_env_set_pgfault_upcall(0, _pgfault_upcall) < 0)
            panic("set_pgfault_handler: could not set pgfault upcall!");
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
```
--

#### Implementing Copy-on-Write Fork

> **Exercise 12.** Implement `fork`, `duppage` and `pgfault` in `lib/fork.c`.

Mostly making calls to existing system calls. Also mind the permission bits of the page mappings. See `lib/fork.c` for implementation detail.

Test the code with the `forktree` program, the result meets expectation.

-- 
This ends part B. Run `make grade`, all tests of part A and B are passed.

## Part C: Preemptive Multitasking and Inter-Process communication (IPC)

In this final part, the kernel will be able to preempt uncooperative environments and to allow environments to pass messages to each other explicitly.

### Clock Interrupts and Preemption

#### Interrupt discipline
External interrupts (i.e., device interrupts) are referred to as IRQs. There are 16 possible IRQs, numbered 0 through 15. The mapping from IRQ number to IDT entry is not fixed. In `inc/trap.h`, `IRQ_OFFSET` is defined to be decimal `32`. Thus the IDT entries `32-47` correspond to the IRQs `0-15`.

In JOS, external device interrupts are always disabled when in the kernel, enabled when in user mode. External interrupts are controlled by the `FL_IF` flag bit of the `%eflags` register (see `inc/mmu.h`).

The `FL_IF` flag is set in user environments when they run so that when an interrupt arrives, passed through to the processor and handled by the interrupt code. The very first instruction of the bootloader masked the interrupt, which have never be re-enabled until now.

> **Exercise 13.** Modify `kern/trapentry.S` and `kern/trap.c` to initialize the appropriate entries in the IDT and provide handlers for IRQs 0 through 15. Then modify the code in `env_alloc()` in `kern/env.c` to ensure that user environments are always run with interrupts enabled.
> 
> Also uncomment the `sti` instruction in `sched_halt()` so that idle CPUs unmask interrupts.
> 
> The processor never pushes an error code when invoking a hardware interrupt handler. You might want to re-read section 9.2 of the [80386 Reference Manual](https://pdos.csail.mit.edu/6.828/2018/readings/i386/toc.htm), or section 5.8 of the [IA-32 Intel Architecture Software Developer's Manual, Volume 3](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3A.pdf), at this time.

The appropriate entries in the IDT and handlers for IRQs are set in a way similar to what had been done in the previous lab. Need to set `istrap = 0` when `SETGATE`, so that interrupts are disabled in the kernel. For `env_alloc()`, simply enable interrupts by:

```c
	// Enable interrupts while in user mode.
	// LAB 4: Your code here.
	e->env_tf.tf_eflags |= FL_IF;
```

--
#### Handling Clock Interrupts

The calls to `lapic_init` and `pic_init` (from `i386_init` in `init.c`), set up the clock and the interrupt controller to generate interrupts.

> **Exercise 14.** Modify the kernel's `trap_dispatch()` function so that it calls `sched_yield()` to find and run a different environment whenever a clock interrupt takes place.
> 
> You should now be able to get the `user/spin` test to work: the parent environment should fork off the child, `sys_yield()` to it a couple times but in each case regain control of the CPU after one time slice, and finally kill the child environment and terminate gracefully.

Just dispatch as instructed. Run the `user/spin` test and all works fine.

Regression test: Previous passed test still works fine, running with multiple CPUs OK. Run `make grade` and get a total score of 65/80 at this point.

--

### Inter-Process communication (IPC)
(Technically, it's "inter-environment communication" or "IEC" in JOS.)

#### IPC in JOS
Two system calls, `sys_ipc_recv` and `sys_ipc_try_send`, along with. two library wrappers `ipc_recv` and `ipc_send` will be implemented to provide a simple interprocess communication mechanism.

The "messages" that user environments can send to each other consist of two components: a single 32-bit value, and optionally a single page mapping. Allowing environments to pass page mappings in messages provides an efficient way to transfer more data than will fit into a single 32-bit integer, and also allows environments to set up shared memory arrangements easily.

#### Sending and Receiving Messages
To receive a message, an environment calls `sys_ipc_recv`. This system call de-schedules the current environment and does not run it again until a message has been received (from any possible environment).

To try to send a value, an environment calls `sys_ipc_try_send` with both the receiver's `env_id` and the value to be sent. If the named environment is actually receiving (it has called `sys_ipc_recv` and not gotten a value yet), then the send delivers the message and returns 0. Otherwise the send returns `-E_IPC_NOT_RECV` to indicate that the target environment is not currently expecting to receive a value. 

The library function `ipc_recv` in user space will take care of calling `sys_ipc_recv` and then looking up the information about the received values in the current environment's `struct Env`. `ipc_send` will take care of repeatedly calling `sys_ipc_try_send` until the send succeeds.

#### Transferring Pages
When an environment calls `sys_ipc_recv` with a valid `dstva` parameter (below `UTOP`), the environment is stating that it is willing to receive a page mapping. If the sender sends a page, then that page should be mapped at `dstva` in the receiver's address space (also below `UTOP`). If the receiver already had a page mapped at dstva, then that previous page is unmapped.

After any IPC the kernel sets the new field `env_ipc_perm` in the receiver's `Env` structure to the permissions of the page received, or zero if no page was received.

#### Implementing IPC
> **Exercise 15.** Implement `sys_ipc_recv` and `sys_ipc_try_send` in `kern/syscall.c`. Read the comments on both before implementing them, since they have to work together. When you call `envid2env` in these routines, you should set the `checkperm` flag to `0`, meaning that any environment is allowed to send IPC messages to any other environment, and the kernel does no special permission checking other than verifying that the target `envid` is valid.
> 
> Then implement the `ipc_recv` and `ipc_send` functions in `lib/ipc.c`.

Most code is used to check the permission and parameter's validity. Note that zero is a valid address of the mapping in the message, so an address above `UTOP` should be used to indicate that no page mapping is meant to be sent/received. In my implementation, `UTOP` is used as such an address.

The code passes all the tests.

--

Now Part C is finished. Run `make grade` and the score is 80/80.

---
### Challenge 1
> ***Challenge!*** Add a less trivial scheduling policy to the kernel, such as a fixed-priority scheduler that allows each environment to be assigned a priority and ensures that higher-priority environments are always chosen in preference to lower-priority environments. If you're feeling really adventurous, try implementing a Unix-style adjustable-priority scheduler or even a lottery or stride scheduler. (Look up "lottery scheduling" and "stride scheduling" in Google.)
> 
> Write a test program or two that verifies that your scheduling algorithm is working correctly (i.e., the right environments get run in the right order). It may be easier to write these test programs once you have implemented `fork()` and IPC in parts B and C of this lab.

I implemented a lottery scheduler by:

1. Add a `env_tickets` field to `struct Env` in `inc/env.h` to store the lottery tickets set for an environment (maximum 255).
2. Add a `sys_env_set_tickets` system call in `kern/syscall.c`, also its user library wrapper in `inc/syscall.c` and `inc/lib.h`, the corresponding syscall number `SYS_env_set_tickets` in `inc/syscall.h`. This syscall takes a `envid` and ticket number, set the environment's ticket to the given number of tickets (modulo 256) (or `ENV_DEFAULT_TICKETS` in `inc/env.h` if `0` is passed).
3. A pseudo-random number generator `rand()`, and its seed setter `srand()` in `kern/sched.c`, the implementation is copied from **The C Programming Language (Second Edition)** *(Brian W. Kernighan, Dennis M. Ritchie)*:
	
	```c
	static unsigned next = 1;
	static unsigned rand()
	{
		next = next * 1103515245 + 12345;
		return next;
	}
	
	void srand(unsigned seed)
	{
		next = seed;
	}
	```
	
	And the corresponding flag `use_lottery` indicating if lottery scheduling is enabled, initialized by `lottery_sched_init()` which is called in `i386_init()` (if to enable lottery scheduling).
4. Modify `sched_yield` to choose an environment based on the random lottery number:

	```c
	// ...
	if (use_lottery) {
		int total_tickets = 0;
		for (int i = 0; i < NENV; i++)
			if (envs[i].env_status == ENV_RUNNABLE ||
				(i == cur_env_id && envs[i].env_status == ENV_RUNNING))
				total_tickets += envs[i].env_tickets;

		if (total_tickets == 0) // Nothing runnable
			sched_halt();

		int draw = rand() % total_tickets;
		total_tickets = 0;
		for (int i = 0; i < NENV; i++)
			if (envs[i].env_status == ENV_RUNNABLE ||
				(i == cur_env_id && envs[i].env_status == ENV_RUNNING)) {
				total_tickets += envs[i].env_tickets;
				if (total_tickets >= draw)
					env_krun(&envs[i]);
			}

		// sched_halt never returns
		sched_halt();
	}
	// ... Normal round-robin scheduler
	```
5. A test user program `user/lottery.c` which forks a child environment, and the parent's lottery tickets set to 100, while the child's set to 10. Each environment will finish after 100 runs. 

Run `make run-lottery-nox` to test the lottery scheduling mechanism: When the parent runs 100 times and finish, the child has only run about 9 or 10 times. 

---

### Challenge 2
> ***Challenge!*** The JOS kernel currently does not allow applications to use the x86 processor's x87 floating-point unit (FPU), MMX instructions, or Streaming SIMD Extensions (SSE). Extend the `Env` structure to provide a save area for the processor's floating point state, and extend the context switching code to save and restore this state properly when switching from one environment to another. The `FXSAVE` and `FXRSTOR` instructions may be useful, but note that these are not in the old i386 user's manual because they were introduced in more recent processors. Write a user-level test program that does something cool with floating-point.

To support FPU, MMX and SSE, first enable `OSFXSR` to support `FXSAVE` and `FXRSTOR` instuctions. In `trap_init_percpu()`:

```c
	cr4 = rcr4();
	cr4 |= 1 << 9;  // OSFXSR is 9th bit of CR4
	lcr4(cr4);
```

Also, extend a field to the `struct Env`:

```c
	char fxsave[512] __attribute__((aligned(16)));
```

When trapping from user mode, save FPU, MMX and SSE registers (in `trap()` in `kern/trap.c`:

```c
	asm volatile("fxsave %0" : "=m" (*curenv->fxsave));
```

and restore them in `env_run()`:

```c
	asm volatile("fxrstor %0" :: "m" (*curenv->fxsave));
```

I wrote a simple test program `user/fputest.c` to test if these registers are correctly saved and restored. Run `make run-fputest-nox`,  if the child environment does not panic, these registers are correctly saved and restored and FPU, MMX and SSE instructions could work properly.

---
### Challenge 3
> ***Challenge!*** Implement a shared-memory `fork()` called `sfork()`. This version should have the parent and child share all their memory pages (so writes in one environment appear in the other) except for pages in the stack area, which should be treated in the usual copy-on-write manner. Modify `user/forktree.c` to use `sfork()` instead of regular `fork()`. Also, once you have finished implementing IPC in part C, use your `sfork()` to run `user/pingpongs`. You will have to find a new way to provide the functionality of the global `thisenv` pointer.

`sfork` itself is not hard to implement, however, since all mappings except the stack is shared when `sfork`, the change of `thisenv` in child environment will affect the parent as well (since it's a user space variable that stores in user memory space). So I changed `thisenv` from a pointer to a macro that always dereferences `USTACKTOP-4` as a pointer to a `struct Env`:

```c
#define thisenv     (*((const volatile struct Env **) (USTACKTOP - 4)))
```

The `lib/entry.S` has to be modified a little, since now the stack actually starts grow from `STACKTOP-4`:

```asm
	// See if we were started with arguments on the stack
	cmpl $USTACKTOP-4, %esp
	jne args_exist
```

Now, `libmain.c` sets `thisenv`, and `sfork` modified the child's `thisenv` works as expected.

Now `sfork()`: actually simpler than the regular `fork()`, just copy the mapping of all pages except the stack if the mapping is writable or read-only. For mappings marked as COW, `sfork()` should first copy the page in the parent's memory space and mark it writable, then copy the new mapping to the child. See `lib/fork.c` for detail.

Test: change `user/forktree.c` to use `sfork()`, run `make run-forktree-nox` and it works fine. `make run-pingpongs-nox` also OK.

---

### Challenge 4
> **Challenge!** Why does `ipc_send` have to loop? Change the system call interface so it doesn't have to. Make sure you can handle multiple environments trying to send to one environment at the same time.

To make `ipc_send` non-loop, the `struct Env` has to be redesigned to store necessary states:

```c
struct Env {
	// ...
	struct Env *env_link;		// Next free Env or next in sender_list
	// ...
	// Lab 4 IPC
	bool env_ipc_recving;		// Env is blocked receiving
	bool env_ipc_sending;		// Env is blocked sending, for non-loop ipc_send challenge
	void *env_ipc_va;			// VA of sended page or at which to map received page
	uint32_t env_ipc_value;		// Data value to send or received
	envid_t env_ipc_from;		// envid of the sender
	int env_ipc_perm;		// Perm of page mapping received
	struct Env *sender_list_head, *sender_list_tail;	// for non-loop ipc_send challenge
	// ...
}
```

Now, the IPC parameters of both sending or receiving can be stored, a FIFO queue of every `Env` sending to this `env` is also maintained to ensure fairness.

If an environment tries to send a message, the kernel will (see `sys_ipc_try_send()` in `kern/syscall.c`):

1. check the validity of parameters
2. mark this environment as `ENV_NOT_RUNNABLE` and set `env_ipc_sending` to be `true`
3. save IPC parameters
4. append this environment's struct `Env` to the `sender_list` of the target environment
5. check if the target is already receiving. If so, a sending action can be immediately performed (using `ipc_try_recv()` in `kern/syscall.c`)

If an environment is receiving a message, the kernel will (see `sys_ipc_recv()` in `kern/syscall.c`):

1. check the validity of parameters
2. mark this environment as `ENV_NOT_RUNNABLE` and set `env_ipc_recving` to be `true` (as before)
3. save IPC parameters
4. check if there already is sending `Env` in `sender_list`. If so, receive operation can be finished using `ipc_try_recv()`.

---

### Lab 4 IS COMPLETED.
