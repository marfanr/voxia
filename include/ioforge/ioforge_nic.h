#ifndef __SYS__IOFORGE__IOFORGE_NIC_H__
#define __SYS__IOFORGE__IOFORGE_NIC_H__

#include "ioforge/ioforge.h"
#include "type.h"

#define MAX_NICS 8    /* maksimum NIC yang bisa didaftarkan */
#define NIC_MAC_LEN 6 /* panjang MAC address (bytes)        */
#define PENDING_QUEUE_SIZE 128
#define PENDING_QUEUE_MASK (PENDING_QUEUE_SIZE - 1)
#define BUFFER_POOL_SIZE 1280
#define BUFFER_POOL_MASK (BUFFER_POOL_SIZE - 1)

// untuk scatter gather pattern
struct data_template {
	const void* buffer;
	size_t len;
	uint8_t wait_next_data;
};

struct ioforge_nic_operation {
	int (*send)(const struct data_template data[], size_t count);
	int (*receive)(void** buffer, size_t* size); // buat pooling
	int (*get_mac_address)(uint8_t mac[NIC_MAC_LEN]);
	void (*storeBufferToPool)(int rx_id, void* vaddr);
};

typedef enum {
	Ready = 2,
	Bussy = 1,
	Unready = 0,
	Halted = -2,
} IoforgeNICStatus;

#ifdef __cplusplus
extern "C" {
#endif

struct pending_rx {
	int rx_id;
	uint8_t* data;
	size_t len;
};

struct rx_buffer {
	void* vaddr;
	uintptr_t paddr;
};

struct rx_buffer_pool {
	struct rx_buffer buffers[BUFFER_POOL_SIZE];
	//  posisi terdepan buffer yang digunakan
	uint32_t head;
	// posisi terbelakang buffer yang sudah selesai digunakan
	uint32_t tail;
};

struct ioforge_nic_service {
	struct ioforge_device service;
	struct ioforge_nic_operation ops;
	IoforgeNICStatus status;
	struct pending_rx pending_queue[PENDING_QUEUE_SIZE];
	uint32_t pq_head;
	uint32_t pq_tail;
};

void ioforge_register_nic(struct ioforge_nic_service* nic);
void ioforge_nic_rx(struct ioforge_nic_service* nic, uint8_t* buffer,
		    size_t len, int rx_id);
struct ioforge_nic_service* IOforgeNICFindByName(char* name);

int ioforge_receive_pending_queue(struct ioforge_nic_service* nic,
				  struct pending_rx* rx);

// jangan lupa clear rx setelah di consume
void ioforge_clear_rx_queue(struct ioforge_nic_service* nic,
			    struct pending_rx* rx);

#ifdef __cplusplus
}
#endif
#endif // __SYS__IOFORGE__IOFORGE_NIC_H__