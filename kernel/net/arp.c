#include "arp.h"
#include "ethernet.h"
#include <str.h>
#include "netutils.h"

void arp_reply(netdev_t* dev, uint32_t ip, uint8_t out_mac[6]) {
	auto netbuff = create_netbuff();

	struct arp_packet* reply = (struct arp_packet*) netbuff_put(
		netbuff, sizeof(struct arp_packet));

	// arp
	reply->htype = 0x0100; // htons(1) → Ethernet
	reply->ptype = 0x0008; // htons(0x0800) → IPv4
	reply->hlen = 6;
	reply->plen = 4;
	reply->oper = 0x0200; // htons(2) → reply

	// sender = kita
	memcopy(reply->sender_mac, dev->mac, 6);
	reply->sender_ip = vxInetAddr("192.168.100.80");

	// target = pengirim request
	memcopy(reply->target_mac, out_mac, 6);
	reply->target_ip = ip;

	ethernet_send_frame(dev, netbuff, 0x0608, out_mac);

	free_netbuff(netbuff);
}