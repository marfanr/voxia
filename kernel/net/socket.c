#include "socket.h"
#include "init/init.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include <str.h>
#include "memory/slab.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "ipv4.h"
#include "ip_type.h"
#include "net/netbuff.h"
#include "tcp.h"
#include "netutils.h"
#include "netdev.h"
#include "type.h"

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

// hardcode
#define MYIP "192.168.100.80"

static int socket_receive(socket_t* socket, void* buffer, size_t size) {
	// auto family = socket->family;
	// auto type = socket->type;

	auto dev = socket->netdev;
	if (!dev) {
		return SOCK_ERR_NODEV;
	}

	auto nic = dev->nic;

	// TODO: handle if devnet is virtual device
	if (nic->pq_tail == nic->pq_head) {
		return 0;
	}

	struct pending_rx rx;
	if (ioforge_receive_pending_queue(nic, &rx) == -1) {
		return 0;
	}
	// serial2_printf("pending rx buffer 0x%x\n", rx.data);

	size_t n = (rx.len < size) ? rx.len : size;

	struct ethernet_header* eth = (struct ethernet_header*) rx.data;

	uint16_t ethertype = vxHtons(eth->ethertype);

	if (ethertype == ETHER_TYPE_IP) {
		struct ipv4_header* ip =
			(struct ipv4_header*) (rx.data
					       + sizeof(
						       struct ethernet_header));

		char ip_buf[16];
		vxInetNtoa(ip->src_ip, ip_buf);

		if (ip->protocol == ICMP_PROTOCOL) {
			handle_icmp(dev, ip, eth->src_mac);
		}

		if (ip->protocol == TCP_PROTOCOL) {
			handle_tcp(dev, ip, eth->src_mac);
		}
	}

	if (ethertype == ETHER_TYPE_ARP) {
		struct arp_packet* arp =
			(struct arp_packet*) (rx.data
					      + sizeof(struct ethernet_header));
		char ip_buf[16];
		vxInetNtoa(arp->sender_ip, ip_buf);
		LOG2_INFO("Socket", "arp packet from %s", ip_buf);

		auto target = arp->target_ip;
		if (vxInetAddr(MYIP) == target) {
			LOG2_INFO("ARP", "success targetting me");
			arp_reply(dev, arp->sender_ip, eth->src_mac);
		}
	}

	memcopy((void*) buffer, (void*) rx.data, n);

	// clear rx
	ioforge_clear_rx_queue(nic, &rx);
	return 1;
}

static int
socket_set_sockopt(socket_t* socket, uint32_t level, uint32_t optname,
		   const void* optval, uint32_t optlen) {
	UNUSED(optlen);

	switch (level) {
	case SOL_SOCKET: {
		switch (optname) {
		case SO_BINDTODEVICE: {
			auto netdev = lookup_netdev((char*) optval);
			socket->netdev = netdev;
			if (!netdev) {
				return SOCK_ERR_NODEV;
			}
			return SOCK_OK;
			break;
		}
		}
	}
	}
	return SOCK_ERR_NOTCONN;
}

static int socket_bind(socket_t* socket, sockaddr_in_t* addr, uint32_t len) {
	UNUSED(socket);
	UNUSED(addr);
	UNUSED(len);
	return 0;
}