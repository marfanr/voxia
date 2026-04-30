#ifndef __NET__TCP_H__
#define __NET__TCP_H__

#include <type.h>

struct tcp_header {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;

	uint8_t data_offset_reserved;
	uint8_t flags;

	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_ptr;
} __attribute__((packed));

#endif // __NET__TCP_H__