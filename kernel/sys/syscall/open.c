#include "console/console.h"
#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_SYNC 04010000
#define O_RSYNC 04010000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000

#define O_ASYNC 020000
#define O_DIRECT 040000
#define O_LARGEFILE 0100000
#define O_NOATIME 01000000
#define O_PATH 010000000
#define O_TMPFILE 020200000

int syscall_open(const char* path, int flags, int mode) {
	(void)path;
	(void)flags;
	(void)mode;

	auto thr = get_current_core_data()->active_thread;
	if (!thr) {
		return -1;
	}

	auto proc = thr->process;
	if (!proc) {
		return -1;
	}

	auto saved_path = safe_str_from_user(proc->page, path);

	serial2_printf("open path %s flags %d mode %d\n", saved_path->c_str,
	               flags, mode);

	dentry_ptr out;
	// TODO: handle flag
	uint8_t fl = 0;
	if (flags & O_CREAT)
		fl |= CREATE_MISSING_ENTRY;

	dentry_ptr base_dir = (saved_path->c_str[0] == '/') ? 0 : proc->cwd;
	if (resolve_dentry(saved_path->c_str, base_dir, &out, fl) != VFS_OK) {
		str_release(saved_path);
		return -ENOENT;
	}

	if (!out->vnode) {
		str_release(saved_path);
		dentry_put(out);
		return -ENOENT;
	}

	auto fpath = get_full_path_from_dentry(out);
	serial2_printf("found at %s (vnode 0x%x)\n", fpath->c_str, out->vnode);
	str_release(fpath);

	if ((flags & O_TRUNC) && out->vnode &&
	    out->vnode->type == VNODE_TYPE_FILE) {
		serial2_printf("Trunc detected\n");
		auto ops = (vops_file_t*)out->vnode->ops;
		if (ops && ops->truncate) {
			ops->truncate(out->vnode, 0);
		}
	}

	auto fdtable = proc->fdtable;
	if (!fdtable) {
		return -1;
	}

	uint32_t fd_id = 0;
	auto next_fd = fdtable->next_fd;
	if (next_fd >= fdtable->max_fds) {
		return -EMFILE;
	}

	for (fd_id = next_fd; fd_id < fdtable->max_fds; fd_id++) {
		if (fdtable->fds[fd_id] == nullptr)
			break;
	}

	fdtable->next_fd = fd_id + 1;

	auto fd = alloc_fd();
	fd->vnode = out->vnode;
	fd->flags = (uint32_t)flags;
	if (flags & O_CLOEXEC) {
		fd->fd_flags = 1; /* FD_CLOEXEC */
	}
	fd->ops = out->vnode->ops;
	// fd menyimpan referensi permanen ke dentry — naikkan refcount.
	dentry_get(out);
	fd->dentry = out;
	fdtable->fds[fd_id] = fd;

	serial2_printf("opened at fd %d\n", fd_id);

	dentry_ptr proc_dentry;
	if (resolve_dentry("/proc", 0, &proc_dentry, 0) != VFS_OK) {
		str_release(saved_path);
		// Batalkan fd yang sudah dialokasikan
		dentry_put(out); // refcount dari dentry_get di atas
		dentry_put(out); // refcount dari resolve_dentry
		fdtable->fds[fd_id] = nullptr;
		kfree2(fd);
		return -1;
	}

	dentry_ptr curr_proc_dentry;
	if (resolve_dentry(itoa(proc->pid, 10), proc_dentry, &curr_proc_dentry,
	                   CREATE_MISSING_ENTRY) != VFS_OK) {
		str_release(saved_path);
		dentry_put(proc_dentry);
		dentry_put(out); // refcount dari dentry_get
		dentry_put(out); // refcount dari resolve_dentry
		fdtable->fds[fd_id] = nullptr;
		kfree2(fd);
		return -1;
	}
	if (curr_proc_dentry->vnode) {
		curr_proc_dentry->vnode->type = VNODE_TYPE_DIR;
		curr_proc_dentry->vnode->permission = 0555;
	}

	dentry_ptr fd_dentry;
	if (resolve_dentry(itoa(fd_id, 10), curr_proc_dentry, &fd_dentry,
	                   CREATE_MISSING_ENTRY) != VFS_OK) {
		str_release(saved_path);
		dentry_put(proc_dentry);
		dentry_put(curr_proc_dentry);
		dentry_put(out); // refcount dari dentry_get
		dentry_put(out); // refcount dari resolve_dentry
		fdtable->fds[fd_id] = nullptr;
		kfree2(fd);
		return -1;
	}

	fd_dentry->vnode = out->vnode;

	dentry_put(fd_dentry);
	dentry_put(curr_proc_dentry);
	dentry_put(proc_dentry);

	dentry_put(out);

	str_release(saved_path);

	vops_file_t* ops = fd->ops;
	if (ops) {
		if (ops->redirect_on_open) {
			ops->redirect_on_open(fd->vnode, fdtable->fds[fd_id]);
		}
	}

	return (int)fd_id;
}