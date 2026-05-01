#ifndef __NET__NETUTILS_H__
#define __NET__NETUTILS_H__

#include <type.h>

static inline uint16_t vxHtons(uint16_t value) {
	return (value << 8) | (value >> 8);
}

static inline uint16_t vxNtohs(uint16_t netshort) {
	return (netshort >> 8) | (netshort << 8);
}

uint32_t vxInetAddr(const char* addr);

uint16_t checksum16_adc(const uint16_t* data, size_t length);

#endif // __NET__NETUTILS_H__