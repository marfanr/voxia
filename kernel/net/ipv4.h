#ifndef __NET__IPV4_H__
#define __NET__IPV4_H__

#include "net/netbuff.h"
#include "net/netdev.h"
#include <type.h>

struct ipv4_header {
	uint8_t version_ihl;
	uint8_t tos;
	uint16_t total_length;
	uint16_t id;
	uint16_t flags_fragment;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t checksum;
	uint32_t src_ip;
	uint32_t dst_ip;
} __attribute__((packed));

void ipv4_send(netdev_t* dev, struct netbuff* nb, uint32_t dst_ip,
	       uint8_t protocol, uint8_t mac_dest[6]);
#endif