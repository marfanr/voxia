#ifndef __HAL__CPU__CORE_H__
#define __HAL__CPU__CORE_H__

#include "autoconf.h"
#include "libk/type.h"
#include "procc/scheduler.h"
#include "procc/workqueue.h"

typedef struct
{
    uint16_t          core_id;
    boolean_t         usleep_trigerred;
    scheduler_core_t *scheduler;
    workqueue_t       workqueue[VOXIA_MAX_WORKQUEUE_EACH_CORE];
    uint32_t          workqueue_count;
} each_core_data;

void            coreUpdateGs(uint16_t id);
uint16_t        coreGetCpuID();
each_core_data *vxGetCoreData();
each_core_data *vxGetCoreDataByCoreID(uint16_t core_id);

#endif // __HAL__CPU__CORE_H__