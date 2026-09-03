#include "socket.h"
#include "init/init.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include <str.h>
#include "memory/slab.h"
#include "memory/kalloc.h"
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

/* helper: get inet_socket from base socket_t* (safe because base is first member) */
#define INET_SOCK(s) ((struct inet_socket*)(s))

static inet_socket_ops_t* socket_ops = 0;

static int socket_receive(socket_t* socket, void* buffer, size_t size);
static int
socket_set_sockopt(socket_t* socket, uint32_t level, uint32_t optname,
		   const void* optval, uint32_t optlen);
static int socket_bind(socket_t* socket, sockaddr_in_t* addr, uint32_t len);

INIT(Socket) {
	socket_ops = (inet_socket_ops_t*)kalloc(sizeof(inet_socket_ops_t));
	socket_ops->recv = socket_receive;
	socket_ops->set_sockopt = socket_set_sockopt;
	socket_ops->bind = socket_bind;
}

int create_socket(sock_family_t family, sock_type_t type, uint16_t protocol,
		  socket_t** out_socket) {
	socket_t* sock;
	size_t alloc_size;

	switch (family) {
	case AF_INET:
		alloc_size = sizeof(struct inet_socket);
		break;
	case AF_UNIX:
		alloc_size = sizeof(struct unix_socket);
		break;
	default:
		return -EAFNOSUPPORT;
	}

	auto s = kalloc(alloc_size);
	memset(s, 0, alloc_size);

	// sock points to the base (first field) of the allocated struct
	sock = (socket_t*)s;
	sock->family = family;
	sock->type = type;
	sock->protocol = protocol;
	sock->state = SOCK_STATE_CLOSED;

	if (family == AF_INET) {
		sock->ops = socket_ops;
	} else {
		sock->ops = NULL; /* UNIX ops not yet implemented */
	}

	if (!socket_ops)
		LOG2_WARN("Socket", "socket ops not initialized");

	if (out_socket)
		*out_socket = sock;

	return 0;
}

// checksum

// hardcode
#define MYIP "192.168.100.80"

static int socket_receive(socket_t* socket, void* buffer, size_t size) {
	auto dev = INET_SOCK(socket)->netdev;
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
			auto netdev = lookup_netdev((char*)optval);
			INET_SOCK(socket)->netdev = netdev;
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