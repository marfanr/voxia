#include "phys_base_allocator.h"
#include "libk/stivale2.h"
#include <hal/cpu/paging.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/buddy.h>
#include <memory/memory_utils.h>

uint64_t higher_base_length_ = 0;
uint64_t usable_total        = 0;

extern void vma_setup_zone();
extern void paging_install();

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

    metadata_count = higher_base_length_ / 0x1000;
    metadata_size  = ALIGN_UP(metadata_count * sizeof(memory_4k_block), 0x1000);

    serial_trace("metadata  count %d with size %d kb\n", metadata_count, metadata_size / 1024);

    for (uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];

        if (entry->length >= metadata_size)
        {
            serial_trace("found suitale place for metadata 0x%x length %d kb \n", entry->base,
                         entry->length / 1024);

            phys_boot_metadata = (memory_4k_block *)(entry->base);
            entry->length -= metadata_size;
            entry->base += metadata_size;
            break;
        }
    }

    for (uint64_t i = 0; i < metadata_count; i++)
    {
        phys_boot_metadata[i].address = i * 0x1000;
        if ((i + 1) != metadata_count)
        {
            phys_boot_metadata[i].next = &phys_boot_metadata[i + 1];
        }
        else
        {
            phys_boot_metadata[i].next = &phys_boot_metadata[0];
        }

        if (i > 0)
        {
            phys_boot_metadata[i].prev = &phys_boot_metadata[i - 1];
        }
        else
        {
            phys_boot_metadata[i].prev = &phys_boot_metadata[metadata_count - 1];
        }
        phys_boot_metadata[i].used = 1;
    }

    for (uint64_t i = 0; i < memmap->entries; i++)
    {
        struct stivale2_mmap_entry *entry = &memmap->memmap[i];

        if (entry->type != STIVALE2_MMAP_USABLE)
            continue;

        uint64_t metadata_index = entry->base / 0x1000;
        uint64_t metadata_end   = entry->length / 0x1000;
        for (uint64_t j = metadata_index; j < (metadata_index + metadata_end); j++)
        {
            phys_boot_metadata[j].used = 0;
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

    for (uint64_t i = 0; i < metadata_count; i++)
    {
        if (phys_boot_metadata[i].used == 0)
        {
            serial_trace("first free block at index %d : 0x%x\n", i, phys_boot_metadata[i].address);
            break;
        }
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
        if (phys_boot_metadata[i].used == 0 && !start)
        {
            start = i;
        }

        if (start && (i - start) == block)
        {
            /* serial_trace("i-start %d block %d\n", i - start, block); */
            for (uint64_t j = start; j < i; j++)
            {
                if (phys_boot_metadata[j].used)
                {
                    start = 0;
                }
                phys_boot_metadata[j].used = 1;
            }
            return (void *)phys_boot_metadata[start].address;
        }
    }

    return 0;
}
void *
phys_base_alloc_on_top(uint64_t block)
{
    if (block == 0)
        return NULL;
    //
    // uint64_t total_blocks = higher_base_length_ / BLOCK_SIZE;
    //
    // // Start from the highest block and go down
    // for (int64_t i = total_blocks - 1; i >= 0; i--) {
    //     // Skip if this block is already used
    //     if (bitmap_base_[i / 8] & (1 << (i % 8)))
    //         continue;
    //
    //     // Check if we have enough free blocks below this one
    //     boolean_t all_free = 1;
    //     for (uint64_t j = 1; j < block; j++) {
    //         // Make sure we don't go below block 0
    //         if (i < j || bitmap_base_[(i - j) / 8] & (1 << ((i - j) % 8))) {
    //             all_free = 0;
    //             break;
    //         }
    //     }
    //
    //     if (all_free) {
    //         // Mark all blocks in the range as used
    //         for (uint64_t j = 0; j < block; j++) {
    //             bitmap_base_[(i - j) / 8] |= (1 << ((i - j) % 8));
    //         }
    //
    //         // Return the address corresponding to the lowest block in
    //         // the range
    //         return (void *)PHYS2VIRT(
    //             (uint64_t)((i - block + 1) * BLOCK_SIZE));
    //     }
    // }
    //
    // Could not find enough consecutive free blocks
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
    uint64_t index = VIRT2PHYS((uint64_t)ptr) / BLOCK_SIZE;
    // for (uint64_t i = 0; i < size; i++)
    //     bitmap_base_[(index + i) / 8] &= ~(1 << ((index + i) % 8));
}

void
pmm_log_usage()
{
    uint64_t used = ((usable_total / BLOCK_SIZE) - pys_base_get_free_block_count());
    serial_trace("used memory : %d mb from %d mb\n", used * BLOCK_SIZE / 1024 / 1024,
                 usable_total / 1024 / 1024);
}
