#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "string.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include "vfs/vfs.h"
#include <sys/syscall.h>

int syscall_unmount(const char* target, int flags){
	(void)flags;

	if (!target)
		return -EINVAL;

	auto proc = get_current_core_data()->active_thread->process;

	auto safe_target = safe_str_from_user(proc->page, target);

	dentry_ptr target_dentry;
	if (resolve_dentry(safe_target->c_str, 0, &target_dentry, 0) != VFS_OK) {
		str_release(safe_target);
		return -ENOENT;
	}

	if (vfs_umount(target_dentry) != VFS_OK) {
		str_release(safe_target);
		dentry_put(target_dentry);
		return -ENOENT;
	}

	dentry_put(target_dentry);	
	str_release(safe_target);
	print_dentry_tree(get_root_dentry(), 0);
	return 0;
}