#include "phys_base_allocator.h"
#include "libk/stivale2.h"
#include <hal/cpu/paging.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>

uint64_t higher_base_length_ = 0;
uint64_t usable_total        = 0;

extern void vma_setup_zone();
extern void paging_install();

uint8_t         *bitmap_base_       = 0;
memory_4k_block *phys_boot_metadata = 0;
uint64_t         metadata_count     = 0;
uint64_t         metadata_size      = 0;

void
phys_base_allocator_install(struct stivale2_struct_tag_memmap *memmap)
{

    struct stivale2_mmap_entry *smallest_entry = NULL;

    for (register uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];
        higher_base_length_ += entry->length;

        if (entry->type == STIVALE2_MMAP_USABLE)
        {
            usable_total += entry->length;
        }
    }

    metadata_count = higher_base_length_ / BLOCK_SIZE / 8;
    metadata_size  = ALIGN_UP(metadata_count * sizeof(uint8_t), BLOCK_SIZE);

    serial_trace("metadata  count %d with size %d kb\n", metadata_count, metadata_size / 1024);

    for (uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];

        if (entry->length >= metadata_size)
        {
            serial_trace("found suitale place for metadata 0x%x length %d kb \n", entry->base,
                         entry->length / 1024);

            // phys_boot_metadata = (memory_4k_block *)(entry->base);
            bitmap_base_ = (uint8_t *)(entry->base);
            entry->length -= metadata_size;
            entry->base += metadata_size;
            break;
        }
    }

    memset(bitmap_base_, 0xFF, metadata_size);

    for (uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];

        if (entry->type != STIVALE2_MMAP_USABLE)
            continue;

        uint64_t metadata_index = entry->base / 0x1000;
        uint64_t metadata_end   = entry->length / 0x1000;
        for (uint64_t j = metadata_index; j < (metadata_index + metadata_end); j++)
        {
            bitmap_base_[j / 8] &= ~(1 << (j % 8));
        }
    }

    // find first smallest entry
    for (uint64_t i = 0; i < metadata_count; i++)
    {
        if ((bitmap_base_[i / 8] & (1 << (i % 8))) == 0)
        {
            serial_trace("found smallest entry at index %d\n", i);
            break;
        }
    }

    // mark smallest entry sebagai reserved atau digunakan
    smallest_entry->type = STIVALE2_MMAP_RESERVED;
    serial_printf("smallest entry : 0x%x length %d\n", smallest_entry->base,
                  ALIGN_DOWN(smallest_entry->length, 0x1000) / (0x1000));

    serial_trace("memory block 4k sie %d\n", sizeof(memory_4k_block));

    serial_trace("usable memory size : %d mb\n", usable_total / 1024 / 1024);
    serial_trace("algned 4 block struct size %d \n", sizeof(memory_4k_block));

    for (uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];
        serial_send_string("base: 0x");
        serial_send_number((entry->base), 16);
        serial_send_string("  length: ");
        serial_send_number(entry->length / 1024, 10);
        serial_send_string("kb type : ");

        switch (entry->type)
        {
            case STIVALE2_MMAP_USABLE:
                serial_send_string("USABLE");
                break;
            case STIVALE2_MMAP_RESERVED:
                serial_send_string("RESERVED");
                break;
            case STIVALE2_MMAP_ACPI_RECLAIMABLE:
                serial_send_string("ACPI_RECLAIMABLE");
                break;
            case STIVALE2_MMAP_ACPI_NVS:
                serial_send_string("ACPI_NVS");
                break;
            case STIVALE2_MMAP_BAD_MEMORY:
                serial_send_string("BAD_MEMORY");
                break;
            case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE:
                serial_send_string("BOOTLOADER_RECLAIMABLE");
                break;
            case STIVALE2_MMAP_KERNEL_AND_MODULES:
                serial_send_string("KERNEL_AND_MODULES");
                break;
        }
        serial_trace(" block couts : %d", (entry->length) / (0x1000));

        serial_send_string("\n");
    }

    paging_install();
    vma_setup_zone();
}

void *
phys_base_alloc(uint64_t block)
{
    uint64_t start = 0;
    for (uint64_t i = 0; i < metadata_count; i++)
    {
        if ((bitmap_base_[i / 8] & (1 << (i % 8))) == 0)
        {
            start = i;
        }

        if (start && (i - start) == block)
        {
            // serial_trace("found free block at index %d count %d\n", start, block);
            /* serial_trace("i-start %d block %d\n", i - start, block); */
            for (uint64_t j = start; j < i; j++)
            {
                if (bitmap_base_[j / 8] & (1 << (j % 8)))
                {
                    start = 0;
                    for (uint64_t k = start; k < j; k++)
                    {
                        bitmap_base_[k / 8] &= ~(1 << (k % 8));
                    }
                    break;
                }
                bitmap_base_[j / 8] |= (1 << (j % 8));
            }
            // serial_trace("allocated memory at index %d count %d\n", start, block);
            return (void *)(start * BLOCK_SIZE);
        }
    }

    return 0;
}
void *
phys_base_alloc_on_top(uint64_t block)
{
    if (block == 0)
        return NULL;

    return NULL;
}

uint64_t
pys_base_get_free_block_count()
{
    uint64_t count = 0;
    // for (uint64_t i = 0; i < higher_base_length_ / BLOCK_SIZE; i++) {
    //     if (!(bitmap_base_[i / 8] & (1 << (i % 8)))) {
    //         count++;
    //     }
    // }
    return count;
}

void
phys_base_free(void *ptr, uint64_t size)
{
    uint64_t index = (uint64_t)ptr / BLOCK_SIZE;
    for (uint64_t i = index; i < index + size; i++)
    {
        bitmap_base_[i / 8] &= ~(1 << (i % 8));
    }
}

void
pmm_log_usage()
{
    uint64_t used = ((usable_total / BLOCK_SIZE) - pys_base_get_free_block_count());
    serial_trace("used memory : %d mb from %d mb\n", used * BLOCK_SIZE / 1024 / 1024,
                 usable_total / 1024 / 1024);
}
