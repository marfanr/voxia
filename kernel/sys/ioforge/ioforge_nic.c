#include "ioforge/ioforge_nic.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"
#include <type.h>
#include "memory/memory_utils.h"
#include <str.h>

KERNEL_API void ioforge_register_nic(struct ioforge_nic_service* nic) {
	struct ioforge_device* svc = &nic->service;
	svc->type = IOFORGE_NIC;

	LOG2_DEBUG("NIC", "registered NIC at 0x%x (%s)", nic, svc->name);
	ioforge_attach(ioforge_get_root(), (struct ioforge_device*) nic);
}

// KERNEL_API
// struct ioforge_nic_service* IOforgeNICFindByName(char* name) {
// 	struct ioforge_device* tmp = ioforge_get_root();
// 	while (tmp != 0) {
// 		if (tmp->type == IOFORGE_NIC) {
// 			struct ioforge_nic_service* tmp_nic =
// 				(struct ioforge_nic_service*) tmp;
// 			if (strncmp(tmp_nic->service.name, name, strlen(name))
// 			    == 0) {
// 				serial2_printf("found service type %d (%s)\n",
// 					       tmp_nic->service.type,
// 					       tmp_nic->service.name);
// 				return tmp_nic;
// 			}
// 		}
// 		tmp = tmp->next;
// 	}
// 	return 0;
// }

// untuk soft handling
KERNEL_API
void ioforge_nic_rx(struct ioforge_nic_service* nic, uint8_t* buffer,
		    size_t len, int rx_id) {
	if (!nic || !buffer || len == 0)
		return;

	uint32_t head = nic->pq_head;
	uint32_t next = (head + 1) & PENDING_QUEUE_MASK;
	if (next != nic->pq_tail) {
		struct pending_rx* rx = &nic->pending_queue[head];
		rx->data = buffer;
		rx->len = len;
		rx->rx_id = rx_id;

		__atomic_store_n(&nic->pq_head, next, __ATOMIC_RELEASE);
	}
}

int ioforge_receive_pending_queue(struct ioforge_nic_service* nic,
				  struct pending_rx* rx) {
	if (nic->pq_tail == nic->pq_head) {
		return -1;
	}

	auto old = &nic->pending_queue[nic->pq_tail];
	rx->data = old->data;
	rx->len = old->len;
	rx->rx_id = old->rx_id;

	old->data = 0;
	old->len = 0;
	old->rx_id = 0;

	nic->pq_tail = (nic->pq_tail + 1) % PENDING_QUEUE_SIZE;
	return 1;
}

void ioforge_clear_rx_queue(struct ioforge_nic_service* nic,
			    struct pending_rx* rx) {
	nic->ops.storeBufferToPool(rx->rx_id, rx->data);
	rx->data = 0;
	rx->len = 0;
	rx->rx_id = 0;
}
