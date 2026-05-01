#include "icmp.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "net/ethernet.h"
#include "net/ip_type.h"
#include "net/ipv4.h"
#include "net/netbuff.h"
#include "net/socket.h"
#include "netutils.h"

void handle_icmp(netdev_t* dev, struct ipv4_header* ip, uint8_t mac_src[6]) {

	uint8_t ihl = ip->version_ihl & 0x0F;
	uint16_t ip_hdr_len = ihl * 4;

	uint16_t total_len = vxNtohs(ip->total_length);
	if (total_len < ip_hdr_len + 8)
		return; // minimal ICMP

	struct icmp_header* icmp =
		(struct icmp_header*) ((uint8_t*) ip + ip_hdr_len);

	uint16_t icmp_len = total_len - ip_hdr_len;

	// echo request
	if (icmp->type == 8) {
		LOG2_INFO("Socket", "icmp echo detected");

		auto nb = create_netbuff();

		// imcp
		struct icmp_echo* icmp_reply =
			(struct icmp_echo*) netbuff_put(nb, icmp_len);

		// copy seluruh ICMP (header + data)
		memcopy(icmp_reply, icmp, icmp_len);

		icmp_reply->type = 0; // echo reply
		icmp_reply->checksum = 0;
		icmp_reply->checksum =
			checksum16_adc((uint16_t*) icmp_reply, icmp_len);

		// ip
		struct ipv4_header* ip_reply =
			(struct ipv4_header*) netbuff_push(nb, ip_hdr_len);

		// copy header asli (biar version, ihl, tos, flags ikut)
		memcopy(ip_reply, ip, ip_hdr_len);

		ip_reply->src_ip = ip->dst_ip;
		ip_reply->dst_ip = ip->src_ip;
		ip_reply->ttl = 64;
		ip_reply->total_length = vxHtons(ip_hdr_len + icmp_len);

		ip_reply->checksum = 0;
		ip_reply->checksum =
			checksum16_adc((uint16_t*) ip_reply, ip_hdr_len);

		ethernet_send_frame(dev, nb, vxHtons(ETHER_TYPE_IP), mac_src);

		free_netbuff(nb);
	}
}