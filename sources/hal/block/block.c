#include "block.h"
#include <libk/hash.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/buddy_allocator.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

#define BLOCK_DEVICE_SIZE 32

static struct block_device **g__block_devices_ = 0;
struct buddy_allocator *block_allocator = 0;

void
block_install ()
{
    size_t estimated_size = (1024 * 1024 * 3);
    block_allocator = buddy_allocator_install (
        (void *)VIRT2PHYS (phys_base_alloc (1 + estimated_size / 4096)),
        (1 + estimated_size / 4096) * 4096);
    g__block_devices_ = (struct block_device **)buddy_alloc (
        block_allocator, sizeof (uintptr_t) * BLOCK_DEVICE_SIZE);
    memset (g__block_devices_, 0, sizeof (uintptr_t) * BLOCK_DEVICE_SIZE);
    serial_trace ("block device installed\n");
}

void
block_register_device (const char *name, block_device_operations_t *ops,
                       void *identifier)
{
    struct block_device *device = (struct block_device *)buddy_alloc (
        block_allocator, sizeof (struct block_device));
    device->name = name;
    device->ops = ops;
    device->identifier = identifier;
    device->used = false;
    int index = hash (name, BLOCK_DEVICE_SIZE);
    g__block_devices_[index] = device;
    serial_trace ("\n [bLOCK] block device registered %s\n", name);
}

struct block_device *
block_get_device (const char *name)
{
    int index = hash (name, BLOCK_DEVICE_SIZE);
    struct block_device *block = g__block_devices_[index];
    serial_trace ("block found\n");
    if (block != 0)
        return block;
    return 0;
}