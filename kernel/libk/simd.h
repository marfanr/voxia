#ifndef __LIBK__SIMD_H__
#define __LIBK__SIMD_H__

#include <libk/type.h>

// sse
void sse_add_pd(double *dst, const double *a, const double *b);
void sse_sub_pd(double *dst, const double *a, const double *b);
void sse_mul_pd(double *dst, const double *a, const double *b);
void sse_div_pd(double *dst, const double *a, const double *b);

// fma
void fma_mul_add_pd(double *dst, const double *a, const double *b, const double *c);
void fma_mul_sub_pd(double *dst, const double *a, const double *b, const double *c);

#endif // __LIBK__SIMD_H__