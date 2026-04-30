#include "arp.h"
#include "ethernet.h"
#include "libk/str.h"
#include "net/socket.h"

void arp_reply(struct ioforge_nic_service* nic, uint32_t ip,
	       uint8_t out_mac[6]) {
	auto netbuff = create_netbuff(nic);

	struct arp_packet* reply = (struct arp_packet*) netbuff_put(
		netbuff, sizeof(struct arp_packet));

	uint8_t my_mac[6] = {0};
	nic->ops->get_mac_address(my_mac);

	// arp
	reply->htype = 0x0100; // htons(1) → Ethernet
	reply->ptype = 0x0008; // htons(0x0800) → IPv4
	reply->hlen = 6;
	reply->plen = 4;
	reply->oper = 0x0200; // htons(2) → reply

	// sender = kita
	memcopy(reply->sender_mac, my_mac, 6);
	reply->sender_ip = vxInetAddr("192.168.100.80");

	// target = pengirim request
	memcopy(reply->target_mac, out_mac, 6);
	reply->target_ip = ip;

	ethernet_send_frame(nic, netbuff, 0x0608, out_mac);

	free_netbuff(netbuff);
}