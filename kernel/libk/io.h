#ifndef __LIBK_IO_H__
#define __LIBK_IO_H__

#include <type.h>

static inline void outb(uint16_t port, uint8_t value) {
	asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// receives data from a IO port
static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));

	return ret;
}

static inline void outl(uint16_t port, uint32_t value) {
	asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
	uint32_t ret;
	asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));

	return ret;
}

static inline uint16_t inw(uint16_t port) {
	uint16_t ret;
	asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));

	return ret;
}

static inline void outw(uint16_t port, uint16_t value) {
	asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void mmio_outw(uint32_t addr, uint16_t value) {
	asm volatile("outw %0, %1" : : "a"(value), "Nd"(addr));
}

static inline uint16_t mmio_inw(uint32_t addr) {
	uint16_t ret;
	asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(addr));

	return ret;
}

static inline void mmio_outl(uintptr_t addr, uint32_t value) {
	*((volatile uint32_t*) addr) = value;
}

static inline uint32_t mmio_inl(uintptr_t addr) {
	return *((volatile uint32_t*) addr);
}

static inline void mmio_outll(uintptr_t addr, uint64_t value) {
	*((volatile uint64_t*) addr) = value;
}

static inline uint64_t mmio_inll(uintptr_t addr) {
	return *((volatile uint64_t*) addr);
}

#endif