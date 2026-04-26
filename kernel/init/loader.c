#include "libk/serial.h"
#include "libk/stivale2.h"
#include "memory/memory_utils.h"
#include <init/init.h>
#include <init/loader.h>
#include <libk/str.h>
#include <libk/type.h>
#include <memory/entry.h>

// ini adalah besar dari stack yang akan digunakan oleh kernel
static uint8_t stack[4096 * 32];

// static struct stivale2_tag l5_tag = {
//     .identifier = STIVALE2_HEADER_TAG_5LV_PAGING_ID, .next = 0};

static struct stivale2_header_tag_smp smp_hdr_tag = {
    .tag = {.identifier = STIVALE2_HEADER_TAG_SMP_ID, .next = 0}, .flags = 0};

static struct stivale2_header_tag_framebuffer framebuffer_hdr_tag = {
    .tag = {.identifier = STIVALE2_HEADER_TAG_FRAMEBUFFER_ID,
            .next = (uint64_t)&smp_hdr_tag},
    .framebuffer_width = 0,
    .framebuffer_height = 0,
    .framebuffer_bpp = 0};

__attribute__((section(".stivale2hdr"),
               used)) static struct stivale2_header stivale_hdr = {
    .entry_point = 0,
    .stack = (uintptr_t)stack + sizeof(stack),
    .flags = (1 << 1) | (1 << 2),
    .tags = (uint64_t)&framebuffer_hdr_tag};

void* stivale2_get_tag(struct stivale2_struct* stivale2_struct, uint64_t id) {
	struct stivale2_tag* current_tag =
	    (struct stivale2_tag*)(void*)stivale2_struct->tags;

	for (;;) {
		if (current_tag == 0)
			return 0;

		if (current_tag->identifier == id)
			return current_tag;

		current_tag = (struct stivale2_tag*)(void*)current_tag->next;
	}
}

int stivale2_mem_entry_type_converter(uint32_t type) {
	switch (type) {
	case STIVALE2_MMAP_USABLE:
		return ENTRY_MMAP_USABLE;
	case STIVALE2_MMAP_RESERVED:
		return ENTRY_MMAP_RESERVED;
	case STIVALE2_MMAP_ACPI_RECLAIMABLE:
		return ENTRY_MMAP_ACPI_RECLAIMABLE;
	case STIVALE2_MMAP_ACPI_NVS:
		return ENTRY_MMAP_ACPI_NVS;
	case STIVALE2_MMAP_BAD_MEMORY:
		return ENTRY_MMAP_BAD_MEMORY;
	case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE:
		return ENTRY_MMAP_BOOTLOADER_RECLAIMABLE;
	case STIVALE2_MMAP_KERNEL_AND_MODULES:
		return ENTRY_MMAP_KERNEL_AND_MODULES;
	case STIVALE2_MMAP_FRAMEBUFFER:
		return ENTRY_MMAP_FRAMEBUFFER;

	default:
		return -1;
	}
}

static void print_guid(struct stivale2_guid* g) {
	LOG_INFO("KERNEL",
	         "GUID %.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x", g->a,
	         g->b, g->c, g->d[0], g->d[1], g->d[2], g->d[3], g->d[4],
	         g->d[5], g->d[6], g->d[7]);
}

