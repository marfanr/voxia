#include "net/socket.h"
#include "init/init.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "memory/slab.h"
#include "etherent.h"
#include "net/arp.h"
#include "net/icmp.h"
#include "net/ipv4.h"

static struct slab_cache* socket_cache = 0;
static socket_ops_t* socket_ops = 0;

static int socket_receive(socket_t* socket, void* buffer, size_t size);
static int
socket_set_sockopt(socket_t* socket, uint32_t level, uint32_t optname,
		   const void* optval, uint32_t optlen);
static int socket_bind(socket_t* socket, sockaddr_in_t* addr, uint32_t len);

INIT(Socket) {
	socket_ops = (socket_ops_t*) kalloc(sizeof(socket_ops_t));
	socket_ops->recv = socket_receive;
	socket_ops->set_sockopt = socket_set_sockopt;
	socket_ops->bind = socket_bind;
}

uint32_t vxInetAddr(const char* addr) {
	uint32_t ip = 0;
	uint8_t* b = (uint8_t*) &ip;

	for (int i = 0; i < 4; i++) {
		uint32_t octet = 0;
		while (*addr >= '0' && *addr <= '9') {
			octet = (octet * 10)
				+ (*addr
				   - '0'); // Konversi karakter ASCII ke integer
			addr++; // Geser pointer string ke karakter berikutnya
		}
		b[i] = (uint8_t) octet;
		if (*addr == '.') {
			addr++;
		} else if (*addr == '\0' && i < 3) {
			return 0; // Atau return error code khusus Anda
		}
	}

	return ip;
}

static int u8_to_str(uint8_t val, char* buf) {
	int i = 0;

	if (val >= 100) {
		buf[i++] = '0' + (val / 100);
		val %= 100;
		buf[i++] = '0' + (val / 10);
		buf[i++] = '0' + (val % 10);
	} else if (val >= 10) {
		buf[i++] = '0' + (val / 10);
		buf[i++] = '0' + (val % 10);
	} else {
		buf[i++] = '0' + val;
	}

	return i;
}

char* vxInetNtoa(uint32_t ip, char* buffer) {
	uint8_t* b = (uint8_t*) &ip;
	int len = 0;

	for (int i = 0; i < 4; i++) {
		len += u8_to_str(b[i], buffer + len);
		if (i < 3)
			buffer[len++] = '.';
	}

	buffer[len] = '\0';
	return buffer;
}

uint16_t vxHtons(uint16_t value) {
	return (value >> 8) | (value << 8);
}

uint16_t vxNtohs(uint16_t netshort) {
	return (netshort >> 8) | (netshort << 8);
}

void vxSocket(sock_family_t family, sock_type_t type, uint16_t protocol,
	      socket_t** socket) {
	// kalau belum ada cache buat dulu
	if (!socket_cache)
		vxCreateSlabCache(&socket_cache, "socket", sizeof(socket_t), 0,
				  0);

	*socket = (socket_t*) vxSlabAlloc(socket_cache);
	(*socket)->family = family;
	(*socket)->type = type;
	(*socket)->protocol = protocol;

	if (!socket_ops)
		LOG2_WARN("Socket", "socket ops not initialized");

	(*socket)->ops = socket_ops;
}

// checksum
uint16_t checksum16(const uint16_t* data, size_t length) {
	uint32_t sum = 0;

	// jumlahkan per 16-bit
	while (length > 1) {
		sum += *data++;
		length -= 2;
	}

	// kalau ada sisa 1 byte
	if (length > 0) {
		sum += *((uint8_t*) data);
	}

	// fold 32-bit ke 16-bit
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	// one's complement
	return (uint16_t) (~sum);
}

