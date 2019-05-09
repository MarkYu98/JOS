#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>
#define SEED 	1857460923

void sched_halt(void);

// Lab4 lottery scheduling challenge
static bool use_lottery = false;
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

void lottery_sched_init()
{
	use_lottery = true;
	srand(SEED);
}

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	// struct Env *idle;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
	int cur_env_id = -1;
	if (curenv)
		cur_env_id = ENVX(curenv->env_id);

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
					env_run(&envs[i]);
			}

		// sched_halt never returns
		sched_halt();
	}

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

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

