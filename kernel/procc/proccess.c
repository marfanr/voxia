#include "procc/proccess.h"
#include "init/init.h"
#include "libk/str.h"
#include "memory/kalloc.h"
#include "procc/task.h"

static pid_t       increment_pid = 1;
static proccess_t *proccess_list;

INIT(Proccess)
{
}

void
execve(const char *path, char *const *argv, char *const *envp)
{
    // TODO:
    // load path
    // buat proccess_t
    // allocate pml4 baru
    
}