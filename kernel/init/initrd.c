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

#include "initrd.h"
#include "hal/cpu/paging.h"
#include "hal/cpu/spinlock.h"
#include "init/init.h"
#include "libk/oct2bin.h"
#include "libk/vector.h"

#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include <vfs/vfs.h>
#include <vfs/vnode.h>

#include <libk/fs/tar.h>
#include <libk/serial.h>
#include <libk/str.h>

#include <memory/kalloc.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>

#include <sys/descriptor.h>

struct initrd_internal_data {
	uint64_t raw_addr;
	uintptr_t virt_addr;
	size_t size;
	spinlock_t lock;
};

struct initrd_internal_vnode_data {
	uint64_t offset;
};
struct slab_cache* initrd_internal_vnode_data_cache = 0;

// local use
static struct initrd_internal_data __initrd_data;

static void LoadIntoVfs(dentry_ptr dentry);
filesystem_t* initrd_fs_impl();

INIT(initrd) {
	initrd_module_t* initrd_module = &ctx->initrd_module;
	uint64_t paddr = VIRT2PHYS(initrd_module->start);
	uint64_t paddr_alligned = (uint64_t)(paddr & ~(BLOCK_SIZE - 1));
	uint64_t offset = paddr - paddr_alligned;
	uint64_t page_count =
	    (initrd_module->size + offset + BLOCK_SIZE - 1) / BLOCK_SIZE;

	vxMultipleMmap(paging_get_highest_page_map(), 0xFFFFE00000000000,
	               paddr_alligned, page_count, 0b111);
	paging_reload(paging_get_highest_page_map());
	__initrd_data.virt_addr = 0xFFFFE00000000000 + offset;

	LOG_INFO("INITRD", "paddr 0x%x aligned to 0x%x off 0x%x", paddr, paddr,
	         paddr_alligned - paddr);

	LOG_INFO("INITRD",
	         "initrd module found at 0x%x (size %d kb), alligned to %d kb",
	         __initrd_data.virt_addr, initrd_module->size / 1024,
	         page_count * BLOCK_SIZE / 1024);

	__initrd_data.raw_addr = initrd_module->start;
	__initrd_data.size = initrd_module->size;

	// We dont need to create a cdev cause initrd already on memory
	// create init directory
	vxCreateFilesystem("initfs", initrd_fs_impl());
	dentry_ptr init_dentry = 0;
	{
		vxNamei("/init", &init_dentry);
		auto inode = vxCreateAndAttachVnode();
		init_dentry->vnode = inode;
		vector_push_back(&inode->dentry_list, init_dentry);
		vxMakeDirectory(vxGetRootDirectory(), init_dentry, 440);
		LOG_DEBUG("INITRD", "entry name %s", init_dentry->name->c_str);
	}

	LoadIntoVfs(init_dentry);

	LOG_INFO("INITRD", "initrd registered");
}

int initrd_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	auto data = (struct initrd_internal_vnode_data*)vnode->private;
	if (!data)
		return -1;

	auto off = data->offset;
	uint8_t* addr = (uint8_t*)__initrd_data.virt_addr + off + offset;
	memcopy(buf, addr, len);
	return len;
}

void LoadIntoVfs(dentry_ptr dentry) {
	uint8_t* addr = (uint8_t*)__initrd_data.virt_addr;
	TarHeader* header = (TarHeader*)addr;

	vops_file_t* initrd_file_ops =
	    (vops_file_t*)kalloc(sizeof(vops_file_t));
	initrd_file_ops->read = initrd_read;

	uint64_t offset = 0;
	while (strncmp(header->ustar, "ustar", 5) == 0) {
		int size = oct2bin((unsigned char*)header->size, 11);
		auto root = dentry->name->c_str;

		dentry_ptr last_dentry = 0;
		if (vxResolveDentry(header->filename, dentry, &last_dentry,
		                    CREATE_MISSING_ENTRY) != VFS_OK) {
			LOG_ERROR("VFS", "failed create dentry for %s",
			          header->filename);
		}

		// if dentry was successfuly created
		if (last_dentry) {
			auto vnode = vxCreateAndAttachVnode();
			vxAttachDentryToVnode(last_dentry, vnode);
			vnode->permission =
			    oct2bin((unsigned char*)header->mode, 8);
			vnode->size = size;
			vnode->ops = (vops_file_t*)initrd_file_ops;

			switch (header->typeflag) {
			case '5':
				vnode->type = VNODE_TYPE_DIR;
				break;
			case '0':
				vnode->type = VNODE_TYPE_FILE;
				struct initrd_internal_vnode_data* data =
				    (struct initrd_internal_vnode_data*)kalloc(
				        sizeof(
				            struct initrd_internal_vnode_data));
				data->offset = offset + 512;
				vnode->private = (void*)data;
				break;
			default:
			}

			// LOG_DEBUG("INITRD", "entry name %s vnode type %d",
			//           last_dentry->name->c_str, vnode->type);
		}

		// LOG_DEBUG("INITRD", "file name %s/%s, size : %d", root,
		//   header->filename, size);

		offset += 512 + ((size + 511) & ~511);
		header = (TarHeader*)((uint8_t*)addr + offset);
	}
}

filesystem_t* initrd_fs_impl() {
	filesystem_t* fs = (filesystem_t*)kalloc(sizeof(filesystem_t));
	fs->ops = (fs_operations_t*)kalloc(sizeof(fs_operations_t));
	return fs;
}

// JANGAN dihapus dulu buat contoh saat implement real fs nanti
// int initrdOpenImpl(void* vdata, int op_mode, thread_t* thread) {
// 	auto internal_data = (struct initrd_internal_data*)vdata;

// 	// TODO: check user who access
// 	if (thread) {
// 		// TODO: check permission
// 		auto uuid = thread->uuid;
// 	}
// 	spin_acquire(&internal_data->lock);
// 	return DEV_OK;
// }

// int initrdCloseImpl(void* data) {
// 	auto internal_data = (struct initrd_internal_data*)data;
// 	spin_release(&internal_data->lock);
// 	return DEV_OK;
// }
