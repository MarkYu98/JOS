#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/pci.h>

// LAB 6: Your driver code here

/* Registers */
#define E1000_TCTL     0x00400 / 4 /* TX Control - RW */
#define E1000_TIPG     0x00410 / 4 /* TX Inter-packet gap -RW */
#define E1000_TDBAL    0x03800 / 4 /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804 / 4 /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808 / 4 /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810 / 4 /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818 / 4 /* TX Descripotr Tail - RW */

/* Transmit Descriptor bit definitions */
#define E1000_TXD_CMD_RS     0x00000008 /* Report Status */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_TXD_CMD_EOP    0x00000001 /* End of Packet */

volatile uint32_t *e1000;

int
e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);
    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    cprintf("e1000_attach: device status register is 0x%x\n", e1000[2]);

    return 0;
}