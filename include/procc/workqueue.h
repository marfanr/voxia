#ifndef __PROCC__WORKQUEUE_H__
#define __PROCC__WORKQUEUE_H__

#include <vector.h>
#include <type.h>

typedef struct workqueue* workqueue_ptr_t;
define_vector(workqueue_ptr_t);

#define SLOT_EMPTY 0x00
#define SLOT_BUSY 0xFF
#define SLOT_FINISHED 0xFE


#ifdef __cplusplus
extern "C" {
#endif


typedef struct workqueue {
	void (*function)(void*);
	void* data;
	uint8_t in_use;
	uint32_t refcount;
	vector(workqueue_ptr_t) * dependency;
	struct workqueue* next;
	struct workqueue* prev;
} workqueue_t;

workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
				vector(workqueue_ptr_t) * dependency);

#ifdef __cplusplus
}
#endif

#endif // __PROCC__WORKQUEUE_H__