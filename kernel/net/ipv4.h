#ifndef __NET__IPV4_H__
#define __NET__IPV4_H__

#include "ioforge/ioforge_nic.h"
#include "net/netbuff.h"
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

void ipv4_send(struct ioforge_nic_service* nic, struct netbuff* netbuff,
	       uint32_t dst_ip);
#endif