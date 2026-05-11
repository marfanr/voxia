// Copyright (c) 2025 Mohammad Arfan

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef __HAL__CPU__CORE_H__
#define __HAL__CPU__CORE_H__

#include "autoconf.h"
#include "libk/type.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "procc/workqueue.h"

typedef struct {
	uint16_t core_id;
	boolean_t usleep_trigerred;
	scheduler_core_t* scheduler;
	workqueue_t workqueue[VOXIA_MAX_WORKQUEUE_EACH_CORE];
	uint32_t workqueue_count;
	thread_t* active_thread;
} each_core_data;

void coreUpdateGs(uint16_t id);
uint16_t coreGetCpuID();
each_core_data* vxGetCoreData();
each_core_data* vxGetCoreDataByCoreID(uint16_t core_id);
int vxGetActiveCoreCount();

#endif // __HAL__CPU__CORE_H__