uint16_t checksum16_adc(const uint16_t* data, size_t length) {
	uint64_t sum = 0;

	while (length >= 2) {
		__asm__ volatile("addw (%1), %w0\n\t"
				 "adcq $0, %0\n\t"
				 : "+r"(sum)
				 : "r"(data)
				 : "memory");
		data++;
		length -= 2;
	}

	if (length) {
		sum += *(uint8_t*) data;
	}

	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

// sementara hardcode
#define MYIP "192.168.100.73"

static int socket_receive(socket_t* socket, void* buffer, size_t size) {
	auto family = socket->family;
	auto type = socket->type;

	auto nic = socket->bound_nic;

	// queue kosong
	if (nic->pq_tail == nic->pq_head) {
		return -1;
	}

	struct pending_rx* rx = &nic->pending_queue[nic->pq_tail];

	// jumlah byte yang aman dicopy
	int n = (rx->len < size) ? rx->len : size;

	struct ethernet_header* eth = (struct ethernet_header*) rx->data;

	uint16_t ethertype = (eth->ethertype << 8) | (eth->ethertype >> 8);
	// LOG2_INFO("Socket", "packet type = 0x%x", ethertype);

	// my mac addr
	uint8_t my_mac[6] = {0};
	if (nic->ops->get_mac_address(my_mac)) {
		// serial2_printf("success retrieve mac address from nic\n");
	}

	// serial2_printf("my mac : ");
	// for (int i = 0; i < 6; i++) {
	// 	serial2_printf("%x", my_mac[i]);
	// 	if (i < 5)
	// 		serial2_printf(":");
	// }
	// serial2_printf("\n");

	if (ethertype == 0x0800) {

		struct ipv4_header* ip =
			(struct ipv4_header*) (rx->data
					       + sizeof(
						       struct ethernet_header));

		char ip_buf[16];
		vxInetNtoa(ip->src_ip, ip_buf);
		// LOG2_INFO("Socket", "ipv4 packet from %s", ip_buf);
		// LOG2_INFO("SOcket", "ipv4 protocol : %d", ip->protocol);

		// icmp
		if (ip->protocol == 1) {
			// LOG2_INFO("Socket", "icmp detected");

			uint8_t ihl = ip->version_ihl & 0x0F;
			struct icmp_header* icmp =
				(struct icmp_header*) ((uint8_t*) ip
						       + (ihl * 4));

			uint16_t total_len = vxNtohs(ip->total_length);
			uint16_t icmp_len = total_len - (ihl * 4);

			//    echo
			if (icmp->type == 8) {
				// LOG2_INFO("Socket", "icmp echo detected");

				struct icmp_echo* echo =
					(struct icmp_echo*) icmp;

				uintptr_t paddr;
				size_t size = sizeof(struct ethernet_header)
					      + total_len;

				if (size < 60)
					size = 60;

				size_t aligned_size =
					((size + 0x1000 - 1) & ~(0x1000 - 1))
					/ 0x1000;

				uintptr_t vaddr = (uintptr_t) ioforge_dma_alloc(
					aligned_size, &paddr);

				memset((void*) vaddr, 0, size);

				// icmp_response
				struct ethernet_header* eth_reply =
					(struct ethernet_header*) vaddr;
				struct ipv4_header* ip_reply =
					(struct
					 ipv4_header*) (vaddr
							+ sizeof(
								struct
								ethernet_header));
				struct icmp_echo* icmp_reply =
					(struct
					 icmp_echo*) (vaddr
						      + sizeof(struct
							       ethernet_header)
						      + sizeof(struct
							       ipv4_header));

				//    ethernet
				memcopy(eth_reply->dest_mac, eth->src_mac, 6);
				memcopy(eth_reply->src_mac, my_mac, 6);
				eth_reply->ethertype = vxHtons(0x0800);

				// ipv4
				memcopy(ip_reply, ip, ihl * 4);

				ip_reply->src_ip = ip->dst_ip;
				ip_reply->dst_ip = ip->src_ip;
				ip_reply->ttl = 64;
				ip_reply->checksum = 0;

				// icmp
				memcopy(icmp_reply, icmp, icmp_len);

				icmp_reply->type = 0; // echo reply
				icmp_reply->checksum = 0;

				// checksum
				icmp_reply->checksum = checksum16_adc(
					(uint16_t*) icmp_reply, icmp_len);
				ip_reply->checksum = checksum16_adc(
					(uint16_t*) ip_reply, ihl * 4);

				// send
				if (nic->ops->send((void*) paddr, size)) {
					// serial2_printf("icmp response sended "
					// 	       "size (%d)\n",
					// 	       size);
				}

				ioforge_dma_free((void*) paddr, (void*) vaddr,
						 aligned_size);
			}
		}
	}

	if (ethertype == 0x0806) {
		struct arp_packet* arp =
			(struct arp_packet*) (rx->data
					      + sizeof(struct ethernet_header));
		char ip_buf[16];
		vxInetNtoa(arp->sender_ip, ip_buf);
		LOG2_INFO("Socket", "arp packet from %s", ip_buf);

		serial2_printf("mac target : ");
		for (int i = 0; i < 6; i++) {
			serial2_printf("%x", eth->dest_mac[i]);
			if (i < 5)
				serial2_printf(":");
		}
		serial2_printf("\n");

		auto target = arp->target_ip;
		if (vxInetAddr(MYIP) == target) {
			LOG2_INFO("ARP", "success targeting me");

			uintptr_t paadr = 0;
			size_t size = sizeof(struct ethernet_header)
				      + sizeof(struct arp_packet);
			size_t aligned_size =
				((size + 0x1000 - 1) & ~(0x1000 - 1)) / 0x1000;

			auto vaddr = (uintptr_t) ioforge_dma_alloc(aligned_size,
								   &paadr);

			struct ethernet_header* eth_reply =
				(struct ethernet_header*) vaddr;
			struct arp_packet* reply =
				(struct
				 arp_packet*) (vaddr
					       + sizeof(
						       struct ethernet_header));

			// Etthernet
			memcopy(eth_reply->dest_mac, eth->src_mac,
				6);				// ke pengirim
			memcopy(eth_reply->src_mac, my_mac, 6); // MAC kita

			eth_reply->ethertype = 0x0608; // htons(0x0806)

			// arp
			reply->htype = 0x0100; // htons(1) → Ethernet
			reply->ptype = 0x0008; // htons(0x0800) → IPv4
			reply->hlen = 6;
			reply->plen = 4;
			reply->oper = 0x0200; // htons(2) → reply

			// sender = kita
			memcopy(reply->sender_mac, my_mac, 6);
			reply->sender_ip = arp->target_ip;

			// target = pengirim request
			memcopy(reply->target_mac, arp->sender_mac, 6);
			reply->target_ip = arp->sender_ip;

			if (size < 60)
				size = 60;

			if (nic->ops->send((void*) paadr, size)) {
				serial2_printf(
					"ARP response sended size (%d)\n",
					size);
			}
		}
	}

	// copy ke buffer user
	memcopy(buffer, rx->data, n);

	// advance tail (consume entry)
	nic->pq_tail = (nic->pq_tail + 1) % PENDING_QUEUE_SIZE;

	return n;
}

static int
socket_set_sockopt(socket_t* socket, uint32_t level, uint32_t optname,
		   const void* optval, uint32_t optlen) {

	switch (level) {
	case SOL_SOCKET: {
		switch (optname) {
		case SO_BINDTODEVICE: {
			auto nic = IOforgeNICFindByName((char*) optval);
			if (!nic) {
				LOG2_ERROR("Socket", "NIC Not found, failed to "
						     "bind into the socket");
				return SOCK_ERR_NODEV;
			}
			socket->bound_nic = nic;
			return SOCK_OK;
			break;
		}
		}
	}
	}
}

static int socket_bind(socket_t* socket, sockaddr_in_t* addr, uint32_t len) {
}