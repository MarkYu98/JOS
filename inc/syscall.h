#ifndef JOS_INC_SYSCALL_H
#define JOS_INC_SYSCALL_H

/* system call numbers */
enum {
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
	SYS_page_alloc,
	SYS_page_map,
	SYS_page_unmap,
	SYS_exofork,
	SYS_env_set_status,
	SYS_env_set_trapframe,
	SYS_env_set_pgfault_upcall,
	SYS_env_set_tickets,
	SYS_yield,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	SYS_env_load_elf,
	NSYSCALLS
};

// For lab5 exec challenge
struct SegmentInfo {
    void *srcva, *dstva;
	unsigned int size;
    int perm;
};

#endif /* !JOS_INC_SYSCALL_H */
