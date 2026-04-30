#include "netbuff.h"
#include "init/init.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "memory/slab.h"
#include <libk/str.h>

#define NETBUFF_MAX_TEMP 32

static struct slab_cache* netbuff_cache = 0;

static struct netbuff* netbuff_temp[NETBUFF_MAX_TEMP] = {0};
static size_t netbuff_temp_count = 0;

INIT(Netbuff) {
	vxCreateSlabCache(&netbuff_cache, "netbuff", sizeof(struct netbuff), 0,
			  0);
}

struct netbuff* create_netbuff(struct ioforge_nic_service* nic) {
	struct netbuff* netbuff = 0;
	if (!netbuff_temp_count) {
		netbuff = (struct netbuff*) vxSlabAlloc(netbuff_cache);
		auto rawbuffer = (uint8_t*) ioforge_dma_alloc(NETBUFF_DATA_SIZE,
							      &netbuff->paddr);
		memset((void*) rawbuffer, 0, NETBUFF_DATA_SIZE);
		netbuff->head = rawbuffer;
		netbuff->end = rawbuffer + NETBUFF_DATA_SIZE;
	} else {
		netbuff = netbuff_temp[netbuff_temp_count - 1];
		netbuff_temp[netbuff_temp_count - 1] = 0;
		netbuff_temp_count--;
	}

	netbuff->data = netbuff->head + NETBUFF_MAX_HEADROOM;
	netbuff->tail = netbuff->data;
	netbuff->length = 0;
	netbuff->nic = nic;

	return netbuff;
}

// Dipakai oleh Aplikasi/Layer atas untuk menambahkan payload ke belakang
void* netbuff_put(struct netbuff* nb, size_t len) {
	void* current_tail = (void*) nb->tail;
	nb->tail += len;
	nb->length += len;
	return current_tail;
}

// Dipakai oleh Layer Network (TCP/IP/Eth) untuk menyisipkan header ke depan
void* netbuff_push(struct netbuff* nb, size_t len) {
	nb->data -= len; // Mundurkan pointer data ke area headroom
	nb->length += len;
	return (void*) nb->data;
}

void free_netbuff(struct netbuff* netbuff) {
	if (netbuff_temp_count >= NETBUFF_MAX_TEMP) {
		ioforge_dma_free((void*) netbuff->paddr, (void*) netbuff->head,
				 NETBUFF_DATA_SIZE);
		slab_free(netbuff_cache, netbuff);
	} else {
		netbuff_temp_count++;
		netbuff_temp[netbuff_temp_count - 1] = netbuff;
	}
}

// void netbuff_insert(struct netbuff* netbuff,)