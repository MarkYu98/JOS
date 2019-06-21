/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_SCHED_H
#define JOS_KERN_SCHED_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

// Lab4 lottery scheduling challenge
void srand(unsigned seed);
void lottery_sched_init();

// This function does not return.
void sched_yield(void) __attribute__((noreturn));

#endif	// !JOS_KERN_SCHED_H
