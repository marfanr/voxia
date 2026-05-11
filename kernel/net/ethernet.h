#ifndef __NET__ETHERNET_H__
#define __NET__ETHERNET_H__

#include "net/netbuff.h"
#include "net/netdev.h"
#include <type.h>

struct ethernet_header {
	uint8_t dest_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
} __attribute__((packed));

void ethernet_send_frame(netdev_t* dev, struct netbuff* netbuff,
			 uint16_t ethertype, const uint8_t dst_mac[6]);

#define ETHER_TYPE_ARP 0x0806
#define ETHER_TYPE_IP 0x0800

#endif // __NET__ETHERNET_H__
