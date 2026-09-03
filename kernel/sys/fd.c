#include "fd.h"
#include "memory/kalloc.h"
#include <str.h>

struct fdtable* alloc_fdtable() {
	struct fdtable* table = kalloc(sizeof(struct fdtable));
	memset(table, 0, sizeof(struct fdtable));
	table->max_fds = INITIAL_MAX_FDS;
	table->fds = (struct file_descriptor**)kalloc(
	    sizeof(struct file_descriptor*) * table->max_fds);
	memset(table->fds, 0, sizeof(struct file_descriptor*) * table->max_fds);
	table->fd_flags = (uint8_t*)kalloc(table->max_fds);
	memset(table->fd_flags, 0, table->max_fds);
	table->next_fd = 0;
    return table;
}

struct file_descriptor* alloc_fd() {
	struct file_descriptor* fd = kalloc(sizeof(struct file_descriptor));
	memset(fd, 0, sizeof(struct file_descriptor));
	fd->count.counter = 1;
	return fd;
}

#include "vfs/vnode.h"
#include "vfs/dentry.h"

void free_fdtable(struct fdtable* table) {
	if (!table) return;
	for (uint32_t i = 0; i < table->max_fds; i++) {
		auto curr_fd = table->fds[i];
		if (curr_fd) {
			if (__atomic_sub_fetch(&curr_fd->count.counter, 1, __ATOMIC_SEQ_CST) == 0) {
				if (curr_fd->write_buffer && curr_fd->write_buffer_size > 0 && curr_fd->vnode) {
					if (curr_fd->vnode->type == VNODE_TYPE_FILE) {
						auto ops = (vops_file_t*)curr_fd->ops;
						if (ops && ops->write) {
							ops->write(curr_fd->vnode, curr_fd->write_buffer, curr_fd->write_buffer_size, curr_fd->pos - curr_fd->write_buffer_size);
						}
					}
				}
				if (curr_fd->vnode && curr_fd->vnode->type == VNODE_TYPE_FIFO) {
					extern void pipe_close_fd(struct file_descriptor* fd);
					pipe_close_fd(curr_fd);
				}
				if (curr_fd->dentry) {
					dentry_put(curr_fd->dentry);
				}
				kfree2(curr_fd->write_buffer);
				kfree2(curr_fd);
			}
		}
	}
	kfree2(table->fds);
	kfree2(table->fd_flags);
	kfree2(table);
}