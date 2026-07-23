#include "net/socket.h"
#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "str.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>

/* must match musl: O_NONBLOCK = 04000 */
#define O_NONBLOCK      04000
#define SOCK_NONBLOCK   04000

int syscall_socket(int domain, int type, int protocol) {
	serial2_printf("syscall_socket domain %d type %d protocol %d\n", domain,
	               type, protocol);

	auto curr_thread = get_current_core_data()->active_thread;
	if (!curr_thread) {
		return -ENOENT;
	}

	auto proc = curr_thread->process;
	if (!proc) {
		return -ENOENT;
	}

#define SOCK_CLOEXEC    02000000

	int nonblock_flag = (type & SOCK_NONBLOCK) ? O_NONBLOCK : 0;
	int cloexec_flag = (type & SOCK_CLOEXEC) ? 1 : 0;
	type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC); /* strip type modifiers */

	socket_t* socket = 0;
	int res = create_socket((sock_family_t)domain, (sock_type_t)type,
	                        (uint16_t)protocol, &socket);

	if (res != 0)
		return -ENOENT;

	auto fdtable = proc->fdtable;
	if (!fdtable) {
		return -ENOENT;
	}

	uint32_t fd_id = 0;
	uint32_t max = fdtable->max_fds;

	bool found = false;
	for (fd_id = 0; fd_id < max; fd_id++) {
		if (fdtable->fds[fd_id] == nullptr) {
			found = true;
			break;
		}
	}

	if (!found) {
		return -EMFILE;
	}

	if (fd_id >= fdtable->next_fd) {
		fdtable->next_fd = fd_id + 1;
	}

	auto fd = alloc_fd();
	fd->fdt = fdtable;
	fd->flags = (uint32_t)nonblock_flag; /* carry SOCK_NONBLOCK → O_NONBLOCK */
	fdtable->fd_flags[fd_id] = (uint8_t)cloexec_flag;
	fdtable->fds[fd_id] = fd;
	fd->private_data = socket;

	/* Create /proc/<pid>/fd/<fd_id> dentry entry */
	{
		dentry_ptr proc_dentry;
		if (resolve_dentry("/proc", 0, &proc_dentry, 0) == VFS_OK) {
			dentry_ptr curr_proc_dentry;
			if (resolve_dentry(itoa(proc->pid, 10), proc_dentry,
			                   &curr_proc_dentry,
			                   CREATE_MISSING_ENTRY) == VFS_OK) {
				if (curr_proc_dentry->vnode) {
					curr_proc_dentry->vnode->type = VNODE_TYPE_DIR;
					curr_proc_dentry->vnode->permission = 0555;
				}
				dentry_ptr fd_dentry;
				if (resolve_dentry(itoa(fd_id, 10), curr_proc_dentry,
				                   &fd_dentry,
				                   CREATE_MISSING_ENTRY) == VFS_OK) {
					dentry_put(fd_dentry);
				}
				dentry_put(curr_proc_dentry);
			}
			dentry_put(proc_dentry);
		}
	}

	return (int)fd_id;
}
