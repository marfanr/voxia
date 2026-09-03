#ifndef __VFS__CACHE_H_
#define __VFS__CACHE_H_

#include <type.h>

#define VFS_CACHE_SIZE 32

struct hlist_node {
	struct hlist_node* next;   
	struct hlist_node* prev; 
	void* dentry;
};

struct hlist_head {
	struct hlist_node* first; // kepala bucket (hanya satu pointer)
};

#define hlist_for_each(pos, head, member)                                      \
	for (pos = (head->first == NULL) ? NULL                                \
					 : container_of((head)->first,         \
							typeof(*pos), member); \
	     pos != NULL;                                                      \
	     pos = (pos->member.next) ? container_of(pos->member.next,         \
						     typeof(*pos), member)     \
				      : NULL)

struct dentry;

struct vfs_cache {
	struct hlist_head buckets[VFS_CACHE_SIZE];
	uint8_t lock;
	int count;
} __attribute__((aligned(64)));

void hlist_add_head(struct hlist_node* n, struct hlist_head* h);

struct vfs_cache* create_vfs_cache();
struct vfs_cache* get_root_cache();
void vfs_cache_insert(struct vfs_cache* cache, struct dentry* dentry);
struct dentry*
cache_lookup(struct vfs_cache* cache, struct dentry* parent, const char* name);

void cache_remove(struct vfs_cache* cache, struct dentry* dentry);
void vfs_cache_remove_dentry(struct dentry* dentry);

#endif // __VFS__CACHE_H_