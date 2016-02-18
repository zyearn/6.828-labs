#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

struct tx_desc tx_desc_array[E1000_TXDESC] __attribute__((aligned(16)));
struct tx_pkg tx_pkg_array[E1000_TXDESC];

int attach_e1000(struct pci_func *pcif) {
    pci_func_enable(pcif);

    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(e1000[E1000_STATUS] == 0x80080783);

    // initialize tx desc arry
    memset(tx_desc_array, 0, sizeof(struct tx_desc) * E1000_TXDESC);
    uint32_t i;
    for (i=0; i<E1000_TXDESC; i++) {
        tx_desc_array[i].addr = PADDR(tx_pkg_array[i].buf);
        tx_desc_array[i].status |= E1000_TXD_STAT_DD;
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

    return 0;
}

int e1000_transmit(char *data, int len) {
	if (len > TX_PKG_SIZE) {
		return -E_PKG_TOO_LONG;
	}

	uint32_t tdt = e1000[E1000_TDT];

	// Check if next tx desc is free
	if (tx_desc_array[tdt].status & E1000_TXD_STAT_DD) {
		memmove(tx_pkg_array[tdt].buf, data, len);
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
