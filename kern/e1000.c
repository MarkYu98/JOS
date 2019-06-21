#include <inc/assert.h>
#include <inc/error.h>
#include <inc/string.h>

#include <kern/e1000.h>
#include <kern/pmap.h>

/* Registers */
#define E1000_IMS      0x000D0 /* Interrupt Mask Set - RW */

#define E1000_TCTL     0x00400 /* TX Control - RW */
#define E1000_TIPG     0x00410 /* TX Inter-packet gap -RW */
#define E1000_TDBAL    0x03800 /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804 /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808 /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810 /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818 /* TX Descripotr Tail - RW */

#define E1000_RCTL     0x00100  /* RX Control - RW */
#define E1000_RDBAL    0x02800 /* RX Descriptor Base Address Low - RW */
#define E1000_RDBAH    0x02804 /* RX Descriptor Base Address High - RW */
#define E1000_RDLEN    0x02808 /* RX Descriptor Length - RW */
#define E1000_RDH      0x02810 /* RX Descriptor Head - RW */
#define E1000_RDT      0x02818 /* RX Descriptor Tail - RW */
#define E1000_MTA      0x05200 /* Multicast Table Array - RW Array */
#define E1000_RA       0x05400 /* Receive Address - RW Array */

/* Receive Control */
#define E1000_RCTL_RST            0x00000001    /* Software reset */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_SBP            0x00000004    /* store bad packet */
#define E1000_RCTL_UPE            0x00000008    /* unicast promiscuous enable */
#define E1000_RCTL_MPE            0x00000010    /* multicast promiscuous enab */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM_NO         0x00000000    /* no loopback mode */
#define E1000_RCTL_LBM_MAC        0x00000040    /* MAC loopback mode */
#define E1000_RCTL_LBM_SLP        0x00000080    /* serial link loopback mode */
#define E1000_RCTL_LBM_TCVR       0x000000C0    /* tcvr loopback mode */
#define E1000_RCTL_DTYP_MASK      0x00000C00    /* Descriptor type mask */
#define E1000_RCTL_DTYP_PS        0x00000400    /* Packet Split descriptor */
#define E1000_RCTL_RDMTS_HALF     0x00000000    /* rx desc min threshold size */
#define E1000_RCTL_RDMTS_QUAT     0x00000100    /* rx desc min threshold size */
#define E1000_RCTL_RDMTS_EIGTH    0x00000200    /* rx desc min threshold size */
#define E1000_RCTL_MO_SHIFT       12            /* multicast offset shift */
#define E1000_RCTL_MO_0           0x00000000    /* multicast offset 11:0 */
#define E1000_RCTL_MO_1           0x00001000    /* multicast offset 12:1 */
#define E1000_RCTL_MO_2           0x00002000    /* multicast offset 13:2 */
#define E1000_RCTL_MO_3           0x00003000    /* multicast offset 15:4 */
#define E1000_RCTL_MDR            0x00004000    /* multicast desc ring 0 */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 0 */
#define E1000_RCTL_SZ_2048        0x00000000    /* rx buffer size 2048 */
#define E1000_RCTL_SZ_1024        0x00010000    /* rx buffer size 1024 */
#define E1000_RCTL_SZ_512         0x00020000    /* rx buffer size 512 */
#define E1000_RCTL_SZ_256         0x00030000    /* rx buffer size 256 */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 1 */
#define E1000_RCTL_SZ_16384       0x00010000    /* rx buffer size 16384 */
#define E1000_RCTL_SZ_8192        0x00020000    /* rx buffer size 8192 */
#define E1000_RCTL_SZ_4096        0x00030000    /* rx buffer size 4096 */
#define E1000_RCTL_VFE            0x00040000    /* vlan filter enable */
#define E1000_RCTL_CFIEN          0x00080000    /* canonical form enable */
#define E1000_RCTL_CFI            0x00100000    /* canonical form indicator */
#define E1000_RCTL_DPF            0x00400000    /* discard pause frames */
#define E1000_RCTL_PMCF           0x00800000    /* pass MAC control frames */
#define E1000_RCTL_BSEX           0x02000000    /* Buffer size extension */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */
#define E1000_RCTL_FLXBUF_MASK    0x78000000    /* Flexible buffer size */
#define E1000_RCTL_FLXBUF_SHIFT   27            /* Flexible buffer shift */

/* Transmit Descriptor bit definitions */
#define E1000_TXD_CMD_RS     0x00000008 /* Report Status */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_TXD_CMD_EOP    0x00000001 /* End of Packet */

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */

