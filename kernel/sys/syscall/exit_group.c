#include "hal/cpu/core.h"
#include "libk/serial.h"
#include <sys/syscall.h>

void syscall_exit_group(int status) {
    auto thr = get_current_core_data()->active_thread;
    if (!thr) {
        return;
    }

    serial2_printf("exit_group: status code %d\n", status);

    auto procc = thr->process;
    if (!procc) {
        return;
    }


}