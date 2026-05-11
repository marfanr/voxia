#ifndef __NET__UDP_H__
#define __NET__UDP_H__

#include <type.h>

typedef struct {
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
} __attribute__((packed)) udp_header_t;

#endif // __NET__UDP_H__