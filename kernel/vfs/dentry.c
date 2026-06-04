#include <hash.h>
#include <llist.h>
#include <str.h>
#include <string.h>
#include <type.h>
#include <vector.h>
#include <vfs/dentry.h>

#include "libk/serial.h"
#include "llist.h"
#include "memory/kalloc.h"
#include "memory/slab.h"
#include "vfs/cache.h"
#include "vfs/dev.h"
#include "vfs/enum.h"
#include "vfs/rcu.h"
#include "vfs/vnode.h"
#include <spinlock.h>
#include <type.h>

static struct slab_cache* dentry_cache = 0;
static dentry_t* root_dentry = 0;

inline uint32_t hash_dentry(const char* name, dentry_ptr parent) {
	auto h = hash32(name, 0);
	if (parent) {
		uintptr_t p = (uintptr_t)parent;
		h ^= (uint32_t)(p & 0xFFFFFFFF);
		h ^= (uint32_t)(p >> 32);
	}
	return h;
}

static spinlock_t lock = {0};

dentry_ptr KERNEL_API create_dentry(kstring name, vnode_t* vnode,
                                    dentry_ptr parent) {
	if (!dentry_cache)
		vxCreateSlabCache(&dentry_cache, "dentry",
		                  sizeof(struct dentry), 0, 0);

	spin_acquire(&lock);
	dentry_t* dentry = (dentry_t*)vxSlabAlloc(dentry_cache);
	// dentry_t* dentry = (dentry_t*)kalloc(sizeof(struct dentry));
	memset(dentry, 0, sizeof(dentry_t));

	__atomic_fetch_add(&dentry->refcount.counter, 1, __ATOMIC_RELAXED);
	dentry->hash = hash_dentry(name->c_str, parent);
	dentry->name = name;
	dentry->vnode = vnode;
	dentry->parent = parent;

	dentry->hash_node.next = dentry->hash_node.prev = &dentry->hash_node;
	dentry->hash_node.dentry = (void*)dentry;

	llist_init(&dentry->siblings);
	llist_init(&dentry->child_list);
	spin_release(&lock);

	return dentry;
}

static void dentry_free_rcu(struct rcu_head* head) {
	auto dentry = container_of(head, struct dentry, rcu);
	slab_free(dentry_cache, dentry);
}

void dentry_get(dentry_ptr dentry) {
	__atomic_fetch_add(&dentry->refcount.counter, 1, __ATOMIC_RELAXED);
}

void dentry_put(dentry_ptr dentry) {
	if (__atomic_fetch_sub(&dentry->refcount.counter, 1,
	                       __ATOMIC_RELAXED) == 1) {
		call_rcu(&dentry->rcu, dentry_free_rcu);
	}
}

void KERNEL_API vxAttachDentryToVnode(dentry_ptr dentry, vnode_t* vnode) {
	dentry->vnode = vnode;
	// vector_push_back(&vnode->dentry_list, dentry);
}

void vxSetDentryAsRoot(dentry_ptr dentry) { root_dentry = dentry; }

dentry_ptr KERNEL_API get_root_dentry() { return root_dentry; }

int KERNEL_API resolve_dentry(const char* path, dentry_ptr parent,
                              dentry_ptr* out, uint8_t flag) {
	if (!path || !out)
		return -1;

	auto root_cache = get_root_cache();
	auto path_copy = str(path);
	if (!path_copy)
		return -1;

	dentry_t* curr = parent ? parent : root_dentry;
	dentry_get(curr);

	char* path_ = path_copy->c_str;
	while (path_ != NULL && *path_ != '\0') {
		char* component = strsep2(&path_, "/");

		if (!component || *component == '\0')
			continue;

		// strip trailing slash dan whitespace
		size_t len = strlen(component);
		while (len > 0 &&
		       (component[len - 1] == '/' || component[len - 1] == ' '))
			component[--len] = '\0';

		if (len == 0)
			continue;

		if (strcmp(component, ".") == 0)
			continue;

		if (strcmp(component, "..") == 0) {
			dentry_t* up = curr->parent; // baca sekali
			if (up) {
				dentry_get(up);
				dentry_put(curr);
				curr = up;
			}
			continue;
		}

		dentry_t* next = cache_lookup(root_cache, curr, component);

		if (!next) {
			if (flag & CREATE_MISSING_ENTRY) {
				dentry_t* new_entry =
				    create_dentry(str(component), 0, curr);
				if (!new_entry)
					goto fail;

				vfs_cache_insert(root_cache, new_entry);
				dentry_get(new_entry);
				dentry_put(curr);
				curr = new_entry;
				continue;

			} else {
				auto curr_vnode = curr->vnode;
				if (!curr_vnode)
					goto fail;

				auto fs_instance = curr_vnode->fs_instance;
				if (!fs_instance)
					goto fail;

				if (!fs_instance->fs || !fs_instance->cdev)
					goto fail;

				auto ops = fs_instance->fs->data.ops;
				if (!ops || !ops->lookup)
					goto fail;

				serial2_printf("lookup into fs %s\n",
				               component);
				if (ops->lookup(fs_instance, component, curr,
				                &next) != VFS_OK)
					goto fail;

				serial2_printf("success lokup from fs %s\n",
				               component);

				vfs_cache_insert(root_cache, next);
				dentry_get(next);
				dentry_put(curr);
				curr = next;
				continue;
			}
		}

		dentry_get(next);
		dentry_put(curr);
		curr = next;
	}

	*out = curr;
	str_release(path_copy);
	return VFS_OK;

fail:
	LOG2_DEBUG("Dentry", "missing '%s'", path_copy->c_str);
	dentry_put(curr);
	*out = NULL;
	str_release(path_copy);
	return -1;
}

