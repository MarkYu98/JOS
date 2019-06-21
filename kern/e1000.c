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

#define N_TX_DESC   64

volatile uint32_t *e1000;

struct tx_desc
{
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
};

__attribute__((__aligned__(PGSIZE)))
struct tx_desc tx_desc_array[N_TX_DESC];

// packet max length 1518 is smaller than PGSIZE/2 (=2048)
__attribute__((__aligned__(PGSIZE)))
char tx_packet_buffer[N_TX_DESC][PGSIZE / 2];

int
e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);
    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    // cprintf("e1000_attach: device status register is 0x%x\n", e1000[2]);

    static_assert(sizeof(tx_desc_array) <= PGSIZE);
    e1000[E1000_TDH] = 0;
    e1000[E1000_TDT] = 0;
    e1000[E1000_TDBAL] = PADDR(tx_desc_array);
    e1000[E1000_TDLEN] = sizeof(tx_desc_array);

    // TCTL Reg: 32| Reserved 26| CNTL Bits 22| COLD 12| CT 4| CNTL Bits 0|
    e1000[E1000_TCTL] = (0x40 << 12) | (0x10 << 4) | 0x8 | 0x2;

    // TIPG Reg: 32| Reserved 30| IPGR2 20| IPGR1 10| IPGT 0|
    e1000[E1000_TIPG] = (6 << 20) | (8 << 10) | 10;

    return 1;
}
