#ifndef __NET__ARP_H__
#define __NET__ARP_H__

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

void vxNetHandleARP(struct arp_packet* arp);

#endif // __NET__ARP_H__