void build_context_from_stivale2(struct stivale2_struct* stivale2_struct,
                                 init_context_t* ctx) {
	{
		struct stivale2_struct_tag_modules* modules_info =
		    (struct stivale2_struct_tag_modules*)stivale2_get_tag(
		        stivale2_struct, STIVALE2_STRUCT_TAG_MODULES_ID);

		for (uint64_t i = 0; i < modules_info->module_count; i++) {
			struct stivale2_module* module =
			    &modules_info->modules[i];
			if (strncmp(module->string, "boot:///initrd.tar", 18) ==
			    0) {
				LOG_INFO("INIT", "initrd was found");
				LOG_INFO("INIT", "initrd size : %d",
				         module->end - module->begin);
				ctx->initrd_module.size =
				    module->end - module->begin;
				ctx->initrd_module.start = module->begin;
				break;
			}
		}
		LOG_INFO("INIT", "initrd module found at 0x%x",
		         ctx->initrd_module.start);
	}

	// initialize memori allocator
	{
		struct stivale2_struct_tag_memmap* memmap_info =
		    (struct stivale2_struct_tag_memmap*)stivale2_get_tag(
		        stivale2_struct, STIVALE2_STRUCT_TAG_MEMMAP_ID);

		ctx->memory.memory_entries = memmap_info->entries;
		serial_trace("memory entries : %d\n",
		             ctx->memory.memory_entries);

		for (uint64_t i = 0; i < memmap_info->entries; i++) {
			struct stivale2_mmap_entry* entry =
			    &memmap_info->memmap[i];
			ctx->memory.memory_map[i].base = entry->base;
			ctx->memory.memory_map[i].length = entry->length;
			ctx->memory.memory_map[i].type =
			    stivale2_mem_entry_type_converter(entry->type);
		}
	}

	{
		struct stivale2_struct_tag_rsdp* rsdp_info =
		    (struct stivale2_struct_tag_rsdp*)stivale2_get_tag(
		        stivale2_struct, STIVALE2_STRUCT_TAG_RSDP_ID);
		ctx->rsdp_addr = rsdp_info->rsdp;
	}

	// setup framebuffer
	{
		struct stivale2_struct_tag_framebuffer* framebuffer_info =
		    (struct stivale2_struct_tag_framebuffer*)stivale2_get_tag(
		        stivale2_struct, STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);

		if (!framebuffer_info ||
		    framebuffer_info->framebuffer_addr == 0) {
			LOG_WARN("loader", "no graphic detected");
		} else {

			ctx->framebuffer.framebuffer_addr =
			    framebuffer_info->framebuffer_addr;
			ctx->framebuffer.framebuffer_width =
			    framebuffer_info->framebuffer_width;
			ctx->framebuffer.framebuffer_height =
			    framebuffer_info->framebuffer_height;
			ctx->framebuffer.framebuffer_bpp =
			    framebuffer_info->framebuffer_bpp;
			ctx->framebuffer.framebuffer_pitch =
			    framebuffer_info->framebuffer_pitch;

			ctx->framebuffer.blue_mask_shift =
			    framebuffer_info->blue_mask_shift;
			ctx->framebuffer.blue_mask_size =
			    framebuffer_info->blue_mask_size;
			ctx->framebuffer.green_mask_shift =
			    framebuffer_info->green_mask_shift;
			ctx->framebuffer.green_mask_size =
			    framebuffer_info->green_mask_size;
			ctx->framebuffer.red_mask_shift =
			    framebuffer_info->red_mask_shift;
			ctx->framebuffer.red_mask_size =
			    framebuffer_info->red_mask_size;
		}
	}

	// kernel
	{
		struct stivale2_struct_tag_kernel_file_v2* kernel_info =
		    (struct stivale2_struct_tag_kernel_file_v2*)
		        stivale2_get_tag(stivale2_struct,
		                         STIVALE2_STRUCT_TAG_KERNEL_FILE_V2_ID);

		LOG_INFO("KERNEL", "kernel file addr 0x%x ",
		         kernel_info->kernel_file);
		LOG_INFO("KERNEL", "kernel file size %d Kb (%d Mb)",
		         kernel_info->kernel_size / 1024,
		         kernel_info->kernel_size / 1024 / 1024);
		ctx->kernel_raw_addr = VIRT2PHYS(kernel_info->kernel_file);
		ctx->kernel_raw_size = kernel_info->kernel_size;
	}

	// boot device
	{
		struct stivale2_struct_tag_boot_volume* boot_info =
		    (struct stivale2_struct_tag_boot_volume*)stivale2_get_tag(
		        stivale2_struct, STIVALE2_STRUCT_TAG_BOOT_VOLUME_ID);

		print_guid(&boot_info->guid);
		print_guid(&boot_info->part_guid);
	}
}

extern initcall_t __init_early_start[];
extern initcall_t __init_early_end[];

void run_all_init_calls(init_context_t* ctx) {
	LOG_INFO("INIT", "run all init");
	size_t i = 0;

	for (initcall_t* fn = __init_early_start; fn < __init_early_end;
	     ++fn, i++)
		(*fn)(ctx);

	LOG_INFO("INIT", "called %d init function", i);
}