#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include <sys/syscall.h>

// following musl
#include "sys/err_no.h"
// following musl
#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_FD		2
#define FUTEX_REQUEUE		3
#define FUTEX_CMP_REQUEUE	4
#define FUTEX_WAKE_OP		5
#define FUTEX_LOCK_PI		6
#define FUTEX_UNLOCK_PI		7
#define FUTEX_TRYLOCK_PI	8
#define FUTEX_WAIT_BITSET	9
#define FUTEX_PRIVATE_FLAG 128

#define MAX_FUTEX 1024

struct futex_waiter {
    uintptr_t uaddr;
    thread_t* thread;
};

static struct futex_waiter futex_queue[MAX_FUTEX];
static spinlock_t futex_lock = {0};

int syscall_futex(int* uaddr, int futex_op, int val, const void* timeout, int* uaddr2, int val3) {
    int cmd = futex_op & ~FUTEX_PRIVATE_FLAG;
    auto thr = get_current_core_data()->active_thread;
    
    if (cmd == FUTEX_WAIT) {
        thr->wake_pending = false; // Reset before acquiring lock to avoid missing wakes
        spin_acquire(&futex_lock);
        
        if (*uaddr != val) {
            spin_release(&futex_lock);
            return -EAGAIN;
        }
        
        // Ensure we are not already in the queue
        bool queued = false;
        struct futex_waiter* my_waiter = NULL;
        for (int i = 0; i < MAX_FUTEX; i++) {
            if (futex_queue[i].thread == thr) {
                queued = true;
                my_waiter = &futex_queue[i];
                break;
            }
        }
        
        if (!queued) {
            for (int i = 0; i < MAX_FUTEX; i++) {
                if (futex_queue[i].thread == NULL) {
                    futex_queue[i].uaddr = (uintptr_t)uaddr;
                    futex_queue[i].thread = thr;
                    my_waiter = &futex_queue[i];
                    queued = true;
                    break;
                }
            }
        }
        
        if (!queued) {
            spin_release(&futex_lock);
            serial2_printf("Futex queue full!\n");
            return -ENOMEM;
        }
        
        spin_release(&futex_lock);

        if (timeout) {
            struct timespec safe_req;
            memcopy(&safe_req, (void *)timeout, sizeof(struct timespec));
            uint64_t total_ns = (uint64_t)safe_req.tv_sec * 1000000000 + (uint64_t)safe_req.tv_nsec;
            uint64_t ms = total_ns / 1000000;
            
            if (ms > 0) {
                thread_sleep(ms);
            } else {
                // thr->state = THREAD_STATE_BLOCKED;
                // schedule_yield();
                thread_block();
            }
            
            // Check if awakened by timeout or by futex wake
            if (!thr->wake_pending) {
                spin_acquire(&futex_lock);
                if (my_waiter && my_waiter->thread == thr) {
                    my_waiter->thread = NULL;
                    my_waiter->uaddr = 0;
                }
                spin_release(&futex_lock);
                return -ETIMEDOUT;
            }
        } else {
            thread_block();
        }
        
        // Cleanup self from queue if not woken up by WAKE properly
        spin_acquire(&futex_lock);
        if (my_waiter && my_waiter->thread == thr) {
            my_waiter->thread = NULL;
            my_waiter->uaddr = 0;
        }
        spin_release(&futex_lock);
        
        return 0;
    } 
    else if (cmd == FUTEX_WAKE) {
        spin_acquire(&futex_lock);
        int awoken = 0;
        
        for (int i = 0; i < MAX_FUTEX; i++) {
            if (futex_queue[i].thread != NULL && futex_queue[i].uaddr == (uintptr_t)uaddr) {
                vxThreadWake(futex_queue[i].thread);
                futex_queue[i].thread = NULL;
                futex_queue[i].uaddr = 0;
                awoken++;
                if (awoken >= val) {
                    break;
                }
            }
        }
        
        spin_release(&futex_lock);
        return awoken;
    }
    else if (cmd == FUTEX_CMP_REQUEUE || cmd == FUTEX_REQUEUE) {
        spin_acquire(&futex_lock);
        
        if (cmd == FUTEX_CMP_REQUEUE) {
            if (*uaddr != val3) {
                spin_release(&futex_lock);
                return -EAGAIN;
            }
        }

        int val2 = (int)(uintptr_t)timeout; // val2 is passed through timeout parameter
        int awoken = 0;
        int requeued = 0;

        for (int i = 0; i < MAX_FUTEX; i++) {
            if (futex_queue[i].thread != NULL && futex_queue[i].uaddr == (uintptr_t)uaddr) {
                if (awoken < val) {
                    vxThreadWake(futex_queue[i].thread);
                    futex_queue[i].thread = NULL;
                    futex_queue[i].uaddr = 0;
                    awoken++;
                } else if (requeued < val2) {
                    // Move it to the new address queue uaddr2
                    futex_queue[i].uaddr = (uintptr_t)uaddr2;
                    requeued++;
                }
            }
        }
        
        spin_release(&futex_lock);
        return awoken + requeued;
    }

    return -ENOSYS;
}
