#ifndef __MEMORY__ENTRY_H__
#define __MEMORY__ENTRY_H__

#include <type.h>

const enum a
    : int { ENTRY_MMAP_USABLE = 1,
	    ENTRY_MMAP_RESERVED = 2,
	    ENTRY_MMAP_ACPI_RECLAIMABLE = 3,
	    ENTRY_MMAP_ACPI_NVS = 4,
	    ENTRY_MMAP_BAD_MEMORY = 5,
	    ENTRY_MMAP_BOOTLOADER_RECLAIMABLE = 6,
	    ENTRY_MMAP_KERNEL_AND_MODULES = 7,
	    ENTRY_MMAP_FRAMEBUFFER = 8 };

typedef struct {
	uint64_t base;
	uint64_t length;
	uint32_t type;
} __attribute__((aligned(32))) memory_entry_t;

#endif // __MEMORY__ENTRY_H__