int KERNEL_API vxnamei(const char* path, dentry_ptr* out) {
	if (!path || !out)
		return -1;

	*out = NULL;

	size_t len = strlen(path);

	if (len == 0) {
		return -1;
	}

	char* temp = (char*)kalloc(len + 1);

	if (!temp) {
		return -1;
	}

	memset(temp, 0, len + 1);
	memcopy(temp, (void*)path, len);

	auto root_cache = get_root_cache();

	dentry_t* curr = root_dentry;

	if (!curr) {
		kfree2(temp);
		return -1;
	}

	dentry_get(curr);

	char* path_iter = temp;

	while (path_iter && *path_iter != '\0') {
		char* component = strsep2(&path_iter, "/");

		if (!component || component[0] == '\0')
			continue;

		dentry_t* next = cache_lookup(root_cache, curr, component);

		if (!next) {
			dentry_t* new_entry =
			    create_dentry(str(component), 0, curr);

			if (!new_entry) {
				dentry_put(curr);
				kfree2(temp);
				return -1;
			}

			vfs_cache_insert(root_cache, new_entry);

			dentry_get(new_entry);
			dentry_put(curr);
			curr = new_entry;
			continue;
		}

		dentry_get(next);
		dentry_put(curr);

		curr = next;
	}

	kfree2(temp);

	*out = curr;

	return VFS_OK;
}

// TODO: need to recheck, maybe can merged with umount
void delete_dentry(dentry_t* node) {
	if (!node)
		return;

	size_t n = 0;
	struct llist_head* pos = node->child_list.next;
	while (pos != &node->child_list) {
		n++;
		pos = pos->next;
	}

	dentry_t** children = NULL;
	if (n > 0) {
		children = (dentry_t**)kalloc(sizeof(dentry_t*) * n);
		int i = 0;
		pos = node->child_list.next;
		while (pos != &node->child_list) {
			children[i++] = container_of(pos, dentry_t, siblings);
			pos = pos->next;
		}
	}

	cache_remove(get_root_cache(), node);
	dentry_put(node);

	for (size_t i = 0; i < n; i++)
		delete_dentry(children[i]);

	if (children)
		kfree(children, sizeof(dentry_t*) * n);
}

void print_dentry_tree(dentry_t* dentry, int depth) {
	if (!dentry)
		return;

	// indent
	char indent[128] = {0};
	for (int i = 0; i < depth; i++) {
		indent[i * 2] = ' ';
		indent[i * 2 + 1] = ' ';
	}

	serial2_printf("%s└── %s (0x%x) (%x) (reff %d)", indent,
	               dentry->name->c_str, dentry, dentry->hash,
	               dentry->refcount.counter);

	auto vnode = dentry->vnode;
	if (vnode) {
		serial2_printf(" permission %d ", vnode->permission);
		if (vnode->type == VNODE_TYPE_BLK) {
			serial2_printf("BLOCK DEVICE %d:%d",
			               vnode->device.major,
			               vnode->device.minor);
		} else if (vnode->type == VNODE_TYPE_CHR) {
			serial2_printf("CHAR DEVICE");
		} else {
			serial2_printf("[%s]",
			               vnode->fs_instance
			                   ? vnode->fs_instance->fs->name
			                   : "NO Filesystem");

			if (vnode->mountedhere) {
				auto block_dentry =
				    vnode->fs_instance->block_dentry;
				auto full_path =
				    get_full_path_from_dentry(block_dentry);
				serial2_printf(" <%s>", full_path->c_str);
				str_release(full_path);
			}
		}
	}
	serial2_printf("\n");

	/* dong recursive in all children */
	struct llist_head* pos = dentry->child_list.next;
	while (pos != &dentry->child_list) {
		dentry_t* child = container_of(pos, dentry_t, siblings);

		print_dentry_tree(child, depth + 1);
		pos = pos->next;
	}
}

int get_reffcount(dentry_ptr dentry) {
	return __atomic_load_n(&dentry->refcount.counter, __ATOMIC_RELAXED);
}

kstring get_full_path_from_dentry(dentry_ptr dentry) {
	if (!dentry)
		return 0;

	if (!dentry->parent)
		return str(dentry->name->c_str);

	if (!dentry->name)
		return 0;

	kstring curr_name = str(dentry->name->c_str);
	if (!curr_name)
		return 0;

	auto curr = dentry->parent;
	while (curr) {
		if (!curr->parent || !curr->name) {
			kstring rooted = str_concat_prefix(curr_name, "/");
			str_release(curr_name);
			return rooted;
		}

		kstring with_slash = str_concat(curr->name, "/");
		if (!with_slash) {
			str_release(curr_name);
			return NULL;
		}

		kstring merged = str_concat(with_slash, curr_name->c_str);
		str_release(with_slash);
		str_release(curr_name);

		if (!merged)
			return NULL;

		curr_name = merged;
		curr = curr->parent;
	}

	return curr_name;
}