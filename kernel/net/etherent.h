#ifndef __NET__ETHERNET_H__
#define __NET__ETHERNET_H__

#include <type.h>

struct ethernet_header {
	uint8_t dest_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
} __attribute__((packed));

#endif // __NET__ETHERNET_H__
