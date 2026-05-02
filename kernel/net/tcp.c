#include "tcp.h"
#include "libk/serial.h"
#include "net/ip_type.h"
#include "net/ipv4.h"
#include "net/netbuff.h"
#include "net/netdev.h"
#include "net/netutils.h"
#include <hal/rand/rand.h>
#include <libk/str.h>

#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_RST 0x04
#define FLAG_PSH 0x08
#define FLAG_ACK 0x10
#define FLAG_URG 0x20

void handle_tcp(netdev_t* dev, struct ipv4_header* ip, uint8_t mac_dst[6]) {
	uint8_t ihl = ip->version_ihl & 0x0F;
	uint16_t ip_hdr_len = ihl * 4;

	uint16_t total_len = vxNtohs(ip->total_length);
	struct tcp_header* tcp =
		(struct tcp_header*) ((uint8_t*) ip + ip_hdr_len);

	uint8_t tcp_hdr_len = ((tcp->offset >> 4) & 0x0F) * 4;

	// validasi
	if (tcp_hdr_len < 20 || total_len < ip_hdr_len + tcp_hdr_len)
		return;

	// handle
	auto flags = tcp->flags;
	auto syn = flags & FLAG_SYN;
	auto ack = flags & FLAG_ACK;
	auto fin = flags & FLAG_FIN;

	tcp_options_t client_opts;
	parse_tcp_options(tcp, &client_opts);

	if (syn && !ack) {
		LOG2_INFO("TCP", "Syn Received");

		send_command(dev, ip, tcp, &client_opts, FLAG_SYN | FLAG_ACK,
			     mac_dst);
	}

	if (ack && !syn) {
		uint8_t tcp_hdr_len = ((tcp->offset >> 4) & 0x0F) * 4;
		uint16_t data_len = total_len - ip_hdr_len - tcp_hdr_len;
		uint8_t* payload = (uint8_t*) tcp + tcp_hdr_len;

		if (data_len > 0 && tcp->flags & FLAG_PSH) {
			LOG2_INFO("TCP", "Data diterima: %d bytes", data_len);

			if (data_len >= 4
			    && strncmp((char*) payload, "GET ", 4) == 0) {
				LOG2_INFO("TCP", "http request");

				// print payload
				serial2_printf("%s", payload);
				// 2. Kirim HTTP response
				char* http_resp = "HTTP/1.1 200 OK\r\n"
						  "Content-Length: 13\r\n"
						  "Connection: close\r\n"
						  "\r\n"
						  "Hello, World!";
				send_tcp_data(dev, ip, tcp,
					      (uint8_t*) http_resp, 71,
					      mac_dst);
			}
		} else if (fin) {
			// ACK untuk FIN client
			// send_tcp_ack(dev, ip, tcp, mac_dst);
			send_command(dev, ip, tcp, &client_opts, FLAG_ACK,
				     mac_dst);
		}
	}
}

void send_command(netdev_t* dev, struct ipv4_header* ip, struct tcp_header* tcp,
		  tcp_options_t* opt, uint8_t flags, uint8_t mac_dst[6]) {

	uint8_t opt_buf[80];
	uint8_t opt_len = build_synack_options(dev, opt_buf, opt);

	auto nb = create_netbuff();

	auto reply_len = sizeof(struct tcp_header) + opt_len;
	struct tcp_header* tcp_reply =
		(struct tcp_header*) netbuff_push(nb, reply_len);

	if (!tcp_reply) {
		free_netbuff(nb);
		return;
	}

	uint8_t* opt_ptr = (uint8_t*) (tcp_reply + 1);
	memcopy(opt_ptr, opt_buf, opt_len);
	tcp_reply->offset = ((5 + opt_len / 4) << 4);

	tcp_reply->source_port = tcp->destination_port;
	tcp_reply->destination_port = tcp->source_port;
	tcp_reply->sequence = vxHtonl(vxRand());
	tcp_reply->acknowledgment = vxHtonl(vxNtohl(tcp->sequence) + 1);

	tcp_reply->flags = flags;
	tcp_reply->window = vxHtons(65535);

	tcp_reply->checksum = 0;

	// pseudo ip
	struct __attribute__((packed)) {
		uint32_t src_ip;
		uint32_t dst_ip;
		uint8_t zero;
		uint8_t protocol;
		uint16_t tcp_length;
	} pseudo = {0};

	pseudo.src_ip = ip->dst_ip;
	pseudo.dst_ip = ip->src_ip;
	pseudo.protocol = TCP_PROTOCOL;
	pseudo.tcp_length = vxHtons((uint16_t) reply_len);

	uint32_t sum = 0;
	sum = checksum16_raw((uint16_t*) &pseudo, sizeof(pseudo));
	sum += checksum16_raw((uint16_t*) tcp_reply, reply_len);

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	tcp_reply->checksum = (uint16_t) (~sum);

	ipv4_send(dev, nb, ip->src_ip, TCP_PROTOCOL, mac_dst);
	free_netbuff(nb);
}

