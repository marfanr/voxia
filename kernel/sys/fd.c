#include "fd.h"
#include "memory/kalloc.h"

struct fdtable* alloc_fdtable() {
	struct fdtable* table = kalloc(sizeof(struct fdtable));
	table->max_fds = INITIAL_MAX_FDS;
	table->fds = (struct file_descriptor**)kalloc(
	    sizeof(struct file_descriptor*) * table->max_fds);
	table->next_fd = 0;
    return table;
}

struct file_descriptor* alloc_fd() {
	struct file_descriptor* fd = kalloc(sizeof(struct file_descriptor));
	return fd;
}


void free_fdtable(struct fdtable* table) {
	kfree2(table->fds);
	kfree2(table);
}