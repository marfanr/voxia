#include "hal/cpu/core.h"
#include "str.h"
#include "sys/err_no.h"
#include "vfs/cache.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>

int syscall_close(int fd) {
	auto proc = get_current_core_data()->active_thread->process;
	if (fd < 0 || fd >= (int)proc->fdtable->max_fds) {
		return -EBADF;
	}

	auto fdtable = proc->fdtable;
	auto curr_fd = fdtable->fds[fd];
	fdtable->fds[fd] = nullptr;

	if (__atomic_sub_fetch(&curr_fd->count.counter, 1, __ATOMIC_SEQ_CST) == 0) {
		if (curr_fd->write_buffer && curr_fd->write_buffer_size > 0 &&
			curr_fd->vnode) {
			if (curr_fd->vnode->type == VNODE_TYPE_FILE) {
				auto ops = (vops_file_t*)curr_fd->ops;
				if (ops && ops->write) {
					ops->write(
						curr_fd->vnode, curr_fd->write_buffer,
						curr_fd->write_buffer_size,
						curr_fd->pos - curr_fd->write_buffer_size);
				}
			}
		}

		// Also call ops->flush or custom logic if it's a pipe.
		if (curr_fd->vnode && curr_fd->vnode->type == VNODE_TYPE_FIFO) {
			extern void pipe_close_fd(struct file_descriptor* fd);
			pipe_close_fd(curr_fd);
		}

		/* Simpan pointer dentry sebelum fd di-free, untuk eviction check */
		dentry_ptr file_dentry = curr_fd->dentry;

		if (file_dentry) {
			dentry_put(file_dentry);
		}

		kfree2(curr_fd->write_buffer);
		kfree2(curr_fd);

		if (file_dentry && file_dentry->vnode &&
			file_dentry->vnode->type == VNODE_TYPE_FILE &&
			(file_dentry->flags & DENTRY_IN_CACHE) &&
			!(file_dentry->flags & DENTRY_PINNED) &&
			get_reffcount(file_dentry) == 1) {
			cache_remove(get_root_cache(), file_dentry);
		}
	}


	// TODO: free fd on /proc/
	dentry_ptr proc_dentry;
	if (resolve_dentry("/proc", 0, &proc_dentry, 0) != VFS_OK) {
		return 0;
	}

	dentry_ptr curr_proc_dentry;
	if (resolve_dentry(itoa(proc->pid, 10), proc_dentry, &curr_proc_dentry,
	                   0) != VFS_OK) {
		dentry_put(proc_dentry);
		return 0;
	}

	dentry_ptr fd_dentry;
	if (resolve_dentry(itoa(fd, 10), curr_proc_dentry, &fd_dentry, 0) !=
	    VFS_OK) {
		dentry_put(curr_proc_dentry);
		dentry_put(proc_dentry);
		return 0;
	}

	delete_dentry(fd_dentry);
	dentry_put(curr_proc_dentry);
	dentry_put(proc_dentry);

	return 0;
}