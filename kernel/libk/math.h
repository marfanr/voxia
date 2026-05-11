#ifndef __LIBK__MATH_H_
#define __LIBK__MATH_H_

#include <type.h>
#define M_PI 3.14159265358979323846

uint64_t pow(uint64_t base, uint64_t exp);
uint64_t clamp(uint64_t value, uint64_t min, uint64_t max);
uint64_t min(uint64_t a, uint64_t b);
uint64_t max(uint64_t a, uint64_t b);
int64_t abs(int64_t a);
double cos(double x);
double sin(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double sqrt(double x);

float fmaxf(float a, float b);
float fminf(float a, float b);
float fmodf(float a, float b);
float fabsf(float a);
#define ceil(X) (int) X > (float) i ? i + 1 : i

#endif // __LIBK__MATH_H_