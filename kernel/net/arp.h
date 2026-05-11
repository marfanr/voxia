#ifndef __NET__ARP_H__
#define __NET__ARP_H__

#include "net/netdev.h"
#include <type.h>

struct arp_packet {
	uint16_t htype;
	uint16_t ptype;
	uint8_t hlen;
	uint8_t plen;
	uint16_t oper;

	uint8_t sender_mac[6];
	uint32_t sender_ip;

	uint8_t target_mac[6];
	uint32_t target_ip;
} __attribute__((packed));

void arp_reply(netdev_t* dev, uint32_t ip, uint8_t out_mac[6]);

#endif // __NET__ARP_H__