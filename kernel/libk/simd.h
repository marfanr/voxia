#ifndef __LIBK__SIMD_H__
#define __LIBK__SIMD_H__

#include <type.h>

// sse
void sse_add_pd(double* dst, const double* a, const double* b);
void sse_sub_pd(double* dst, const double* a, const double* b);
void sse_mul_pd(double* dst, const double* a, const double* b);
void sse_div_pd(double* dst, const double* a, const double* b);

// fma
void fma_mul_add_pd(double* dst, const double* a, const double* b,
		    const double* c);
void fma_mul_sub_pd(double* dst, const double* a, const double* b,
		    const double* c);
void simd_sub_pd(double* dst, const double* a, const double* b);
void simd_mul_pd(double* dst, const double* a, const double* b);

void init_simd();

extern boolean_t simd_has_avx;
extern boolean_t simd_has_avx2;

#endif // __LIBK__SIMD_H__