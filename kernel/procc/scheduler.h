#ifndef __PROCC__SCHEDULER_H__
#define __PROCC__SCHEDULER_H__

#include <hal/cpu/register.h>
#include <libk/type.h>

void scheduler_init();
void scheduler_tick(cpu_register_t *rsp);
int  scheduler_get_current_process_pid();
#endif // __PROCC__SCHEDULER_H__