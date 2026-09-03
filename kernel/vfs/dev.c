#include "vfs/dev.h"
#include <hash.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <str.h>
#include <type.h>

static cdev_ptr_t dev_chain = 0;
static struct slab_cache* block_device_cache = 0;

struct minor_map {
	uint8_t minor_bitmap[DEV_MINOR_BITMAP_COUNT];
};

static struct minor_map major_map[DEV_MAJOR_MAX_COUNT] = {0};

static int32_t alloc_minor(uint32_t major) {
	auto bitmap = major_map[major].minor_bitmap;
	for (int32_t b = 0; b < DEV_MINOR_BITMAP_COUNT; b++) {
		if (bitmap[b] == 0xFF)
			continue;

		for (int32_t i = 0; i < 8; i++) {
			if (bitmap[b] & (1 << i))
				continue;

			bitmap[b] |= (1 << i);
			return b * 8 + i;
		}
	}
	return -1;
}

// static void free_minor(uint32_t major, uint32_t minor) {
// 	uint8_t* bitmap = major_map[major].minor_bitmap;
// 	bitmap[minor / 8] &= ~(1 << (minor % 8));
// }


KERNEL_API cdev_ptr_t create_dev(void* ops, uint32_t major) {
	if (!block_device_cache)
		vxCreateSlabCache(&block_device_cache, "block_device",
		                  sizeof(cdev_t), 0, 0);

	auto cdev = (cdev_ptr_t)vxSlabAlloc(block_device_cache);
	if (!cdev)
		return 0;

	cdev->major = major;
	auto minor = alloc_minor(major);
	if (minor < 0) {
		slab_free(block_device_cache, cdev);
		return 0;
	}
	cdev->minor = (uint32_t)minor;
	cdev->ops = ops;
	cdev->next = NULL;

	auto curr = &dev_chain;
	while (*curr)
		curr = &(*curr)->next;
	*curr = cdev;

	return cdev;
}

cdev_ptr_t KERNEL_API retrieve_dev(uint32_t major, uint32_t minor) {
	cdev_ptr_t curr = dev_chain;
	while (curr) {
		if (curr->major == major && curr->minor == minor)
			return curr;
		curr = curr->next;
	}
	return 0;
}