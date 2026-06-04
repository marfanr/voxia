#include <str.h>
#include <sys/syscall.h>

int64_t syscall_rt_sigprocmask(int how, void* set, void* oldset, size_t sigsetsize) {
    (void)how;
    (void)set;
    
    if (oldset) {
        memset(oldset, 0, sigsetsize);
    }
    return 0;
}