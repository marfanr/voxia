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
#include <vector.h>

#include "llist.h"
#include "type.h"
#include "vfs/cache.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include <vfs/vfs.h>
#include <vfs/vnode.h>

#include <libk/fs/tar.h>
#include <libk/serial.h>
#include <str.h>

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
	uint64_t paddr_alligned = (uint64_t) (paddr & ~(BLOCK_SIZE - 1));
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
	// vxCreateFilesystem("initfs", initrd_fs_impl());
	dentry_ptr init_dentry = 0;
	{
		vxnamei("/init", &init_dentry);
		auto inode = create_and_attach_vnode();
		init_dentry->vnode = inode;
		inode->permission = 660;
		inode->type = VNODE_TYPE_DIR;

		LOG_DEBUG("INITRD", "entry name %s", init_dentry->name->c_str);
	}

	LoadIntoVfs(init_dentry);

	LOG_INFO("INITRD", "initrd registered");
}

static size_t
initrd_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	auto data = (struct initrd_internal_vnode_data*) vnode->vnode_private;
	if (!data)
		return 0;

	auto off = data->offset;
	uint8_t* addr = (uint8_t*) __initrd_data.virt_addr + off + offset;
	memcopy(buf, addr, len);
	return len;
}

__attribute__((unused)) static void
print_dentry_tree(dentry_t* node, int depth) {
	if (!node)
		return;

	// indent
	char indent[128] = {0};
	for (int i = 0; i < depth; i++) {
		indent[i * 2] = ' ';
		indent[i * 2 + 1] = ' ';
	}

	serial_printf("%s└── %s (0x%lx) (%lx)\n", indent, node->name->c_str,
		      node, node->hash);

	// rekursi ke semua child
	struct llist_head* pos = node->child_list.next;
	while (pos != &node->child_list) {
		dentry_t* child = container_of(pos, dentry_t, siblings);

		print_dentry_tree(child, depth + 1);
		pos = pos->next;
	}
}

KERNEL_API void LoadIntoVfs(dentry_ptr dentry) {
	uint8_t* const base = (uint8_t*) __initrd_data.virt_addr;
	uint64_t offset = 0;

	vops_file_t* initrd_file_ops =
		(vops_file_t*) kalloc(sizeof(vops_file_t));
	initrd_file_ops->read = initrd_read;

	LOG_DEBUG("INITRD", "loading initrd into vfs started from %s",
		  dentry->name->c_str);

	while (1) {
		if (offset + sizeof(TarHeader) > __initrd_data.size)
			break;

		TarHeader header;
		memcopy(&header, base + offset, sizeof(TarHeader));

		if (strncmp(header.ustar, "ustar", 5) != 0)
			break;

		size_t size = (size_t) oct2bin((unsigned char*) header.size,
					       sizeof(header.size));

		if (size > __initrd_data.size - offset - sizeof(TarHeader)) {
			LOG_ERROR("INITRD",
				  "invalid entry size %zu at offset %llu, "
				  "aborting",
				  size, (unsigned long long) offset);
			break;
		}

		LOG2_INFO("INITRD", "registering file %s", header.filename);

		uint64_t data_size_aligned =
			((uint64_t) size + 511u) & ~(uint64_t) 511u;
		uint64_t next_offset = offset + 512u + data_size_aligned;

		if (next_offset < offset) { /* wraparound */
			LOG_ERROR("INITRD", "offset overflow at %llu, aborting",
				  (unsigned long long) offset);
			break;
		}

		{
			/*
             * Semua deklarasi variabel di sini, SEBELUM goto target,
             * sehingga goto next_entry tidak melompati inisialisasi.
             *
             */
			dentry_ptr last_dentry = NULL;
			if (vxResolveDentry(header.filename, dentry,
					    &last_dentry, CREATE_MISSING_ENTRY)
			    != VFS_OK) {
				LOG_ERROR("VFS", "failed create dentry for %s",
					  header.filename);
			}

			if (last_dentry) {
				auto vnode = create_and_attach_vnode();
				last_dentry->vnode = vnode;
				vnode->permission = (uint16_t) oct2bin(
					(unsigned char*) header.mode,
					sizeof(header.mode));

				vnode->size = size;
				vnode->ops = initrd_file_ops;
				switch (header.typeflag) {
				case '5':
					vnode->type = VNODE_TYPE_DIR;
					break;
				case '0': {
					vnode->type = VNODE_TYPE_FILE;
					struct initrd_internal_vnode_data* data =
						(struct
						 initrd_internal_vnode_data*)
							kalloc(sizeof(
								struct
								initrd_internal_vnode_data));
					data->offset = offset + 512u;
					vnode->vnode_private = (void*) data;
					break;
				}
				default:
					LOG_WARN("INITRD",
						 "unhandled typeflag '%c' for "
						 "%s",
						 header.typeflag,
						 header.filename);
					break;
				}
			}
		}

		offset = next_offset;
	}

	LOG_DEBUG("INITRD", "done loading initrd into vfs");
	print_dentry_tree(dentry, 0);
}

filesystem_t* initrd_fs_impl() {
	filesystem_t* fs = (filesystem_t*) kalloc(sizeof(filesystem_t));
	fs->ops = (fs_operations_t*) kalloc(sizeof(fs_operations_t));
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
