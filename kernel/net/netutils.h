#ifndef __NET__NETUTILS_H__
#define __NET__NETUTILS_H__

#include <type.h>

static inline uint16_t vxHtons(uint16_t value) {
	return (uint16_t) ((value << 8) | (value >> 8));
}

static inline uint16_t vxNtohs(uint16_t netshort) {
	return (uint16_t) ((netshort >> 8) | (netshort << 8));
}

// full 4-byte swap
static inline uint32_t vxHtonl(uint32_t value) {
	return ((value & 0xFF000000) >> 24) | ((value & 0x00FF0000) >> 8)
	       | ((value & 0x0000FF00) << 8) | ((value & 0x000000FF) << 24);
}

static inline uint32_t vxNtohl(uint32_t netlong) {
	return ((netlong & 0xFF000000) >> 24) | ((netlong & 0x00FF0000) >> 8)
	       | ((netlong & 0x0000FF00) << 8) | ((netlong & 0x000000FF) << 24);
}

uint32_t vxInetAddr(const char* addr);

uint16_t checksum16_adc(const uint16_t* data, size_t length);
uint32_t checksum16_raw(const uint16_t* data, size_t length);
char* vxInetNtoa(uint32_t ip, char* buffer);

#endif // __NET__NETUTILS_H__