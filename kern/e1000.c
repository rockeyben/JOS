#include <kern/e1000.h>

// LAB 6: Your driver code here

#define vmask(v, mask) v * (mask & (~(mask << 1)))

struct e1000_tx_desc tx_desc_buf[E1000_TX_LEN] __attribute__ ((aligned(PGSIZE)));
struct e1000_data tx_data_buf[E1000_TX_LEN] __attribute__ ((aligned (PGSIZE)));

struct e1000_rx_desc rx_desc_buf[E1000_RX_LEN] __attribute__ ((aligned (PGSIZE)));
struct e1000_data rx_data_buf[E1000_RX_LEN] __attribute__ ((aligned (PGSIZE)));

void desc_init()
{
    int i;
    for(i = 0; i < E1000_TX_LEN; i++){
        tx_desc_buf[i].buffer_addr = PADDR(&tx_data_buf[i]);
        tx_desc_buf[i].upper.fields.status = E1000_TXD_STAT_DD;
    }
    for(i = 0; i < E1000_RX_LEN; i++){
        rx_desc_buf[i].buffer_addr = PADDR(&rx_data_buf[i]);
    }
}

void e1000_init()
{
    // transmitting pkt
    e1000[E1000_TDBAL / 4] = PADDR(&tx_desc_buf);
    e1000[E1000_TDBAH / 4] = 0x0;
    e1000[E1000_TDH / 4] = 0x0;
    e1000[E1000_TDT / 4] = 0x0;
    e1000[E1000_TDLEN / 4] = E1000_TX_LEN * sizeof(struct e1000_tx_desc);
    e1000[E1000_TCTL / 4] = vmask(1, E1000_TCTL_EN) |
                                vmask(1, E1000_TCTL_PSP) |
                                vmask(0x10, E1000_TCTL_CT) |
                                vmask(0x40, E1000_TCTL_COLD);
    e1000[E1000_TIPG / 4] = vmask(10, E1000_TIPG_IPGT) |
                                vmask(6, E1000_TIPG_IGPR1) |
                                vmask(8, E1000_TIPG_IGPR2);

    // receiving pkt
    e1000[E1000_RDBAL / 4] = PADDR(&rx_desc_buf);
    e1000[E1000_RDBAH / 4] = 0x0;
    e1000[E1000_RDH / 4] = 0x0;
    e1000[E1000_RDT / 4] = E1000_RX_LEN;
    e1000[E1000_RDLEN / 4] = E1000_RX_LEN * sizeof(struct e1000_rx_desc);
    e1000[E1000_RCTL / 4] = E1000_RCTL_EN |
                                !E1000_RCTL_LPE |
                                E1000_RCTL_LBM_NO |
                                E1000_RCTL_RDMTS_HALF |
                                E1000_RCTL_MO_0 |
                                E1000_RCTL_BAM |
                                E1000_RCTL_BSEX |
                                E1000_RCTL_SZ_4096 |
                                E1000_RCTL_SECRC;
                                


} 

int e1000_attach(struct pci_func * pcif)
{
    pci_func_enable(pcif);
    desc_init();
    e1000 =  mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    e1000_init();
    assert(e1000[E1000_STATUS / 4] == 0x80080783);
    return 0;
}

int e1000_trans_pkt(uint8_t * addr, size_t size)
{
    uint32_t tail = e1000[E1000_TDT / 4];
    struct e1000_tx_desc * tx_tail = &tx_desc_buf[tail];

    // Desciptor Done
    // # Indicates that the descriptor is finished and is written back either after the
    // # desciptor is processed or after the pkt has been transmitted on wire.
    if (tx_tail->upper.fields.status != E1000_TXD_STAT_DD)
        return -1;
    
    if (size > DATA_SIZE)
        size = DATA_SIZE;
    
    memmove(&tx_data_buf[tail], addr, size);
    tx_tail->lower.flags.length = size;
    tx_tail->upper.fields.status = 0;
    tx_tail->lower.data |= E1000_TXD_CMD_RS; // Report Status

    e1000[E1000_TDT / 4] = (tail + 1) % E1000_TX_LEN;
    return 0;
}

int e1000_recv_pkt(uint8_t * addr)
{
    static uint32_t real_tail = 0;
    uint32_t tail = real_tail;
    struct e1000_rx_desc * rx_tail = &rx_desc_buf[tail];
    if(!(rx_tail->status & E1000_RXD_STAT_DD))
        return -1;
    
    size_t size = rx_tail->length;
    memmove(addr, &rx_data_buf[tail], size);
    rx_tail->status = 0;
    e1000[E1000_RDT / 4] = tail;
    real_tail = (tail + 1) % E1000_RX_LEN;
    return size;
}