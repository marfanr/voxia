#include "net/socket.h"
#include "init/init.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include "memory/slab.h"

static struct slab_cache* socket_cache = 0;
static socket_ops_t* socket_ops = 0;

static int socket_receive(socket_t* socket, void** buffer, size_t* size);
// static void socket_bind(socket_t* socket, struct ioforge_nic_service* nic,
// 			uint16_t port);

INIT(Socket) {
	socket_ops = (socket_ops_t*) kalloc(sizeof(socket_ops_t));
	socket_ops->recv = socket_receive;
}

void vxSocket(sock_family_t family, sock_type_t type, uint16_t protocol,
	      socket_t* socket) {
	// kalau belum ada cache buat dulu
	if (!socket_cache)
		vxCreateSlabCache(&socket_cache, "socket", sizeof(socket_t), 0,
				  0);

	socket = (socket_t*) vxSlabAlloc(socket_cache);
	socket->family = family;
	socket->type = type;
	socket->protocol = protocol;

	if (!socket_ops)
		LOG2_WARN("Socket", "socket ops not initialized");

	socket->ops = socket_ops;
}

static int socket_receive(socket_t* socket, void** buffer, size_t* size) {
	auto family = socket->family;
	auto type = socket->type;
}