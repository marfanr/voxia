#include "ipv4.h"
#include "ethernet.h"
#include "libk/serial.h"
#include <str.h>
#include "net/netdev.h"
#include "net/netutils.h"

static void fill_ip_header(struct ipv4_header* ip_hdr, uint16_t total_length,
			   uint16_t id, uint16_t flags_fragment,
			   uint8_t protocol, uint32_t src_ip, uint32_t dst_ip) {
	ip_hdr->version_ihl = 0x45; // Version 4, IHL 5 (20 bytes)
	ip_hdr->tos = 0;
	ip_hdr->total_length = vxHtons(total_length);

	// sementara belum butuh fragmentasi
	ip_hdr->id = 0;
	ip_hdr->flags_fragment = 0;
	ip_hdr->ttl = 64;
	ip_hdr->protocol = protocol;
	// sementara hardcode
	ip_hdr->src_ip = src_ip;
	ip_hdr->dst_ip = dst_ip;

	ip_hdr->checksum = 0;
	ip_hdr->checksum =
		checksum16_adc((uint16_t*) ip_hdr, sizeof(struct ipv4_header));
}

void ipv4_send(netdev_t* dev, struct netbuff* nb, uint32_t dst_ip,
	       uint8_t protocol, uint8_t mac_dest[6]) {

	uint16_t mtu = dev->mtu;
	uint16_t max_ip_payload = mtu - sizeof(struct ipv4_header);
	uint16_t total_payload_len = nb->length;
	uint16_t sent = 0;
	uint16_t ip_id = get_next_ip_id(dev);

	// hardcode
	auto ip_addr = vxInetAddr("192.168.100.80");
	// serial2_printf("ipv4\n");

	if (total_payload_len <= max_ip_payload) {
		struct ipv4_header* ip_hdr = (struct ipv4_header*) netbuff_push(
			nb, sizeof(struct ipv4_header));
		fill_ip_header(ip_hdr,
			       total_payload_len + sizeof(struct ipv4_header),
			       ip_id, 0, protocol, ip_addr, dst_ip);
		ethernet_send_frame(dev, nb, vxHtons(ETHER_TYPE_IP), mac_dest);
		return;
	}

	while (sent < total_payload_len) {
		uint16_t chunk_size =
			(total_payload_len - sent > max_ip_payload)
				? max_ip_payload
				: (total_payload_len - sent);

		// Buat netbuff baru untuk fragmen ini
		auto frag_nb = create_netbuff();
		uint8_t* dest = netbuff_put(frag_nb, chunk_size);

		// Copy potongan data dari netbuff utama ke netbuff fragmen
		memcopy(dest, nb->data + sent, chunk_size);

		// Tempel Header IP
		struct ipv4_header* ip_hdr = (struct ipv4_header*) netbuff_push(
			frag_nb, sizeof(struct ipv4_header));

		// Hitung Flags & Offset
		uint16_t offset = sent / 8;
		uint16_t flags_offset = offset & 0x1FFF;
		if (sent + chunk_size < total_payload_len) {
			flags_offset |= 0x2000; // More Fragments (MF) = 1
		}

		fill_ip_header(ip_hdr, frag_nb->length, ip_id, flags_offset,
			       protocol, ip_addr, dst_ip);

		ethernet_send_frame(dev, frag_nb, vxHtons(ETHER_TYPE_IP),
				    mac_dest);

		sent += chunk_size;
		free_netbuff(frag_nb); // Hapus fragmen setelah dikirim
	}
}