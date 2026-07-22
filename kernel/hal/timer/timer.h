#ifndef __HAL__TIMER__TIMER_H__
#define __HAL__TIMER__TIMER_H__

#include <type.h>

void usleep(const uint64_t time_ns);
void setup_timer_interrupt(void);

typedef struct {
	uint64_t current;
} time_counter_t;

void init_timer_counter(time_counter_t* counter);
uint64_t get_timer_counter_count(time_counter_t* counter);
uint64_t get_timer_counter_count_ms(time_counter_t* counter);
uint64_t get_timer_counter_count_ns(time_counter_t* counter);

#endif // __HAL__TIMER__TIMER_H__