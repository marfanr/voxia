#include "icmp.h"
#include "libk/serial.h"
#include <str.h>
#include "net/ethernet.h"
#include "net/ip_type.h"
#include "net/ipv4.h"
#include "net/netbuff.h"
#include "net/socket.h"
#include "netutils.h"

void handle_icmp(netdev_t* dev, struct ipv4_header* ip, uint8_t mac_dst[6]) {

	uint8_t ihl = ip->version_ihl & 0x0F;
	uint16_t ip_hdr_len = ihl * 4;

	uint16_t total_len = vxNtohs(ip->total_length);
	if (total_len < ip_hdr_len + 8)
		return; // minimal ICMP

	struct icmp_header* icmp =
		(struct icmp_header*) ((uint8_t*) ip + ip_hdr_len);

	uint16_t icmp_len = total_len - ip_hdr_len;

	// echo request
	char ip_buf[16];
	vxInetNtoa(ip->src_ip, ip_buf);

	uint16_t frag_field = vxNtohs(ip->flags_fragment);

	// 1. Cek bit "More Fragments" (MF)
	// Bit ini ada di posisi 0x2000
	bool has_more_fragments = (frag_field & 0x2000);

	// 2. Cek "Fragment Offset"
	// Offset ada di 13 bit bawah (0x1FFF)
	uint16_t fragment_offset = (frag_field & 0x1FFF);

	if (has_more_fragments || fragment_offset > 0) {
		LOG2_WARN("ICMP", "packet ini terfragmentasi");
		// TODO: handle reassembly untuk packet besar
		return;
	}

	if (icmp->type == 8) {
		auto nb = create_netbuff();

		// Alokasi space untuk ICMP payload + header
		struct icmp_echo* icmp_reply =
			(struct icmp_echo*) netbuff_put(nb, icmp_len);

		// Copy data dari ICMP request
		memcopy(icmp_reply, icmp, icmp_len);

		// Ubah jadi Echo Reply
		icmp_reply->type = 0;
		icmp_reply->checksum = 0;
		icmp_reply->checksum = checksum16_adc(
			(uint16_t*) (void*) icmp_reply, icmp_len);

		// Kirim ke layer IP
		ipv4_send(dev, nb, ip->src_ip, 1,
			  mac_dst); // 1 adalah protokol ICMP

		free_netbuff(nb);
	}
}