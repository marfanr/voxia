#ifndef __NET__ICMP_H__
#define __NET__ICMP_H__

#include <type.h>

struct icmp_header {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
} __attribute__((packed));

struct icmp_echo {
	uint8_t type; // 8 = request, 0 = reply
	uint8_t code; // selalu 0 untuk echo
	uint16_t checksum;

	uint16_t identifier;
	uint16_t sequence;

	uint8_t data[]; // payload (opsional, variabel)
} __attribute__((packed));

#endif // __NET__ICMP_H__