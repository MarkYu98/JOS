#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

int e1000_attach(struct pci_func *pcif);

#endif  // SOL >= 6