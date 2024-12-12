#include "phys_base_allocator.h"
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <libk/str/memset.h>
#include <memory/memory_utils.h>

uint64_t higher_base_length_;
uint64_t bitmap_size_;
uint8_t *bitmap_base_;

void phys_base_allocator_install(struct stivale2_struct_tag_memmap *memmap) {
  for (uint64_t i = 0; i < memmap->entries; i++) {
    struct stivale2_mmap_entry *entry = &memmap->memmap[i];
    higher_base_length_ += entry->length;
  }
  bitmap_size_ =
      ALIGN_UP(ALIGN_DOWN(higher_base_length_, 0x1000) / 0x1000 / 8, 0x1000);
  KDEBUG(DEBUG_LEVEL_INFO, "bitmap size: %dkb", bitmap_size_ / 1024);

  // find base to host bitmap
  for (uint64_t i = 0; i < memmap->entries; i++) {
    struct stivale2_mmap_entry *entry = &memmap->memmap[i];
    if (entry->type != STIVALE2_MMAP_USABLE)
      continue;
    if (entry->length >= bitmap_size_) {
      bitmap_base_ = (uint8_t *)(PHYS2VIRT(entry->base));
      entry->base += bitmap_size_;
      entry->length -= bitmap_size_;
      serial_send_string("bitmap base : 0x");
      serial_send_number((uint64_t)bitmap_base_, 16);
      serial_send_string("  bitmap size : ");
      serial_send_number(bitmap_size_ / 1024, 10);
      serial_send_string("kb\n");
      break;
    }
  }
  //   setup bitmap
  KDEBUG(DEBUG_LEVEL_INFO, "bitmap physical memory allocator hosted at: 0x%x",
         bitmap_base_);
  memset((void *)bitmap_base_, 0xFF, bitmap_size_);
  for (uint64_t i = 0; i < memmap->entries; i++) {
    struct stivale2_mmap_entry *entry = &memmap->memmap[i];
    if (entry->type != STIVALE2_MMAP_USABLE)
      continue;

    phys_base_free((void *)entry->base, entry->length / 0x1000);
  }

  // logger
  for (uint64_t i = 0; i < memmap->entries; i++) {
    struct stivale2_mmap_entry *entry = &memmap->memmap[i];
    if (!(entry->type == STIVALE2_MMAP_USABLE ||
          entry->type == STIVALE2_MMAP_KERNEL_AND_MODULES ||
          entry->type == STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE ||
          entry->type == STIVALE2_MMAP_ACPI_RECLAIMABLE ||
          entry->type == STIVALE2_MMAP_ACPI_NVS ||
          entry->type == STIVALE2_MMAP_BAD_MEMORY))
      continue;
    serial_send_string("base: 0x");
    serial_send_number(entry->base, 16);
    serial_send_string("  length: ");
    serial_send_number(entry->length / 1024, 10);
    serial_send_string("kb type : ");
    switch (entry->type) {
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

    serial_send_string("\n");
  }
  bitmap_base_[0 / 8] |= 1 << (0 % 8);
}

void *phys_base_alloc(uint64_t block) {
  uint64_t start = 0;
  for (uint64_t i = 0; i < higher_base_length_ / 0x1000; i++) {
    if (bitmap_base_[i / 8] & (1 << (i % 8))) {
      start = i + 1;
      continue;
    }
    if (i - start + 1 == block) {
      for (uint64_t j = start; j <= i; j++)
        bitmap_base_[j / 8] |= 1 << (j % 8);
      return (void *)PHYS2VIRT((uint64_t)(start * 0x1000));
    }
  }
}

// pmm align
// void *pmm_alloc_align(size_t block, size_t align) {
//   uint64_t start = 0;
//   for (uint64_t i = 0; i < higher_base_length_ / 0x1000; i++) {
//     if (bitmap_base_[i / 8] & (1 << (i % 8))) {
//       start = i + 1;
//       continue;
//     }
//     if (i - start + 1 == block) {
//       if ((start * 0x1000) % align == 0) {
//         for (uint64_t j = start; j <= i; j++)
//           bitmap_base_[j / 8] |= 1 << (j % 8);
//         return (void *)PHYS2VIRT((uint64_t)(start * 0x1000));
//       }
//     }
//   }
// }

void phys_base_free(void *ptr, uint64_t size) {
  uint64_t index = ((uint64_t)ptr) / 0x1000;
  for (uint64_t i = 0; i < size; i++)
    bitmap_base_[(index + i) / 8] &= ~(1 << ((index + i) % 8));
}

void pmm_log_usage() {
  uint64_t used = 0;
  for (uint64_t i = 0; i < higher_base_length_ / 0x1000; i++) {
    if (bitmap_base_[i / 8] & (1 << (i % 8)))
      used++;
  }
  KDEBUG(DEBUG_LEVEL_INFO, "used memory : %d Alloc \n", used);
}