#include "procc/proccess.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "procc/task.h"
#include "type.h"
#include "vfs/dentry.h"
#include <str.h>

// static pid_t increment_pid = 1;
// static proccess_t* proccess_list;

INIT(Proccess) {}

void execve(const char* path, char* const* argv, char* const* envp) {
	UNUSED(path);
	UNUSED(argv);
	UNUSED(envp);

	serial2_printf("exec proccess %s ... \n", path);

	dentry_ptr out;
	resolve_dentry((char*)path, 0, &out, 0);

	if (out) {
		serial2_printf("found %s\n", out->name->c_str);
	}
	// 	// TODO:
	// 	// load path
	// 	// buat proccess_t
	// 	// allocate pml4 baru
}