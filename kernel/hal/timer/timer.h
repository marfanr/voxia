#ifndef __HAL__TIMER__TIMER_H__
#define __HAL__TIMER__TIMER_H__

#include <libk/type.h>

void usleep(const double time_ns);

typedef struct
{
    uint64_t current;
} time_counter_t;

void     vxTimerCounterInit(time_counter_t *counter);
uint64_t vxTimerCounterCount(time_counter_t *counter);
double   vxTimerCounterCountInMs(time_counter_t *counter);
double   vxTimerCounterCountInNs(time_counter_t *counter);
#endif // __HAL__TIMER__TIMER_H__