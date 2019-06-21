#include <inc/lib.h>
#include <inc/error.h>

#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    struct jif_pkt *pkt;
    envid_t from_envid;
    int r;

    r = sys_page_alloc(0, (void *)REQVA, PTE_U | PTE_W | PTE_P);
    if (r < 0)
        panic("output env: sys_page_alloc: %e", r);

    while (true) {
        r = ipc_recv(&from_envid, (void *)REQVA, NULL);
        if (from_envid != ns_envid || r != NSREQ_OUTPUT)
            continue;
        while (true) {
            pkt = (struct jif_pkt*) REQVA;
            r = sys_transmit((void *)pkt->jp_data, pkt->jp_len);
            if (!r)
                break;
            if (r != E_QUEUE_FULL)
                panic("output env: sys_transmit: %e", r);
            sys_yield();
        }
    }
}
