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
#include "init/init.h"
#include "libk/string.h"
#include "libk/type.h"
#include "procc/thread.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <libk/hash.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>

static cdev_ptr_t dev_chain = 0;
static struct slab_cache* block_device_cache = 0;

INIT(Dev) {
	vxCreateSlabCache(&block_device_cache, "block_device", sizeof(cdev_t),
	                  0, 0);
	// create directory for /dev
	{
		auto vnode = vxCreateAndAttachVnode();
		dentry_ptr entry = vxCreateDentry(str("dev"), vnode);
		entry->vnode = vnode;
		vnode->type = VNODE_TYPE_DIR;

		vxMakeDirectory(vxGetRootDirectory(), entry, 660);
		LOG_DEBUG("DEV", "entry name %s", entry->name->c_str);
	}
}

static int DevOpOpenImpl(void* vdata, int op_mode, thread_t* thread) {
	cdev_ptr_t cdev = (cdev_ptr_t)vdata;
	if (!cdev->ops)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	if (!cdev->ops->open)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	return ((cdev_ptr_t)vdata)->ops->open(cdev->ops->data, op_mode, thread);
}

static int DevOpCloseImpl(void* vdata) {
	cdev_ptr_t cdev = (cdev_ptr_t)vdata;
	if (!cdev->ops)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	if (!cdev->ops->close)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	return ((cdev_ptr_t)vdata)->ops->close(cdev->ops->data);
}

static int DevOpReadImpl(void* vdata, uintptr_t addr, void* buf, size_t count) {
	cdev_ptr_t cdev = (cdev_ptr_t)vdata;
	if (!cdev->ops)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	if (!cdev->ops->open)
		return ERR_DEV_OPS_NOT_IMPLEMENTED;

	return ((cdev_ptr_t)vdata)
	    ->ops->read(cdev->ops->data, addr, buf, count);
}

int vxMakeDev(cdev_operations_t* ops, uint16_t minor, uint32_t uuid,
              uint16_t permission, dev_name_t name) {
	auto cdev = (cdev_ptr_t)vxSlabAlloc(block_device_cache);
	if (!cdev)
		return -1;

	memset(cdev, 0, sizeof(cdev_t));
	strcpy(cdev->name, name);
	cdev->ops = ops;
	cdev->major = 2; // hardcoded for block device
	cdev->minor = minor;
	cdev->uuid = uuid;
	cdev->permission = permission;

	// inserting to dev chain
	{
		cdev->next = dev_chain;
		dev_chain = cdev;
	}

	auto vnode = vxCreateAndAttachVnode();
	if (!vnode)
		return -1;

	// create entry on /dev/{name}
	dentry_ptr new_entry;
	{
		dentry_ptr dev_entry = 0;
		if (vxResolveDentry("/dev", 0, &dev_entry, 0) != VFS_OK) {
			LOG_ERROR("dev", "dev entry missing");
			return -1;
		}

		size_t count = 0;
		for (size_t i = 0; i < dev_entry->children.size; i++) {
			if (strncmp(dev_entry->children.data[i]->name->c_str,
			            name, strlen(name))) {
				count++;
			}
		}

		string name_str;
		if (count > 0) {
			char temp[count / 10 + 1];
			auto inc = itoa(count, temp, 10);
			char tmp[strlen(name) + (count / 10 + 1)];
			strcpy(tmp, name);
			strcat(tmp, inc);
			name_str = str(tmp);
		} else {
			name_str = str(name);
		}

		// LOG_DEBUG("DEV", "dev dentry %s", dev_dentry->name->c_str);
		new_entry = vxCreateDentry(name_str, (vnode_ptr_t)vnode);
		vxMakeDirectory(dev_entry, new_entry, 440);
		LOG_DEBUG("DEV", "entry name %s", new_entry->name->c_str);
	}

	// create vnode
	{
		// vnode->type = VNODE_TYPE_BLK;
		// vnode->major = cdev->major;
		// vnode->minor = cdev->minor;

		// vnode->ops = (vops_blk_t*)kalloc(sizeof(vops_blk_t));
		// vnode->ops->v_data = (void*)cdev;
		// vnode->ops->open = DevOpOpenImpl;
		// vnode->ops->close = DevOpCloseImpl;
		// vnode->ops->read = DevOpReadImpl;
	}

	return DEV_OK;
}

cdev_ptr_t vxRetrieveDev(uint16_t major, uint16_t minor) {
	cdev_ptr_t curr = dev_chain;
	while (curr) {
		if (curr->major == major && curr->minor == minor)
			return curr;
		curr = curr->next;
	}
	return 0;
}