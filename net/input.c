#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
    struct jif_pkt *pkt[QUEUE_SIZE];
    int r;

    for (int i = 0; i < QUEUE_SIZE; i++) {
        pkt[i] = (struct jif_pkt *) (REQVA + i * PGSIZE);
        if ((r = sys_page_alloc(0, pkt[i], PTE_W | PTE_U | PTE_P)) < 0)
            panic("input: sys_page_alloc: %e", r);
    }
    for (int i = 0; ; i++) {
        if (i >= QUEUE_SIZE)
            i = 0;
        while (true) {
            r = sys_pkt_receive(pkt[i]->jp_data);
            if (r != -E_QUEUE_EMPTY)
                break;
            sys_yield();
        }
        if (r < 0) {
            cprintf("input env: sys_packet_receive: %e", r);
            continue;
        }
        pkt[i]->jp_len = r;
        ipc_send(ns_envid, NSREQ_INPUT, pkt[i], PTE_U | PTE_P);
    }
}
