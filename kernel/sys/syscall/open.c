#include "console/console.h"
#include "hal/cpu/core.h"
#include "vfs/dentry.h"
#include <sys/syscall.h>

int syscall_open(const char* path, int flags, int mode) {
    (void)path;
    (void)flags;
    (void)mode;

    // auto thr = get_current_core_data()->active_thread;

    // dentry_ptr out;
    // resolve_dentry((char *)path, 0, &out, (uint8_t)flags);
 
    return -1;
}