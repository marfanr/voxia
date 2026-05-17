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

#include <vfs/cache.h>
#include "autoconf.h"
#include "init/init.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "llist.h"
#include "type.h"
#include <str.h>
#include <vfs/dentry.h>
#include <hash.h>

static struct vfs_cache* cache_ = 0;

INIT(VfsCache) {
	cache_ = create_vfs_cache();
	serial2_printf("cache at 0x%x\n", cache_);
}

struct vfs_cache* create_vfs_cache() {
	struct vfs_cache* cache =
		(struct vfs_cache*) kalloc(sizeof(struct vfs_cache));
	memset(cache, 0, sizeof(struct vfs_cache));
	cache->count = 0;
	__atomic_clear(&cache->lock, __ATOMIC_RELAXED);
	serial2_printf("create cache at 0x%x\n", cache);
	return cache;
}

static void hlist_add_head(struct hlist_node* n, struct hlist_head* h) {
	struct hlist_node* first = h->first;
	n->next = first;
	n->prev = NULL;
	if (first)
		first->prev = n;
	h->first = n;
}

__attribute__((always_inline)) void
vfs_cache_insert(struct vfs_cache* cache, struct dentry* dentry) {
	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	auto idx = dentry->hash & (VFS_CACHE_SIZE - 1);

	hlist_add_head(&dentry->hash_node, &cache->buckets[idx]);
	__atomic_fetch_add(&cache->count, 1, __ATOMIC_RELAXED);

	if (dentry->parent) {
		llist_add_tail(&dentry->siblings, &dentry->parent->child_list);
	} else {
		LOG_DEBUG("VFS", "'%s' has no parent", dentry->name->c_str);
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
}

struct dentry*
cache_lookup(struct vfs_cache* cache, struct dentry* parent, const char* name) {
	uint32_t h = hash_dentry(name, parent);
	auto idx = h & (VFS_CACHE_SIZE - 1);

	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	struct hlist_head* bucket = &cache->buckets[idx];

	for (struct hlist_node* pos = bucket->first; pos != NULL;
	     pos = pos->next) {
		struct dentry* d = container_of(pos, struct dentry, hash_node);
		if (d->parent == parent && strcmp(d->name->c_str, name) == 0) {
			__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
			return d;
		}
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);

	return 0;
}

// static void hlist_del(struct hlist_node* n) {
// 	UNUSED(n);
// 	// struct hlist_node* next = n->next;
// 	// struct hlist_node** pprev = n->pprev;

// 	// if (next)
// 	// 	next->pprev = pprev;
// 	// *pprev = next;

// 	// n->next = NULL;
// 	// n->pprev = NULL;
// }

void cache_remove(struct vfs_cache* cache, struct dentry* dentry) {
	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	// hapus dari hash table
	// hlist_del(&dentry->hash_node);
	__atomic_fetch_sub(&cache->count, 1, __ATOMIC_RELAXED);

	// hapus dari child_list parent — ini yang kurang
	if (dentry->parent && dentry->siblings.next != NULL) {
		// llist_del(&dentry->siblings);
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
}

KERNEL_API struct vfs_cache* get_root_cache() {
	return cache_;
}