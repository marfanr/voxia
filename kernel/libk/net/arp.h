#ifndef __NET__ARP_H__
#define __NET__ARP_H__

#include <type.h>

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4 0x0800
#define ARP_HLEN_ETHERNET 6
#define ARP_PLEN_IPV4 4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

typedef struct {
	uint16_t htype;
	uint16_t ptype;
	uint8_t hlen;
	uint8_t plen;
	uint16_t op;
} __attribute__((packed)) arp_header_t;

#endif // __NET__ARP_H__