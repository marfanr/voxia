#include "net/socket.h"
#include "init/init.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "memory/slab.h"

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

uint16_t vxHtons(uint16_t value) {
	return (value >> 8) | (value << 8);
}

inline uint16_t vxNtohs(uint16_t netshort) {
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

static int socket_receive(socket_t* socket, void* buffer, size_t size) {
	auto family = socket->family;
	auto type = socket->type;

	auto nic = socket->bound_nic;

	// queue kosong
	if (nic->pq_tail == nic->pq_head) {
		return -1; // lebih jelas daripada -1
	}

	struct pending_rx* rx = &nic->pending_queue[nic->pq_tail];

	// tentukan jumlah byte yang aman dicopy
	int n = (rx->len < size) ? rx->len : size;

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