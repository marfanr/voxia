#ifndef __LIBK__SIMD_H__
#define __LIBK__SIMD_H__

#include "hal/cpu/core.h"

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

#define SIMD_HAS_AVX (get_current_core_data() && get_current_core_data()->simd_has_avx)
#define SIMD_HAS_AVX2 (get_current_core_data() && get_current_core_data()->simd_has_avx2)

void kernel_fpu_begin(void);
void kernel_fpu_end(void);

#endif // __LIBK__SIMD_H__