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

#include "vfs/cache.h"
#include "autoconf.h"
#include "hal/acpi/hpet.h"
#include "init/init.h"
#include "libk/hash.h"
#include "libk/string.h"
#include "memory/slab.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"

static vfs_cache_blob_t vfs_cache_blob[VOXIA_VFS_CACHE_BLOB_SIZE];
static struct slab_cache* vfs_cache_cache;

INIT(VfsCache) {
	vxCreateSlabCache(&vfs_cache_cache, "vfs_cache", sizeof(vfs_cache_t),
	                  64, 0);
}

void vxCreateNodeCache(string path, vnode_ptr_t inode) {
	uint64_t hash_index = hash(path->c_str, VOXIA_VFS_CACHE_BLOB_SIZE);
	vfs_cache_blob_t* cache = &vfs_cache_blob[hash_index];

	vfs_cache_t* node = (vfs_cache_t*)vxSlabAlloc(vfs_cache_cache);
	node->path = path;
	node->inode = inode;
	node->next = cache->head;
	cache->head = node;
	if (vxHPETGetMainCount())
		cache->last_access = vxHPETGetMainCount();
}

vnode_ptr_t vxLookupNodeCache(string path) {
	uint64_t hash_index = hash(path->c_str, VOXIA_VFS_CACHE_BLOB_SIZE);
	vfs_cache_blob_t* cache = &vfs_cache_blob[hash_index];

	if (cache->head) {
		vfs_cache_t* node = cache->head;
		while (node) {
			if (stringcmp(node->path, path)) {
				cache->last_access = vxHPETGetMainCount();
				return node->inode;
			}
			node = node->next;
		}
	}
	return NULL;
}