void send_tcp_data(netdev_t* dev, struct ipv4_header* ip,
		   struct tcp_header* tcp, uint8_t* data, size_t len,
		   uint8_t mac_dst[6]) {

	// 1. Hitung panjang data yang masuk (HTTP GET) untuk menentukan nilai ACK balasan
	uint8_t ihl = ip->version_ihl & 0x0F;
	uint16_t ip_hdr_len = ihl * 4;
	uint16_t ip_total_len = vxNtohs(ip->total_length);
	uint8_t tcp_hdr_len_in = ((tcp->offset >> 4) & 0x0F) * 4;
	uint16_t incoming_data_len = ip_total_len - ip_hdr_len - tcp_hdr_len_in;

	uint16_t total_tcp_len = sizeof(struct tcp_header) + len;

	auto nb = create_netbuff();
	// 3. Salin data (payload HTTP) tepat setelah TCP Header
	uint8_t* payload_ptr = (uint8_t*) netbuff_put(nb, len);
	memcopy(payload_ptr, data, len);

	struct tcp_header* tcp_reply = (struct tcp_header*) netbuff_push(
		nb, sizeof(struct tcp_header));

	if (!tcp_reply) {
		free_netbuff(nb);
		return;
	}

	tcp_reply->offset = (5 << 4); // 5 words = 20 bytes (Tanpa Options)

	// 4. Konfigurasi Header TCP
	tcp_reply->source_port = tcp->destination_port;
	tcp_reply->destination_port = tcp->source_port;

	// SEQ kita = ACK yang diminta oleh klien
	tcp_reply->sequence = tcp->acknowledgment;

	// ACK kita = SEQ klien + panjang data (GET request) yang kita terima
	uint32_t seq_in = vxNtohl(tcp->sequence);
	tcp_reply->acknowledgment = vxHtonl(seq_in + incoming_data_len);

	tcp_reply->flags =
		FLAG_ACK | FLAG_FIN
		| FLAG_PSH; // PSH (Push) memberitahu OS klien untuk segera membaca data
	tcp_reply->window = vxHtons(65535);
	tcp_reply->checksum = 0;

	// 5. Buat Pseudo IP untuk Checksum
	struct __attribute__((packed)) {
		uint32_t src_ip;
		uint32_t dst_ip;
		uint8_t zero;
		uint8_t protocol;
		uint16_t tcp_length;
	} pseudo = {0};

	pseudo.src_ip = ip->dst_ip;
	pseudo.dst_ip = ip->src_ip;
	pseudo.protocol = TCP_PROTOCOL;
	pseudo.tcp_length = vxHtons((uint16_t) total_tcp_len);

	// 6. Hitung Checksum secara sekuensial (Mendukung data berukuran ganjil)
	uint32_t sum = 0;
	sum = checksum16_raw((uint16_t*) &pseudo, sizeof(pseudo));
	sum += checksum16_raw((uint16_t*) tcp_reply, sizeof(struct tcp_header));
	sum += checksum16_raw((uint16_t*) payload_ptr, len);

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	tcp_reply->checksum = (uint16_t) (~sum);

	// 7. Kirim paket
	ipv4_send(dev, nb, ip->src_ip, TCP_PROTOCOL, mac_dst);
	free_netbuff(nb);
}

