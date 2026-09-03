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

#include "autoconf.h"
#include "init/init.h"
#include "libk/serial.h"
#include "llist.h"
#include "type.h"
#include <hash.h>
#include <str.h>
#include <type.h>
#include <vfs/cache.h>
#include <vfs/dentry.h>

static struct vfs_cache* cache_ = 0;

INIT(VfsCache) {
	cache_ = create_vfs_cache();
	serial2_printf("cache at 0x%x\n", cache_);
}

struct vfs_cache* create_vfs_cache() {
	struct vfs_cache* cache =
	    (struct vfs_cache*)kalloc(sizeof(struct vfs_cache));
	memset(cache, 0, sizeof(struct vfs_cache));
	cache->count = 0;
	__atomic_clear(&cache->lock, __ATOMIC_RELAXED);
	serial2_printf("create cache at 0x%x\n", cache);
	return cache;
}

void hlist_add_head(struct hlist_node* n, struct hlist_head* h) {
	struct hlist_node* first = h->first;
	n->next = first;
	n->prev = NULL;
	if (first)
		first->prev = n;
	h->first = n;
}

static void hlist_del(struct hlist_node* n, struct hlist_head* h) {
	if (n->prev) {
		n->prev->next = n->next;
	} else {
		h->first = n->next;
	}

	if (n->next) {
		n->next->prev = n->prev;
	}

	n->next = n->prev = NULL;
}

__attribute__((always_inline)) void vfs_cache_insert(struct vfs_cache* cache,
                                                     struct dentry* dentry) {
	/* Cache memegang satu referensi agar dentry tidak di-free
	 * selama masih ada di dalam cache. Dilepas di cache_remove. */
	dentry_get(dentry);

	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	auto idx = dentry->hash & (VFS_CACHE_SIZE - 1);

	hlist_add_head(&dentry->hash_node, &cache->buckets[idx]);
	__atomic_fetch_add(&cache->count, 1, __ATOMIC_RELAXED);
	dentry->flags |= DENTRY_IN_CACHE;

	bool has_parent = dentry->parent != NULL;
	if (has_parent)
		llist_add_tail(&dentry->siblings, &dentry->parent->child_list);

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE); // release dulu

	if (!has_parent)
		LOG_DEBUG("VFS", "'%s' has no parent", dentry->name->c_str);
}

struct dentry* cache_lookup(struct vfs_cache* cache, struct dentry* parent,
                            const char* name) {
	uint32_t h = hash_dentry(name, parent);
	auto idx = h & (VFS_CACHE_SIZE - 1);

	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	struct hlist_head* bucket = &cache->buckets[idx];

	for (struct hlist_node* pos = bucket->first; pos != NULL;
	     pos = pos->next) {
		struct dentry* d = container_of(pos, struct dentry, hash_node);
		if (d->parent == parent && strcmp(d->name->c_str, name) == 0) {
			dentry_get(d);
			__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
			return d;
		}
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);

	return 0;
}

void vfs_cache_remove_dentry(struct dentry* dentry) {
	auto cache = cache_;
	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	if (!(dentry->flags & DENTRY_IN_CACHE)) {
		__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
		return;
	}

	uint32_t idx = dentry->hash & (VFS_CACHE_SIZE - 1);
	hlist_del(&dentry->hash_node, &cache->buckets[idx]);
	__atomic_fetch_sub(&cache->count, 1, __ATOMIC_RELAXED);
	dentry->flags &= ~(uint32_t)DENTRY_IN_CACHE;

	if (dentry->siblings.next != NULL && dentry->siblings.next != &dentry->siblings) {
		llist_del(&dentry->siblings);
		dentry->siblings.next = dentry->siblings.prev = &dentry->siblings;
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
}

void cache_remove(struct vfs_cache* cache, struct dentry* dentry) {
	while (__atomic_test_and_set(&cache->lock, __ATOMIC_ACQUIRE))
		;

	if (!(dentry->flags & DENTRY_IN_CACHE)) {
		__atomic_clear(&cache->lock, __ATOMIC_RELEASE);
		return;
	}

	uint32_t idx = dentry->hash & (VFS_CACHE_SIZE - 1);
	hlist_del(&dentry->hash_node, &cache->buckets[idx]);
	__atomic_fetch_sub(&cache->count, 1, __ATOMIC_RELAXED);
	dentry->flags &= ~(uint32_t)DENTRY_IN_CACHE;

	if (dentry->siblings.next != NULL && dentry->siblings.next != &dentry->siblings) {
		llist_del(&dentry->siblings);
		dentry->siblings.next = dentry->siblings.prev = &dentry->siblings;
	}

	__atomic_clear(&cache->lock, __ATOMIC_RELEASE);

	dentry_put(dentry);
}

KERNEL_API struct vfs_cache* get_root_cache() { return cache_; }