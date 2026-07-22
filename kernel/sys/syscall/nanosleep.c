#include "hal/acpi/hpet.h"
#include "hal/cpu/core.h"
#include "procc/sched.h"
#include "sys/err_no.h"
#include <sys/syscall.h>

 int syscall_nanosleep(const struct timespec* req, struct timespec* rem) {
        if (!req)
            return -EINVAL;
    
        auto curr_procc = get_current_core_data()->active_thread->process;
        if (!curr_procc)
            return -EINVAL;
    
        if (!is_valid_user_pointer(curr_procc->page, req, sizeof(struct timespec)))
            return -EFAULT;
    
        if (rem && !is_valid_user_pointer(curr_procc->page, rem, sizeof(struct timespec)))
            return -EFAULT;
    
        // calculate remaining time
        struct timespec safe_req;
        memcopy(&safe_req, (void *)req, sizeof(struct timespec));
    
        if (safe_req.tv_sec < 0 || safe_req.tv_nsec < 0 || safe_req.tv_nsec >= 1000000000)
            return -EINVAL;
    
        uint64_t total_ns = (uint64_t)safe_req.tv_sec * 1000000000 + (uint64_t)safe_req.tv_nsec;
        uint64_t start_ns = vxHPETGetMainCount() * vxHPETMinTickNs();
        uint64_t end_ns = start_ns + total_ns;
    
        uint64_t ms = total_ns / 1000000;
        if (ms > 0) {
            thread_sleep(ms);
        }

        while (1) {
            uint64_t now = vxHPETGetMainCount() * vxHPETMinTickNs();
            if (now >= end_ns) break;
            
            uint64_t remaining_ns = end_ns - now;
            if (remaining_ns >= 2000000) {
                thread_sleep(remaining_ns / 1000000);
            } else if (remaining_ns >= 500000) {
                schedule_yield();
            } else {
                asm volatile("pause");
            }
        }

        uint64_t elapsed_ns = vxHPETGetMainCount() * vxHPETMinTickNs() - start_ns;
        if (rem) {
            struct timespec safe_rem;
            if (elapsed_ns >= total_ns) {
                safe_rem.tv_sec = 0;
                safe_rem.tv_nsec = 0;
            } else {
                uint64_t remaining_ns = total_ns - elapsed_ns;
                safe_rem.tv_sec = remaining_ns / 1000000000;
                safe_rem.tv_nsec = remaining_ns % 1000000000;
            }
            memcopy((void *)rem, &safe_rem, sizeof(struct timespec));
        }
        
        return 0;
    }