void parse_tcp_options(struct tcp_header* tcp, tcp_options_t* out) {
	memset(out, 0, sizeof(*out));
	out->mss = 536; // default RFC 793

	uint8_t* opt = (uint8_t*) (tcp + 1);
	uint8_t hdr_len = ((tcp->offset >> 4) & 0x0F) * 4;
	uint8_t* end = (uint8_t*) tcp + hdr_len;

	while (opt < end) {
		switch (*opt) {
		case 0:
			return; // End of options
		case 1:
			opt++;
			continue; // NOP

		case 2: // MSS
			if (opt[1] == 4)
				out->mss = (opt[2] << 8) | opt[3];
			opt += opt[1];
			break;

		case 3: // Window Scale
			if (opt[1] == 3)
				out->has_wscale = 1;
			out->wscale = opt[2];
			opt += opt[1];
			break;

		case 4: // SACK Permitted
			out->sack_permitted = 1;
			opt += opt[1];
			break;

		case 8: // Timestamp
			if (opt[1] == 10) {
				out->has_timestamp = 1;
				out->ts_val = ((uint32_t) opt[2] << 24)
					      | ((uint32_t) opt[3] << 16)
					      | ((uint32_t) opt[4] << 8)
					      | opt[5];
				out->ts_ecr = ((uint32_t) opt[6] << 24)
					      | ((uint32_t) opt[7] << 16)
					      | ((uint32_t) opt[8] << 8)
					      | opt[9];
			}
			opt += opt[1];
			break;

		default:
			if (opt[1] == 0)
				return; // corrupt, stop
			opt += opt[1];
			break;
		}
	}
}

uint8_t
build_synack_options(netdev_t* dev, uint8_t* buf, tcp_options_t* client_opts) {
	uint8_t* p = buf;

	// 1. MSS (4 bytes)
	uint16_t our_mss = dev->mtu - sizeof(struct ipv4_header)
			   - sizeof(struct tcp_header);
	uint16_t mss =
		(client_opts->mss < our_mss) ? client_opts->mss : our_mss;
	*p++ = 2;
	*p++ = 4;
	*p++ = (mss >> 8) & 0xFF;
	*p++ = mss & 0xFF;
	*p++ = 1; // NOP

	// 2. SACK Permitted (2 bytes)
	// if (client_opts->sack_permitted) {
	// 	*p++ = 4;
	// 	*p++ = 2;
	// }

	// timestamp, butuh RTC
	// if (client_opts->has_timestamp) {
	// 	// Tambahkan NOP sampai posisi saat ini genap kelipatan 4
	// 	while ((p - buf) % 4 != 0)
	// 		*p++ = 1; // NOP

	// 	*p++ = 8;
	// 	*p++ = 10;
	// 	uint32_t now =
	// 		1000; // Berikan nilai sembarang non-zero untuk SYN-ACK pertama

	// 	*p++ = (now >> 24) & 0xFF;
	// 	*p++ = (now >> 16) & 0xFF;
	// 	*p++ = (now >> 8) & 0xFF;
	// 	*p++ = now & 0xFF;
	// 	*p++ = (client_opts->ts_val >> 24) & 0xFF;
	// 	*p++ = (client_opts->ts_val >> 16) & 0xFF;
	// 	*p++ = (client_opts->ts_val >> 8) & 0xFF;
	// 	*p++ = client_opts->ts_val & 0xFF;
	// }

	// 4. Window Scale (3 bytes)
	// if (client_opts->has_wscale) { // Gunakan boolean flag di sini!
	// 	*p++ = 1; // 1 NOP sebelum WScale agar total opsi menjadi genap 4 byte
	// 	*p++ = 3;
	// 	*p++ = 3;
	// 	*p++ = 7; // Skala kita (misal: 7)
	// }

	// 5. Padding di akhir memastikan total option length habis dibagi 4
	// while ((p - buf) % 4 != 0)
	// 	*p++ = 1; // NOP

	return (uint8_t) (p - buf);
}
