#ifndef __PROCC__WORKQUEUE_H__
#define __PROCC__WORKQUEUE_H__

#include <vector.h>
#include <type.h>

typedef struct workqueue* workqueue_ptr_t;
define_vector(workqueue_ptr_t);

typedef struct workqueue {
	void (*function)(void*);
	void* data;
	uint8_t in_use;
	vector(workqueue_ptr_t) * dependency;
} workqueue_t;

workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
				vector(workqueue_ptr_t) * dependency);

#endif // __PROCC__WORKQUEUE_H__