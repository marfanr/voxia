#ifndef __HAL__TIMER__TIMER_H__
#define __HAL__TIMER__TIMER_H__

#include <type.h>

void usleep(const uint64_t time_ns);
void vxTimerRegisterInterrupt(void);

typedef struct {
	uint64_t current;
} time_counter_t;

void vxTimerCounterInit(time_counter_t* counter);
uint64_t vxTimerCounterCount(time_counter_t* counter);
uint64_t vxTimerCounterCountInMs(time_counter_t* counter);
uint64_t vxTimerCounterCountInNs(time_counter_t* counter);
#endif // __HAL__TIMER__TIMER_H__