#ifndef __HAL__ETH__e1000__e1000_H__
#define __HAL__ETH__e1000__e1000_H__

#include <type.h>

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

struct e1000_rx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t status;
	volatile uint8_t errors;
	volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint8_t cso;
	volatile uint8_t cmd;
	volatile uint8_t status;
	volatile uint8_t css;
	volatile uint16_t special;
} __attribute__((packed));

void eth_e1000_init();
void e1000_irq();

#endif // __HAL__ETH__e1000__e1000_H__