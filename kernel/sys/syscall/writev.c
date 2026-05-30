#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

long syscall_writev(int fd, const struct iovec* iov, int iovcnt) {
    auto curr_procc = get_current_core_data()->active_thread->process;
    auto fdt = (struct fdtable*)curr_procc->fdtable;

    if (fd < 0 || fd >= (int)fdt->max_fds) {
        LOG2_ERROR("writev", "fd %d is invalid, max fd %d", fd, fdt->max_fds);
        return -EBADF;
    }

    auto curr_fd = fdt->fds[fd];
    if (!curr_fd || !curr_fd->vnode) {
        LOG2_ERROR("writev", "fd %d vnode is missing", fd);
        return -EBADF;
    }

    auto ops = (vops_file_t*)curr_fd->ops;
    if (!ops || !ops->write) {
        LOG2_ERROR("writev", "fd %d write ops is missing", fd);
        return -ENOTTY;
    }

    if (!iov || iovcnt <= 0)
        return -EINVAL;

    auto iovec_ = (struct iovec*)kalloc(sizeof(struct iovec) * (size_t)iovcnt);
    memcopy(iovec_, (void*)iov, sizeof(struct iovec) * (size_t)iovcnt);

    long total_written = 0;
    for (int i = 0; i < iovcnt; i++) {
        auto iov_ = &iovec_[i];
        if (!iov_->iov_len || !iov_->iov_base)  /* <-- was: iov-> (bug) */
            continue;

        serial2_printf("write: from 0x%x\n", iov_->iov_base);
        long write_count = ops->write(curr_fd->vnode, iov_->iov_base,
                                      (size_t)iov_->iov_len,
                                      (size_t)total_written);
        if (write_count < 0) {
            kfree2(iovec_);
            return write_count;
        }

        total_written += write_count;
    }

    kfree2(iovec_);
    return total_written;
}