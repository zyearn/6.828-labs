#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

struct tx_desc tx_desc_array[E1000_TXDESC] __attribute__((aligned(16)));
struct tx_pkt tx_pkt_array[E1000_TXDESC];

struct rcv_desc rcv_desc_array[E1000_RCVDESC] __attribute__((aligned(16)));
struct rcv_pkt rcv_pkt_array[E1000_RCVDESC];

int attach_e1000(struct pci_func *pcif) {
    pci_func_enable(pcif);

    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(e1000[E1000_STATUS] == 0x80080783);

    // initialize tx desc arry
    memset(tx_desc_array, 0, sizeof(struct tx_desc) * E1000_TXDESC);
    uint32_t i;
    for (i=0; i<E1000_TXDESC; i++) {
        tx_desc_array[i].addr = PADDR(tx_pkt_array[i].buf);
        tx_desc_array[i].status |= E1000_TXD_STAT_DD;
    }

    // initialize rev desc arry
    memset(rcv_desc_array, 0, sizeof(struct rcv_desc) * E1000_RCVDESC);
    for (i=0; i<E1000_RCVDESC; i++) {
        rcv_desc_array[i].addr = PADDR(rcv_pkt_array[i].buf);
    }

    // tx initilization
    e1000[E1000_TDBAL] = PADDR(tx_desc_array);
    e1000[E1000_TDBAH] = 0;
    e1000[E1000_TDLEN] = sizeof(struct tx_desc) * E1000_TXDESC;
    e1000[E1000_TDH] = 0;
    e1000[E1000_TDT] = 0;

    e1000[E1000_TCTL] |= E1000_TCTL_EN;
    e1000[E1000_TCTL] |= E1000_TCTL_PSP;
    e1000[E1000_TCTL] &= ~E1000_TCTL_CT;
    e1000[E1000_TCTL] |= (0x10) << 4;
    e1000[E1000_TCTL] &= ~E1000_TCTL_COLD;
    e1000[E1000_TCTL] |= (0x40) << 12;

    e1000[E1000_TIPG] = 0x0;
    e1000[E1000_TIPG] |= (0x6) << 20; // IPGR2 
    e1000[E1000_TIPG] |= (0x4) << 10; // IPGR1
    e1000[E1000_TIPG] |= 0xA; // IPGR

    // rcv initilization
	e1000[E1000_EERD] = 0x0;
	e1000[E1000_EERD] |= E1000_EERD_START;
	while (!(e1000[E1000_EERD] & E1000_EERD_DONE));
	e1000[E1000_RAL] = e1000[E1000_EERD] >> 16;

	e1000[E1000_EERD] = 0x1 << 8;
	e1000[E1000_EERD] |= E1000_EERD_START;
	while (!(e1000[E1000_EERD] & E1000_EERD_DONE));
	e1000[E1000_RAL] |= e1000[E1000_EERD] & 0xffff0000;

	e1000[E1000_EERD] = 0x2 << 8;
	e1000[E1000_EERD] |= E1000_EERD_START;
	while (!(e1000[E1000_EERD] & E1000_EERD_DONE));
	e1000[E1000_RAH] = e1000[E1000_EERD] >> 16;

    // valid
	e1000[E1000_RAH] |= 0x1 << 31;

    for (i=0; i<127; i++)
        e1000[E1000_MTA+i] = 0;

    e1000[E1000_RDBAL] = PADDR(rcv_desc_array);
    e1000[E1000_RDBAH] = 0x0;

    // Set the Receive Descriptor Length Register
    e1000[E1000_RDLEN] = sizeof(struct rcv_desc) * E1000_RCVDESC;

    // Set the Receive Descriptor Head and Tail Registers
    e1000[E1000_RDH] = 0x0;
    e1000[E1000_RDT] = 0x0;

    // Initialize the Receive Control Register
    e1000[E1000_RCTL] |= E1000_RCTL_EN;
    e1000[E1000_RCTL] &= ~E1000_RCTL_LPE;
    e1000[E1000_RCTL] &= ~E1000_RCTL_LBM;
    e1000[E1000_RCTL] &= ~E1000_RCTL_RDMTS;
    e1000[E1000_RCTL] &= ~E1000_RCTL_MO;
    e1000[E1000_RCTL] |= E1000_RCTL_BAM;
    e1000[E1000_RCTL] &= ~E1000_RCTL_SZ; // 2048 byte size
    e1000[E1000_RCTL] |= E1000_RCTL_SECRC;

    return 0;
}

int e1000_transmit(char *data, size_t len) {
	if (len > TX_PKT_SIZE) {
		return -E_PKT_TOO_LONG;
	}

	uint32_t tdt = e1000[E1000_TDT];

	// Check if next tx desc is free
	if (tx_desc_array[tdt].status & E1000_TXD_STAT_DD) {
		memmove(tx_pkt_array[tdt].buf, data, len);
		tx_desc_array[tdt].length = len;

		tx_desc_array[tdt].status &= ~E1000_TXD_STAT_DD;
		tx_desc_array[tdt].cmd |= E1000_TXD_CMD_RS;
		tx_desc_array[tdt].cmd |= E1000_TXD_CMD_EOP;

		e1000[E1000_TDT] = (tdt + 1) % E1000_TXDESC;
	} else { // tx queue is full!
		return -E_TX_FULL;
	}
	
	return 0;
}

int e1000_receive(char *data, size_t buflen, size_t *plen) {
    uint32_t rdt = e1000[E1000_RDT];
    cprintf("receing...\n");

    if (rcv_desc_array[rdt].status & E1000_RXD_STAT_DD) {
        cprintf("receive succ!!!\n");
        if (!(rcv_desc_array[rdt].status & E1000_RXD_STAT_EOP)) {
            panic("jumbo frames\n");
        }
        uint32_t len = rcv_desc_array[rdt].length;
        if (len < buflen)
            buflen = len;

        memmove(data, rcv_pkt_array[rdt].buf, buflen);
		rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_DD;
		rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_EOP;
		e1000[E1000_RDT] = (rdt + 1) % E1000_RCVDESC;

        *plen = buflen;
		return 0;
    }
    
    return -E_RCV_EMPTY;
}
