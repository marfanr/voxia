#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "string.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include "vfs/vfs.h"
#include <sys/syscall.h>

int syscall_mount(const char* source, const char* target, const char* fstype,
                  unsigned long flags, const void* data) {
	(void)flags;
	(void)data;

	if (!source || !target || !fstype)
		return -EINVAL;

	auto proc = get_current_core_data()->active_thread->process;

	auto safe_source = safe_str_from_user(proc->page, source);
	auto safe_target = safe_str_from_user(proc->page, target);
	auto safe_fstype = safe_str_from_user(proc->page, fstype);

	serial2_printf("syscall_mount: %s %s %s\n", safe_source->c_str,
	               safe_target->c_str, safe_fstype->c_str);

	dentry_ptr source_dentry;
	if (resolve_dentry(safe_source->c_str, 0, &source_dentry, 0) !=
	    VFS_OK) {
		str_release(safe_source);
		str_release(safe_target);
		str_release(safe_fstype);
		return -ENOENT;
	}
	serial2_printf("source_dentry: %s\n", source_dentry->name->c_str);

	dentry_ptr target_dentry;
	if (resolve_dentry(safe_target->c_str, 0, &target_dentry, 0) != VFS_OK) {
		str_release(safe_source);
		str_release(safe_target);
		str_release(safe_fstype);
		dentry_put(source_dentry);
		return -ENOENT;
	}
	serial2_printf("target_dentry: %s\n", target_dentry->name->c_str);

	// TODO: handle flags and data

	if (vfs_mount(source_dentry, safe_fstype->c_str, target_dentry, 0) != VFS_OK) {
		str_release(safe_source);
		str_release(safe_target);
		str_release(safe_fstype);
		dentry_put(source_dentry);
		dentry_put(target_dentry);
		return -ENOENT;
	}

	dentry_put(target_dentry);
	dentry_put(source_dentry);
	str_release(safe_source);
	str_release(safe_target);
	str_release(safe_fstype);

	print_dentry_tree(get_root_dentry(), 0);
	return 0;
}