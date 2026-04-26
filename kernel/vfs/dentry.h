#ifndef __VFS__DENTRY_H__
#define __VFS__DENTRY_H__

#include <libk/string.h>
#include <libk/type.h>
#include <libk/vector.h>

typedef struct dentry dentry_t;
typedef struct dentry* dentry_ptr;
define_vector(dentry_ptr);

struct vnode;

struct dentry {
	string name;
	struct vnode* vnode;
	dentry_ptr parent;
	vector(dentry_ptr) children;
	uintptr_t addr;
} __attribute__((aligned(32)));

/**
 * @brief Resolves a file path to a directory entry (dentry).
 *
 * This function traverses the filesystem tree starting from the root dentry.
 * It tokenizes the input path and iterates through the directory structure.
 *
 * Behavior:
 * - If an intermediate directory in the path is missing, the function fails.
 * - If the final component of the path is missing, a new dentry is created.
 *
 * @param path The null-terminated string representing the absolute path to
 * resolve.
 * @param out  A pointer to a dentry pointer. On success, this will be updated
 * to point to the resolved (or newly created) dentry.
 *
 * @return int Returns VFS_OK on success, or 0 if an intermediate directory
 * was not found.
 */
int vxNamei(char* path, dentry_ptr* out);

/**
 * This function handles memory allocation for a new dentry using the slab
 * allocator. It also links the dentry to its corresponding VNode (if provided).
 *
 * @note This function performs lazy initialization of the dentry slab cache
 * if it does not exist yet.
 *
 * @param name  The name of the file or directory.
 * @param vnode Pointer to the associated VNode (inode data).
 * Pass NULL if the VNode is not yet available.
 *
 * @return dentry_ptr A pointer to the newly allocated and initialized dentry.
 */
dentry_ptr vxCreateDentry(string name, struct vnode* vnode);

void vxSetDentryAsRoot(dentry_ptr dentry);
dentry_ptr vxGetRootDirectory();

enum {
	RESOLVE_LAST_ENTRY = (1 << 1),
	CREATE_MISSING_ENTRY = (1 << 2),
};

/**
 * @brief Resolves a path to a directory entry (dentry) with configurable start
 * point and strictness.
 *
 * This function traverses the filesystem path. It differs from vxNamei by
 * allowing a specific parent dentry to start from, and it can handle cases
 * where the final component of the path does not exist yet (controlled by
 * flags).
 *
 * @param path   The file path to resolve (can be relative or absolute).
 * @param parent The dentry to start the search from. If NULL, starts from
 * `root_dentry`.
 * @param out    Double pointer to store the resulting dentry.
 * @param flag   Bitmask to modify behavior.
 * - If `RESOLVE_LAST_ENTRY` is set: If a path component is missing,
 * the function stops, sets `*out` to the *current* directory (the parent),
 * and returns the index of the missing component. This is useful when
 * creating new files/directories.
 *
 * @return int
 * - VFS_OK: The full path was successfully resolved to an existing dentry.
 * - VFS_ENOENT: A directory in the path was missing (and no flag was set).
 * - [Index]: If `RESOLVE_LAST_ENTRY` was set and the target was missing,
 * returns the index of the missing component in the path.
 */
int vxResolveDentry(char* path, dentry_ptr parent, dentry_ptr* out,
                    uint8_t flag);

void vxAttachDentryToParent(dentry_ptr dentry, dentry_ptr parent);
void vxFreeDentry(dentry_ptr dentry);
void vxFreeDentryWithChildren(dentry_ptr dentry);

typedef struct vnode* vnode_ptr_t;
void vxAttachDentryToVnode(dentry_ptr dentry, vnode_ptr_t vnode);

#endif // __VFS__DENTRY_H__