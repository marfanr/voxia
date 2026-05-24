// Copyright (c) 2025 Mohammad Arfan

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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


// TODO: ganti ops nya dengan void *
KERNEL_API cdev_ptr_t create_dev(struct vops_blk* ops, uint32_t major) {
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