#define REG(name) reg[E1000_##name / 4]
#define REGARRAY(name, index) reg[E1000_##name / 4 + (index)]

#define N_TX_DESC      64
#define N_RX_DESC      128

volatile uint32_t *reg;

/* Transmit Descriptor */
struct tx_desc
{
    uint64_t buffer_addr;   /* Address of the descriptor's data buffer */
    uint16_t length;        /* Data buffer length */
    uint8_t cso;            /* Checksum offset */
    uint8_t cmd;            /* Descriptor control */
    uint8_t status;         /* Descriptor status */
    uint8_t css;            /* Checksum start */
    uint16_t special;
};

/* Receive Descriptor */
struct rx_desc {
    uint64_t buffer_addr;   /* Address of the descriptor's data buffer */
    uint16_t length;        /* Length of data DMAed into data buffer */
    uint16_t csum;          /* Packet checksum */
    uint8_t status;         /* Descriptor status */
    uint8_t errors;         /* Descriptor Errors */
    uint16_t special;
};

__attribute__((__aligned__(16)))
struct tx_desc tx_desc_array[N_TX_DESC];

char tx_packet_buffer[N_TX_DESC][1520];  // packet max length 1518

__attribute__((__aligned__(16)))
struct rx_desc rx_desc_array[N_RX_DESC];

char rx_packet_buffer[N_RX_DESC][2048];

int
e1000_attach(struct pci_func *pcif)
{
    int i;

    pci_func_enable(pcif);
    reg = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

    //
    // transmit initialization
    //
    REG(TDBAL) = PADDR(tx_desc_array);
    REG(TDLEN) = sizeof(tx_desc_array);
    REG(TDH) = 0;
    REG(TDT) = 0;

    // TCTL Reg: 32| Reserved 26| CNTL Bits 22| COLD 12| CT 4| CNTL Bits 0|
    REG(TCTL) = (0x40 << 12) | (0x10 << 4) | 0x8 | 0x2;

    // TIPG Reg: 32| Reserved 30| IPGR2 20| IPGR1 10| IPGT 0|
    REG(TIPG) = (6 << 20) | (8 << 10) | 10;

    //
    // receive initialization
    //
    // MAC: 52:54:00:12:34:56
    REGARRAY(RA, 0) = 0x12005452;
    REGARRAY(RA, 1) = 0x80005634;
    REG(MTA) = 0;
    REG(IMS) = 0;
    REG(RDBAL) = PADDR(rx_desc_array);
    REG(RDLEN) = sizeof(rx_desc_array);
    REG(RDH) = 0;
    REG(RDT) = N_RX_DESC - 1;
    for (i = 0; i < N_RX_DESC; i++)
        rx_desc_array[i].buffer_addr = PADDR(rx_packet_buffer[i]);

    REG(RCTL) = E1000_RCTL_EN | E1000_RCTL_RDMTS_HALF | E1000_RCTL_BAM |
                E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC;

    return 1;
}

// Returns 0 on success, < 0 on error.
// Errors are:
//  -E_QUEUE_FULL if transmit queue is full
int
e1000_transmit(void *buf, size_t length)
{
    uint32_t tdt = REG(TDT);

    if ((tx_desc_array[tdt].cmd & E1000_TXD_CMD_RS) &&
        !(tx_desc_array[tdt].status & E1000_TXD_STAT_DD)) {
        // transmit queue is full
        return -E_QUEUE_FULL;
    }

    memmove(tx_packet_buffer[tdt], buf, length);
    tx_desc_array[tdt].buffer_addr = PADDR(tx_packet_buffer[tdt]);
    tx_desc_array[tdt].length = length;
    tx_desc_array[tdt].cmd |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
    tx_desc_array[tdt].status &= ~E1000_TXD_STAT_DD;

    if (++tdt == N_TX_DESC)
        tdt = 0;
    REG(TDT) = tdt;

    return 0;
}

// Returns packet size on success, < 0 on error.
// Errors are:
//  -E_QUEUE_EMPTY if receive queue is empty
ssize_t
e1000_receive(void *buf)
{
    uint32_t rdt = (REG(RDT) + 1) % N_RX_DESC;
    size_t length = rx_desc_array[rdt].length;

    if (!(rx_desc_array[rdt].status & E1000_RXD_STAT_DD))
        return -E_QUEUE_EMPTY;

    memmove(buf, KADDR(rx_desc_array[rdt].buffer_addr), length);
    rx_desc_array[rdt].status = 0;

    REG(RDT) = rdt;

    return length;
}
