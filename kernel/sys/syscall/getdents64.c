#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "str.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/filesystem.h>

int syscall_getdents64(int fd, void* buf, int size) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	if (!curr_procc) {
		return -1;
	}

	auto fdt = (struct fdtable*)curr_procc->fdtable;
	if (fd < 0 || fd >= (int)fdt->max_fds) {
		return -EBADF;
	}

	if (size < 0) {
		return -EINVAL;
	}

	if (!size) {
		return 0;
	}

	if (!is_valid_user_pointer(curr_procc->page, buf, (size_t)size)) {
		return -EFAULT;
	}

	auto safe_buf = (struct dirent*)kalloc((size_t)size);
	if (!safe_buf) {
		return -ENOMEM;
	}

	auto current_file = fdt->fds[fd];
	if (!current_file || !current_file->vnode) {
		kfree2(safe_buf);
		return -EBADF;
	}

	auto vnode = current_file->vnode;
	if (vnode->type != VNODE_TYPE_DIR) {
		kfree2(safe_buf);
		return -ENOTDIR;
	}

	auto ops = (vops_file_t*)vnode->ops;
	if (ops) {
		if (ops->readdir) {
			int bytes = ops->readdir(vnode, safe_buf, (size_t)size, &current_file->pos);
			if (bytes >= 0) {
				uintptr_t current_cr3 = 0;
				asm volatile("mov %%cr3, %0"
				             : "=r"(current_cr3));
				if (current_cr3 != (uintptr_t)curr_procc->page)
					paging_reload(curr_procc->page);

				memcopy(buf, safe_buf, (size_t)bytes);

				if (current_cr3 != (uintptr_t)curr_procc->page)
					paging_reload((page_t)current_cr3);

				kfree2(safe_buf);
				return bytes;
			}
		}
	}

	// TODO: handle getdents on filesystem

	int bytes_written = 0;
	int index = (int)current_file->pos;

	while (1) {
		uint64_t ino = 0;
		uint8_t d_type = DT_UNKNOWN;
		const char* name = NULL;
		size_t name_len = 0;

		// Entry index 0: "."
		if (index == 0) {
			ino = vnode->id;
			d_type = DT_DIR;
			name = ".";
			name_len = 1;
		}
		// Entry index 1: ".."
		else if (index == 1) {
			if (current_file->dentry->parent &&
			    current_file->dentry->parent->vnode) {
				ino = current_file->dentry->parent->vnode->id;
			} else {
				ino = vnode->id;
			}
			d_type = DT_DIR;
			name = "..";
			name_len = 2;
		}
		// Entry index 2+: Sub-entries / Children
		else {
			int target_idx = index - 2;
			int cur_idx = 0;
			struct llist_head* ch =
			    current_file->dentry->child_list.next;

			// Cari anak (dentry) ke-(index - 2) di child_list
			while (ch != &current_file->dentry->child_list &&
			       cur_idx < target_idx) {
				ch = ch->next;
				cur_idx++;
			}

			// Jika sudah mencapai akhir daftar anak
			if (ch == &current_file->dentry->child_list) {
				break;
			}

			dentry_t* child = container_of(ch, dentry_t, siblings);
			if (child->vnode) {
				ino = child->vnode->id;
				// Konversi tipe vnode menjadi tipe dirent
				switch (child->vnode->type) {
				case VNODE_TYPE_DIR:
					d_type = DT_DIR;
					break;
				case VNODE_TYPE_FILE:
					d_type = DT_REG;
					break;
				case VNODE_TYPE_LNK:
					d_type = DT_LNK;
					break;
				case VNODE_TYPE_FIFO:
					d_type = DT_FIFO;
					break;
				case VNODE_TYPE_CHR:
					d_type = DT_CHR;
					break;
				case VNODE_TYPE_BLK:
					d_type = DT_BLK;
					break;
				case VNODE_TYPE_SOCK:
					d_type = DT_SOCK;
					break;
				default:
					d_type = DT_UNKNOWN;
					break;
				}
			} else {
				ino = 0;
				d_type = DT_UNKNOWN;
			}

			if (child->name) {
				name = child->name->c_str;
				name_len = child->name->len;
			} else {
				name = "";
				name_len = 0;
			}
		}

		
		size_t reclen = offsetof(struct dirent, d_name) + name_len + 1;
		reclen = ALIGN_UP(reclen, 8);

		if ((size_t)bytes_written + reclen > (size_t)size) {
			if (bytes_written == 0) {
				kfree2(safe_buf);
				return -EINVAL;
			}
			break;
		}

		auto entry =
		    (struct dirent*)(void*)((char*)safe_buf + bytes_written);
		entry->d_ino = ino;
		entry->d_off = (uint64_t)(index + 1);
		entry->d_reclen = (uint16_t)reclen;
		entry->d_type = d_type;

		if (name_len > 0) {
			strncpy(entry->d_name, name, name_len);
		}
		entry->d_name[name_len] = '\0';

		bytes_written += (int)reclen;
		index++;
	}

	current_file->pos = (uint64_t)index;

	// flush
	uintptr_t current_cr3 = 0;
	asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
	if (current_cr3 != (uintptr_t)curr_procc->page)
		paging_reload(curr_procc->page);

	memcopy(buf, safe_buf, (size_t)bytes_written);

	if (current_cr3 != (uintptr_t)curr_procc->page)
		paging_reload((page_t)current_cr3);

	serial2_printf("flushed getdens\n");

	kfree2(safe_buf);
	return bytes_written;
}