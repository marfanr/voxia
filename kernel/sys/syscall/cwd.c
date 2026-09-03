#include <hal/cpu/core.h>
#include <string.h>
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/dentry.h>
#include <vfs/enum.h>
#include <vfs/vfs.h>

int syscall_getcwd(char* buf, size_t size) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	if (!curr_procc || !curr_procc->cwd)
		return -ENOENT;

	if (size == 0 || !buf)
		return -EINVAL;

	auto path = get_full_path_from_dentry(curr_procc->cwd);
	if (!path)
		return -ENOENT;

	size_t len = strlen(path->c_str);
	if (len + 1 > size) {
		str_release(path);
		return -ERANGE;
	}

	strcpy(buf, path->c_str);
	str_release(path);
	return (int)(len + 1);
}

int syscall_chdir(const char* path) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	if (!curr_procc)
		return -ENOENT;

	auto saved_path = safe_str_from_user(curr_procc->page, path);
	if (!saved_path)
		return -EFAULT;

	dentry_ptr base_dir = (curr_procc && saved_path->c_str[0] != '/') ? curr_procc->cwd : 0;
	dentry_ptr out;
	if (resolve_dentry(saved_path->c_str, base_dir, &out, 0) != VFS_OK) {
		str_release(saved_path);
		return -ENOENT;
	}

	str_release(saved_path);

	if (!out->vnode || out->vnode->type != VNODE_TYPE_DIR) {
		dentry_put(out);
		return -ENOTDIR;
	}

	if (curr_procc->cwd) {
		dentry_put(curr_procc->cwd);
	}

	curr_procc->cwd = out;

	return 0;
}

int syscall_fchdir(int fd) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	if (!curr_procc)
		return -ENOENT;

	auto fdt = (struct fdtable*)curr_procc->fdtable;
	if (fd < 0 || fd >= (int)fdt->max_fds || !fdt->fds[fd])
		return -EBADF;

	auto file = fdt->fds[fd];
	if (!file->vnode || file->vnode->type != VNODE_TYPE_DIR || !file->dentry) {
		return -ENOTDIR;
	}

	if (curr_procc->cwd) {
		dentry_put(curr_procc->cwd);
	}

	curr_procc->cwd = file->dentry;
	dentry_get(curr_procc->cwd);

	return 0;
}
