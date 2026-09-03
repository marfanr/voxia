#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "str.h"
#include "sys/err_no.h"
#include "sys/fd.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <sys/syscall.h>

#define S_IFMT 0170000

#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

int syscall_fstat(int fd, struct stat* buf) {
	serial2_printf("syscall_fstat: fd=%d, buf=%p\n", fd, buf);

	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		return -EBADF;
	}

	struct stat st = {0};
	st.st_ino = curr_fd->vnode->id;
	if (curr_fd->vnode->type == VNODE_TYPE_DIR) {
		st.st_mode = S_IFDIR | curr_fd->vnode->permission;
	} else if (curr_fd->vnode->type == VNODE_TYPE_CHR) {
		st.st_mode = S_IFCHR | curr_fd->vnode->permission;
	} else if (curr_fd->vnode->type == VNODE_TYPE_BLK) {
		st.st_mode = S_IFBLK | curr_fd->vnode->permission;
	} else if (curr_fd->vnode->type == VNODE_TYPE_LNK) {
		st.st_mode = S_IFLNK | curr_fd->vnode->permission;
	} else {
		st.st_mode = S_IFREG | curr_fd->vnode->permission;
	}
	st.st_nlink = 1;
	st.st_uid = 0;
	st.st_gid = 0;
	st.st_rdev = 0;
	st.st_size = (int64_t)curr_fd->vnode->size;

	paging_reload(curr_procc->page);
	memcopy(buf, &st, sizeof(struct stat));
	return 0;
}
