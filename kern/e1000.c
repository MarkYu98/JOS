#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/pci.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here
volatile uint32_t *e1000;

__attribute__((__aligned__(16)))
struct tx_desc tx_desc_array[N_TX_DESC];
char tx_packet_buffer[N_TX_DESC][MAX_PACKET_SIZE+2];  // packet max length 1518

__attribute__((__aligned__(16)))
struct rx_desc rx_desc_array[N_RX_DESC];
char rx_packet_buffer[N_RX_DESC][PGSIZE >> 2];

int
e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);
    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    // cprintf("e1000_attach: device status register is 0x%x\n", e1000[2]);

    // Transmit Init
    e1000[E1000_TDH >> 2] = 0;
    e1000[E1000_TDT >> 2] = 0;
    e1000[E1000_TDBAL >> 2] = PADDR(tx_desc_array);
    e1000[E1000_TDLEN >> 2] = sizeof(tx_desc_array);

    // TCTL Reg: 32| Reserved 26| CNTL Bits 22| COLD 12| CT 4| CNTL Bits 0|
    e1000[E1000_TCTL] = (0x40 << 12) | (0x10 << 4) | 0x8 | 0x2;

    // TIPG Reg: 32| Reserved 30| IPGR2 20| IPGR1 10| IPGT 0|
    e1000[E1000_TIPG] = (6 << 20) | (8 << 10) | 10;

    // Receive Init
    // MAC: 52:54:00:12:34:56
    e1000[E1000_RA >> 2] = 0x12005452;
    e1000[(E1000_RA >> 2) + 1] = 0x80005634;
    e1000[E1000_MTA >> 2] = 0;
    e1000[E1000_IMS] = 0;
    e1000[E1000_RDBAL] = PADDR(rx_desc_array);
    e1000[E1000_RDLEN] = sizeof(rx_desc_array);
    e1000[E1000_RDH] = 0;
    e1000[E1000_RDT] = N_RX_DESC - 1;
    for (int i = 0; i < N_RX_DESC; i++)
        rx_desc_array[i].addr = PADDR(rx_packet_buffer[i]);

    e1000[E1000_RCTL] = E1000_RCTL_EN | E1000_RCTL_RDMTS_HALF | E1000_RCTL_BAM |
                        E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC;

    return 1;
}

// Return 0 on success, -E_QUEUE_FULL when transmit queue is full
int
e1000_transmit(void *buffer, uint32_t length)
{
    uint32_t tdt = e1000[E1000_TDT];
    struct tx_desc *nxt_desc = &tx_desc_array[tdt];

    if ((nxt_desc->cmd & E1000_TXD_CMD_RS) &&
        !(nxt_desc->status & E1000_TXD_STAT_DD)) {
        // Queue Full!
        return -E_QUEUE_FULL;
    }

    memcpy(tx_packet_buffer[tdt], buffer, length);
    nxt_desc->addr = PADDR(tx_packet_buffer[tdt]);
    nxt_desc->length = (uint16_t) length;
    nxt_desc->cmd |= E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    nxt_desc->status &= ~E1000_TXD_STAT_DD;

    if (++tdt >= N_TX_DESC)
        tdt = 0;
    e1000[E1000_TDT] = tdt;

    return 0;
}

// Return 0 on success, -E_QUEUE_EMPTY when receive queue is empty
ssize_t
e1000_receive(void *buffer)
{
    uint32_t rdt = e1000[E1000_RDT];
    size_t length;

    if (++rdt >= N_RX_DESC)
        rdt = 0;
    length = rx_desc_array[rdt].length;

    if (!(rx_desc_array[rdt].status & E1000_RXD_STAT_DD))
        return -E_QUEUE_EMPTY;
    memcpy(buffer, KADDR(rx_desc_array[rdt].addr), length);
    rx_desc_array[rdt].status = 0;

    e1000[E1000_RDT] = rdt;
    return length;
}
