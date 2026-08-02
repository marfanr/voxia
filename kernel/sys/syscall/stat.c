#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/vm_manager.h"
#include "str.h"
#include "string.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <sys/syscall.h>
#include <sys/fd.h>

#define S_IFMT 0170000

#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

int syscall_stat(const char* path, struct stat* buf) {
	auto path_str = str(path);
	serial2_printf("syscall_stat: path=%s, buf=%p\n", path, buf);

	auto proc = get_current_core_data()->active_thread->process;
	
	dentry_ptr base_dir = (proc && path_str->c_str[0] != '/') ? proc->cwd : 0;
	dentry_ptr out;
	if (resolve_dentry(path_str->c_str, base_dir, &out, 0) != VFS_OK) {
		serial2_printf("syscall_stat: failed to resolve path %s\n",
		               path_str->c_str);
		str_release(path_str);
		return -ENOENT;
	}

	struct stat st = {0};
	st.st_ino = out->vnode->id;
	if (out->vnode->type == VNODE_TYPE_DIR) {
		st.st_mode = S_IFDIR | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_CHR) {
		st.st_mode = S_IFCHR | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_BLK) {
		st.st_mode = S_IFBLK | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_LNK) {
		st.st_mode = S_IFLNK | out->vnode->permission;
	} else {
		st.st_mode = S_IFREG | out->vnode->permission;
	}
	st.st_nlink = 1;
	st.st_uid = 0;
	st.st_gid = 0;
	st.st_rdev = 0;
	st.st_size = (int64_t)out->vnode->size;

	paging_reload(proc->page);
	memcopy(buf, &st, sizeof(struct stat));

	str_release(path_str);

	return 0;
}

int syscall_newfstatat(int dirfd, const char* pathname, struct stat* statbuf, int flags) {
	auto proc = get_current_core_data()->active_thread->process;
	if (!proc) return -ENOENT;

	int AT_EMPTY_PATH = 0x1000;
	if ((flags & AT_EMPTY_PATH) && pathname[0] == '\0') {
		return syscall_fstat(dirfd, statbuf);
	}

	auto path_str = str(pathname);
	dentry_ptr base_dir = 0;
	
	if (path_str->c_str[0] != '/') {
		if (dirfd == -100) {
			base_dir = proc->cwd;
		} else {
			auto fdtable = proc->fdtable;
			if (!fdtable || dirfd < 0 || (uint32_t)dirfd >= fdtable->max_fds || !fdtable->fds[dirfd]) {
				str_release(path_str);
				return -EBADF;
			}
			base_dir = fdtable->fds[dirfd]->dentry;
		}
	}

	dentry_ptr out;
	if (resolve_dentry(path_str->c_str, base_dir, &out, 0) != VFS_OK) {
		str_release(path_str);
		return -ENOENT;
	}

	struct stat st = {0};
	st.st_ino = out->vnode->id;
	if (out->vnode->type == VNODE_TYPE_DIR) {
		st.st_mode = S_IFDIR | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_CHR) {
		st.st_mode = S_IFCHR | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_BLK) {
		st.st_mode = S_IFBLK | out->vnode->permission;
	} else if (out->vnode->type == VNODE_TYPE_LNK) {
		st.st_mode = S_IFLNK | out->vnode->permission;
	} else {
		st.st_mode = S_IFREG | out->vnode->permission;
	}
	st.st_nlink = 1;
	st.st_uid = 0;
	st.st_gid = 0;
	st.st_rdev = 0;
	st.st_size = (int64_t)out->vnode->size;

	paging_reload(proc->page);
	memcopy(statbuf, &st, sizeof(struct stat));

	str_release(path_str);
	dentry_put(out);

	return 0;
}