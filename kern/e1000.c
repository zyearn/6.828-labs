#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <inc/assert.h>
#include <inc/string.h>

// LAB 6: Your driver code here
volatile uint32_t *eth_loc;

struct tx_desc tx_queue[E1000_TDLEN_MAX];
struct rx_desc rx_queue[E1000_RCV_MAX];

char tx_buffer[E1000_TDLEN_MAX][2048];
char rx_buffer[E1000_RCV_MAX][2048];

char *packet_test = "hello packet.hello packet.hello packet.hello packet.";

static void
ethw(int index, int value) {
    eth_loc[index >> INTSHIFT] = value;
}

static uint32_t
ethr(int index) {
    return eth_loc[index >> INTSHIFT];
}

static uint16_t
eerd(uint8_t addr) {
    uint32_t word = addr;
    word = (word << E1000_EEPROM_RW_ADDR_SHIFT) | E1000_EEPROM_RW_REG_START;
    ethw(E1000_EERD, word);
    while ((ethr(E1000_EERD) & E1000_EEPROM_RW_REG_DONE) == 0);
    return ethr(E1000_EERD) >> E1000_EEPROM_RW_REG_DATA_SHIFT;
}

static int
check_transmit_ready(int index) {
    if (tx_queue[index].length == 0)
        return 1;

    return (tx_queue[index].cmd & E1000_TXD_CMD_RS) 
        && (tx_queue[index].status & E1000_TXD_STAT_DD);
}

static int
check_rcv_ready(int index) {
    return (rx_queue[index].status & E1000_RXD_STAT_DD);
}

/**
 * max packet length is 1518 and the addr is page aligned
 * so only need one page
 * offset = sizeof(int)
 */
static physaddr_t
convert(void *addr) {
    pte_t *pte = pgdir_walk(curenv->env_pgdir, addr, 0);
    uint32_t offset = sizeof(int);
    return page2pa(&pages[PGNUM(*pte)]) + offset;
}

int mac_addr(int index) {
    if (EEPROM_MAC_ADDR1 <= index && index <= EEPROM_MAC_ADDR3)
        return eerd(index);
    
    return -1;
}

/**
 * ignore if full
 * return 0 if success else -1
 */
int e1000_transmit(void *addr, uint16_t len) {
    uint32_t tail = ethr(E1000_TDT);
    uint32_t next_index = (tail + 1 < E1000_TDLEN_MAX) ? 
        (tail + 1) : 0;

    assert(next_index < E1000_TDLEN_MAX);

    // ignore 
    if (check_transmit_ready(tail) == 0)
        return -1;

    memmove(tx_buffer[tail], addr, len);
    tx_queue[tail].addr = PADDR(tx_buffer[tail]);
    tx_queue[tail].length = len;
    tx_queue[tail].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
    tx_queue[tail].status = 0;

    ethw(E1000_TDT, next_index);
    return 0;
}

int e1000_receive(void *addr, uint32_t *len) {
    int i = ethr(E1000_RDT);
    int last = (i == E1000_RCV_MAX - 1) ? (0) : (i + 1);
    int r = check_rcv_ready(last);
    if (r == 0) {
        //ethw(E1000_RDT, next);
        return -1;
    } 

    memmove(addr, rx_buffer[last], rx_queue[last].length);
    *len = rx_queue[last].length;

    rx_queue[last].status = 0;
    ethw(E1000_RDT, last);

    //cprintf("===== tail = %d\n", last);
    return 0;
}

int attach_e1000(struct pci_func *func) {
    int i;
    pci_func_enable(func);

    // mmio
    eth_loc = mmio_map_region(func->reg_base[0], func->reg_size[0]);

    // print the status register 
    uint32_t reg = ethr(E1000_STATUS);
    cprintf("PCI status register %x\n", reg);

    
    // set TDBAL to the pa of tx queue
    assert((PADDR(tx_queue) & 7) == 0);
    ethw(E1000_TDBAL, PADDR(tx_queue));
    ethw(E1000_TDBAH, 0);

    // set TDLEN to size(byte) of tx_queue
    uint32_t tx_queue_sz = E1000_TDLEN_MAX * sizeof(struct tx_desc);
    assert(tx_queue_sz % 128 == 0);
    ethw(E1000_TDLEN, tx_queue_sz);

    // init head and tail to zero
    ethw(E1000_TDH, 0);
    ethw(E1000_TDT, 0);

    // set TCTL
    ethw(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT 
            | E1000_TCTL_COLD);

    // set TIPG
    // 0 6 4 10
    ethw(E1000_TIPG, 0x60100A);

    // MAC address: configured by command line argument
    uint32_t ral = (eerd(EEPROM_MAC_ADDR2) << 16) | eerd(EEPROM_MAC_ADDR1);
    uint32_t rah = eerd(EEPROM_MAC_ADDR3);
    // assert(ral == 0x12005452);
    // assert(rah == 0x5634);

    ethw(E1000_RAL, ral);
    ethw(E1000_RAH, rah | E1000_RAH_AV);

    ethw(E1000_MTA, 0);

    ethw(E1000_IMS, 0);

    // bind rx buffer to the rx descriptors
    for (i = 0; i < E1000_RCV_MAX; i++) {
        rx_queue[i].addr = PADDR((void*)rx_buffer[i]); 
    }
    ethw(E1000_RDBAL, PADDR(rx_queue));
    ethw(E1000_RDBAH, 0);

    uint32_t rx_queue_sz = E1000_RCV_MAX * sizeof(struct rx_desc);
    assert(rx_queue_sz % 128 == 0);
    ethw(E1000_RDLEN, rx_queue_sz);

    ethw(E1000_RDH, 1);
    ethw(E1000_RDT, 0);

    ethw(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_SZ_2048
            | E1000_RCTL_SECRC);

    return 0;
}
