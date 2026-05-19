#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint16_t now_serving; /* tiket yang sedang dilayani  */
	uint16_t next_ticket; /* counter untuk waiter baru   */
} spinlock_t;

#define SPINLOCK_INIT                                                          \
	{ .now_serving = 0, .next_ticket = 0 }

void spin_acquire(spinlock_t* lock);
void spin_release(spinlock_t* lock);
bool spin_is_locked(const spinlock_t* lock);

#ifdef __cplusplus
}
#endif

#endif // __SPINLOCK_H__