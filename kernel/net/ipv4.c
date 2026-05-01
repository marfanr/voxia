#include "ipv4.h"
#include "ethernet.h"

// TODO:
// - alloc dihandle ini, jaid kirim struct aja
void ipv4_send(netdev_t* dev, struct netbuff* netbuff, uint32_t dst_ip,
	       uint16_t protocol) {

	struct ipv4_header* ip = (struct ipv4_header*) netbuff_put(
		netbuff, sizeof(struct ipv4_header));

	ip->version_ihl = 0x45;
}