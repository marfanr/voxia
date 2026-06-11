#include <init/init.h>
#include <init/loader.h>
#include <libk/limine.h>
#include <libk/serial.h>
#include <memory/entry.h>
#include <memory/memory_utils.h>
#include <str.h>
#include <type.h>

/* Limine Requests Markers */
__attribute__((section(".limine_requests_start"), used))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

/* Limine Requests */
__attribute__((section(".limine_requests"), used))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((section(".limine_requests"), used))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_executable_file_request kernel_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests"), used))
static volatile struct limine_executable_cmdline_request kernel_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0};

__attribute__((section(".limine_requests_end"), used))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static uint32_t limine_mem_entry_type_converter(uint64_t type) {
	switch (type) {
	case LIMINE_MEMMAP_USABLE:
		return ENTRY_MMAP_USABLE;
	case LIMINE_MEMMAP_RESERVED:
		return ENTRY_MMAP_RESERVED;
	case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
		return ENTRY_MMAP_ACPI_RECLAIMABLE;
	case LIMINE_MEMMAP_ACPI_NVS:
		return ENTRY_MMAP_ACPI_NVS;
	case LIMINE_MEMMAP_BAD_MEMORY:
		return ENTRY_MMAP_BAD_MEMORY;
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		return ENTRY_MMAP_BOOTLOADER_RECLAIMABLE;
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		return ENTRY_MMAP_KERNEL_AND_MODULES;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		return ENTRY_MMAP_FRAMEBUFFER;

	default:
		return (uint32_t)-1;
	}
}

void build_context_from_limine(init_context_t* ctx) {
	
	if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
		serial_trace("Limine base revision not supported\n");
		for (;;) __asm__ volatile("hlt");
	}

	if (framebuffer_request.response != NULL && framebuffer_request.response->framebuffer_count > 0) {
		struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
		if (fb != NULL) {
			ctx->framebuffer.framebuffer_addr = (uintptr_t)fb->address;
			ctx->framebuffer.framebuffer_width = (uint16_t)fb->width;
			ctx->framebuffer.framebuffer_height = (uint16_t)fb->height;
			ctx->framebuffer.framebuffer_bpp = fb->bpp;
			ctx->framebuffer.framebuffer_pitch = (uint16_t)fb->pitch;

			ctx->framebuffer.blue_mask_shift = fb->blue_mask_shift;
			ctx->framebuffer.blue_mask_size = fb->blue_mask_size;
			ctx->framebuffer.green_mask_shift = fb->green_mask_shift;
			ctx->framebuffer.green_mask_size = fb->green_mask_size;
			ctx->framebuffer.red_mask_shift = fb->red_mask_shift;
			ctx->framebuffer.red_mask_size = fb->red_mask_size;
		}
	} else {
		serial_trace("loader: no graphic detected\n");
	}

	if (kernel_address_request.response != NULL) {
		ctx->kernel_raw_addr = kernel_address_request.response->physical_base;
		ctx->kernel_virt_addr = kernel_address_request.response->virtual_base;
		serial2_printf("kernel addr at 0x%lx (0x%x)\n", ctx->kernel_virt_addr, ctx->kernel_raw_addr);
	}

	if (kernel_file_request.response != NULL && kernel_file_request.response->executable_file != NULL) {
		struct limine_file* kernel_file = kernel_file_request.response->executable_file;
		ctx->kernel_raw_size = kernel_file->size;
		// Fallback for address if kernel_address_request failed
		if (ctx->kernel_raw_addr == 0) {
			ctx->kernel_raw_addr = VIRT2PHYS((uintptr_t)kernel_file->address);
		}
		serial_trace("loader: kernel size %lu bytes\n", (unsigned long)kernel_file->size);
	}

	if (module_request.response != NULL) {
		struct limine_module_response* response = module_request.response;
		for (uint64_t i = 0; i < response->module_count; i++) {
			struct limine_file* module = response->modules[i];
			if (module != NULL && module->path != NULL) {
				serial2_printf("found module %s\n", module->path);
				if (strncmp(module->path, "/initrd.tar", 18) == 0) {
					ctx->initrd_module.size = module->size;
					ctx->initrd_module.start = (uintptr_t)module->address;
					serial_trace("loader: initrd found at %p, size %lu\n", 
					             (void*)ctx->initrd_module.start, (unsigned long)module->size);
					break;
				}
			}
		}
	}

	if (memmap_request.response != NULL) {
		struct limine_memmap_response* response = memmap_request.response;
		uint64_t entries = response->entry_count;
		ctx->memory.memory_entries = entries;

		if (entries > MAX_MEMORY_ENTRIES)
			entries = MAX_MEMORY_ENTRIES;

		for (uint64_t i = 0; i < entries; i++) {
			struct limine_memmap_entry* entry = response->entries[i];
			if (entry != NULL) {
				serial_trace("loader: memmap entry %x: base=%x, length=%x, type=%x\n", 
				             (unsigned long)i, (void*)entry->base, (unsigned long)entry->length, (unsigned long)entry->type);
				ctx->memory.memory_map[i].base = entry->base;
				ctx->memory.memory_map[i].length = entry->length;
				ctx->memory.memory_map[i].type = limine_mem_entry_type_converter(entry->type);
			}
		}
	} else {
		serial_trace("FATAL: no memmap response\n");
		for (;;) __asm__ volatile("hlt");
	}

	if (rsdp_request.response != NULL) {
		ctx->rsdp_addr = (uintptr_t)rsdp_request.response->address;
	}

	if (hhdm_request.response != NULL) {
		ctx->hhdm_offset = hhdm_request.response->offset;
		serial2_printf("hhdm offset 0x%lx\n", ctx->hhdm_offset);
	}

	serial2_printf("cmdline: %s\n", kernel_cmdline_request.response->cmdline);
	// TODO: parse cmdline debug, no-splash
}

extern initcall_t __init_early_start[];
extern initcall_t __init_early_end[];

void run_all_init_calls(init_context_t* ctx) {
	serial_trace("INIT: run all init\n");
	size_t i = 0;

	for (initcall_t* fn = __init_early_start; fn < __init_early_end;
	     ++fn, i++)
		(*fn)(ctx);

	serial_trace("INIT: called %lu init functions\n", (unsigned long)i);
}
