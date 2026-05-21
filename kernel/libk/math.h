#ifndef __LIBK__MATH_H_
#define __LIBK__MATH_H_

#include <type.h>
#define M_PI 3.14159265358979323846

uint64_t pow(uint64_t base, uint64_t exp);
uint64_t clamp(uint64_t value, uint64_t min, uint64_t max);
uint64_t min(uint64_t a, uint64_t b);
uint64_t max(uint64_t a, uint64_t b);
int64_t abs(int64_t a);

#endif // __LIBK__MATH_H_