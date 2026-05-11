#ifndef __INIT__INIT_H__
#define __INIT__INIT_H__

#include "hal/graphic/framebuffer.h"
#include "init/initrd.h"
#include "memory/entry.h"

#define MAX_MEMORY_ENTRIES 256

typedef struct {
	uint32_t memory_entries;
	memory_entry_t memory_map[MAX_MEMORY_ENTRIES];
} memory_context_t;

typedef struct {
	initrd_module_t initrd_module;
	memory_context_t memory;
	uintptr_t rsdp_addr;
	framebuffer_t framebuffer;
	uintptr_t kernel_raw_addr;
	size_t kernel_raw_size;
} init_context_t;

typedef void (*initcall_t)(init_context_t* ctx);

#define INIT(fn)                                                               \
	void init##fn(init_context_t* ctx);                                    \
	static initcall_t __init_##fn                                          \
		__attribute__((used, section(".init_early." #fn))) = init##fn; \
	void init##fn(init_context_t* ctx)

#define INFLOOP                                                                \
	for (;;)                                                               \
		__asm__ volatile("hlt");

#endif // __INIT__INIT_H