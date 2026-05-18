#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include "libk/type.h"
#include <libk/serial.h>
#include <libk/simd.h>

// TODO: move this to each core struct data
boolean_t simd_has_avx = false;
boolean_t simd_has_avx2 = false;

void init_simd() {
	uint32_t eax, ebx, ecx, edx;

	// 1️⃣ Deteksi fitur dasar
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);

	bool has_xsave = (ecx & (1U << 26)) != 0;
	bool has_avx = (ecx & (1U << 28)) != 0;
	bool has_sse = (ecx & (1U << 25)) != 0;
	bool has_oxsave = (ecx & (1U << 27)) != 0;

	// 2️⃣ Deteksi AVX2 (Leaf 7, subleaf 0)
	cpuid(7, 0, &eax, &ebx, &ecx, &edx);
	bool has_avx2 = (ebx & (1U << 5)) != 0;

	if (!has_sse) {
		LOG2_WARN("SIMD", "SSE not supported, SIMD disabled");
		simd_has_avx = false;
		simd_has_avx2 = false;
		return;
	}

	// AVX butuh OSXSAVE juga
	if (has_avx && !has_oxsave)
		has_avx = false;

	if (has_avx2 && (!has_avx || !has_oxsave))
		has_avx2 = false;

	// 3️⃣ Aktifkan FPU dan SSE di CR0 & CR4
	uint64_t cr0_val;
	uint64_t cr4_val;

	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));

	cr0_val &= ~(1ULL << 2); // Clear EM bit
	cr0_val |= (1ULL << 1);  // Set MP bit

	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4_val));

	cr4_val |= (1ULL << 9);  // OSFXSR
	cr4_val |= (1ULL << 10); // OSXMMEXCPT

	if (has_xsave)
		cr4_val |= (1ULL << 18); // OSXSAVE

	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4_val));

	// 4️⃣ Init FPU
	__asm__ volatile("fninit");

	// 5️⃣ Setup XCR0
	if (has_xsave) {
		uint64_t xcr0;

		if (has_avx) {
			// x87 + SSE + AVX
			xcr0 = 0b111ULL;
			simd_has_avx = true;
			simd_has_avx2 = has_avx2;
		} else {
			// x87 + SSE
			xcr0 = 0b011ULL;
			simd_has_avx = false;
			simd_has_avx2 = false;
		}

		__asm__ volatile("xsetbv"
				 :
				 : "a"((uint32_t) xcr0),
				   "d"((uint32_t) (xcr0 >> 32)), "c"(0)
				 : "memory");

	} else {
		LOG2_WARN("SIMD",
			  "XSAVE/AVX not supported, fallback to SSE only");

		simd_has_avx = false;
		simd_has_avx2 = false;
	}

	if (simd_has_avx2) {
		LOG2_INFO("SIMD", "AVX2 enabled");
	} else if (simd_has_avx) {
		LOG2_INFO("SIMD", "AVX enabled");
	}
}

INIT(SIMD) {
	init_simd();
}

void sse_add_pd(double* dst, const double* a, const double* b) {
	asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a
		     "movapd (%2), %%xmm1\n"  // load 2 double dari b
		     "addpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 + xmm1
		     "movapd %%xmm0, (%0)\n"  // store hasil ke dst
		     :
		     : "r"(dst), "r"(a), "r"(b)
		     : "xmm0", "xmm1");
}

void simd_sub_pd(double* dst, const double* a, const double* b) {
	asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a");
		     "movapd (%2), %%xmm1\n"  // load 2 double dari b
		     "subpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 - xmm1
		     "movapd %%xmm0, (%0)\n"  // store hasil ke dst
		     :
		     : "r"(dst), "r"(a), "r"(b)
		     : "xmm0", "xmm1");
}

void simd_mul_pd(double* dst, const double* a, const double* b) {
	asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a");
		     "movapd (%2), %%xmm1\n"  // load 2 double dari b
		     "mulpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 * xmm1
		     "movapd %%xmm0, (%0)\n"  // store hasil ke dst
		     :
		     : "r"(dst), "r"(a), "r"(b)
		     : "xmm0", "xmm1");
}
// void simd_div(double *dst, const double *a, const double *b);

void fma_mul_add_pd(double* dst, const double* a, const double* b,
		    const double* c) {
	asm volatile("vmovapd (%1), %%ymm0\n"		    // load a
		     "vmovapd (%2), %%ymm1\n"		    // load b
		     "vmovapd (%3), %%ymm2\n"		    // load c
		     "vfmadd132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
							    // ymm1) + ymm2
		     "vmovapd %%ymm0, (%0)\n"		    // store result
		     :
		     : "r"(dst), "r"(a), "r"(b), "r"(c)
		     : "ymm0", "ymm1", "ymm2");
}

void fma_mul_sub_pd(double* dst, const double* a, const double* b,
		    const double* c) {
	asm volatile("vmovapd (%1), %%ymm0\n"		    // load a
		     "vmovapd (%2), %%ymm1\n"		    // load b
		     "vmovapd (%3), %%ymm2\n"		    // load c
		     "vfmsub132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
							    // ymm1) - ymm2
		     "vmovapd %%ymm0, (%0)\n"
		     :
		     : "r"(dst), "r"(a), "r"(b), "r"(c)
		     : "ymm0", "ymm1", "ymm2");
}

// void simd_fma_
// }