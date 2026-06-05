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

	auto saved_path = str(path);

	serial2_printf("open path %s flags %d mode %d\n", saved_path->c_str,
	               flags, mode);

	dentry_ptr out;
	if (resolve_dentry(saved_path->c_str, 0, &out, (uint8_t)flags) !=
	    VFS_OK) {
		str_release(saved_path);
		return -EEXIST;
	}

	auto fpath = get_full_path_from_dentry(out);
	serial2_printf("found at %s (vnode 0x%x)\n", fpath->c_str, out->vnode);
	str_release(fpath);

	auto fdtable = proc->fdtable;
	if (!fdtable) {
		return -1;
	}

	auto next_fd = fdtable->next_fd++;
	auto fd = alloc_fd();
	fd->vnode = out->vnode;
	fd->flags = (uint32_t)flags;
	fd->ops = out->vnode->ops;
	fdtable->fds[next_fd] = fd;

	// create fd dir
	// TODO: will be moved into fd directly

	dentry_ptr proc_dentry;
	if (resolve_dentry("/proc", 0, &proc_dentry, 0) != VFS_OK) {
		str_release(saved_path);
		return -1;
	}

	dentry_ptr curr_proc_dentry;
	if (resolve_dentry(itoa(proc->pid, 10), proc_dentry, &curr_proc_dentry,
	                   CREATE_MISSING_ENTRY) != VFS_OK) {
		str_release(saved_path);
		dentry_put(proc_dentry);
		return -1;
	}

	dentry_ptr fd_dentry;
	if (resolve_dentry(itoa(next_fd, 10), curr_proc_dentry, &fd_dentry,
	                   CREATE_MISSING_ENTRY) != VFS_OK) {
		str_release(saved_path);
		dentry_put(proc_dentry);
		dentry_put(curr_proc_dentry);
		return -1;
	}

    fd_dentry->vnode = out->vnode;
    dentry_get(out);
    dentry_get(fd_dentry);

    str_release(saved_path);

	return (int)next_fd;
}