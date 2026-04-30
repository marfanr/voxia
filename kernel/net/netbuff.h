#ifndef __NET__NETBUFF_H__
#define __NET__NETBUFF_H__

#include "ioforge/ioforge_nic.h"
#include <type.h>

#define NETBUFF_DATA_SIZE 2048
#define NETBUFF_MAX_HEADROOM 64

struct netbuff {
	uintptr_t paddr;

	uint8_t* head;
	uint8_t* end;

	uint8_t* data;
	uint8_t* tail;

	size_t length;
	struct ioforge_nic_service* nic;
};

struct netbuff* create_netbuff(struct ioforge_nic_service* nic);
void destroy_netbuff(struct netbuff* netbuff);
void* netbuff_put(struct netbuff* nb, size_t len);
void* netbuff_push(struct netbuff* nb, size_t len);
void free_netbuff(struct netbuff* netbuff);
#endif // __NET__NETBUFF_H__