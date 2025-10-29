#include "block.h"
#include "init/init.h"
#include <libk/hash.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>

static struct block_device *g__block_devices_root_ = 0;
struct slab_cache          *block_device_cache     = 0;

INIT(block)
{
    slab_cache_create(&block_device_cache, "block_device", sizeof(struct block_device), 0, 0);
}

void
block_register_device(const char *name, block_device_operations_t *ops, void *identifier)
{
    struct block_device *device = (struct block_device *)slab_alloc(block_device_cache);
    strcpy((char *)device->name, name);
    device->ops            = ops;
    device->identifier     = identifier;
    device->bar            = 0;
    device->used           = 0;
    device->next           = g__block_devices_root_;
    g__block_devices_root_ = device;
    LOG_INFO("BLOCK", "[bLOCK] block device registered %s", name);
}

block_device *
block_get_device(const char *name)
{
    block_device *device = g__block_devices_root_;
    while (device)
    {
        if (strncmp(device->name, name, 64) == 0)
        {
            return device;
        }
        device = device->next;
    }
    return 0;
}