#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>
#include <inc/types.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E
#define MAX_PACKET_SIZE 1518

int e1000_attach(struct pci_func *pcif);
int e1000_transmit(void *buf, uint32_t length);
ssize_t e1000_receive(void *buf);

#endif  // SOL >= 6
