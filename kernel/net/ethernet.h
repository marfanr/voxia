#ifndef __NET__ETHERNET_H__
#define __NET__ETHERNET_H__

#include "ioforge/ioforge_nic.h"
#include "net/netbuff.h"
#include <type.h>

struct ethernet_header {
	uint8_t dest_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
} __attribute__((packed));

void ethernet_send_frame(struct ioforge_nic_service* nic,
			 struct netbuff* netbuff, uint16_t ethertype,
			 const uint8_t dst_mac[6]);

#endif // __NET__ETHERNET_H__
