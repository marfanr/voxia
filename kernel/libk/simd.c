#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include "libk/type.h"
#include <libk/serial.h>
#include <libk/simd.h>

boolean_t simd_has_avx = false;
INIT(SIMD) {
	uint32_t eax, ebx, ecx, edx;

	// 1️⃣ Deteksi fitur dasar
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);

	bool has_xsave = ecx & (1 << 26);
	bool has_avx = ecx & (1 << 28);
	bool has_sse = ecx & (1 << 25);
	bool has_oxsave = ecx & (1 << 27);

	if (!has_sse) {
		LOG2_WARN("SIMD", "SSE not supported, SIMD disabled");
		simd_has_avx = false;
		return;
	}

	// 2️⃣ Aktifkan FPU dan SSE di CR0 & CR4
	uint64_t cr0, cr4;

	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 &= ~(1 << 2); // Clear EM bit (disable FPU emulation)
	cr0 |= (1 << 1);  // Set MP bit (monitor co-processor)
	__asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1 << 9);  // OSFXSR - enable FXSAVE/FXRSTOR for SSE
	cr4 |= (1 << 10); // OSXMMEXCPT - enable SSE exceptions
	if (has_xsave)
		cr4 |= (1 << 18); // OSXSAVE - enable XSAVE/XRSTOR for AVX
	__asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

	// 3️⃣ Inisialisasi FPU
	__asm__ volatile("fninit");
	// LOG2_INFO("SIMD", "FPU initialized");

	// 4️⃣ Jika CPU dukung AVX + XSAVE, aktifkan lewat XCR0

	if (has_xsave && has_avx) {
		uint64_t cr4;
		asm volatile("mov %%cr4, %0" : "=r"(cr4));
		cr4 |= (1 << 18); // OSXSAVE
		asm volatile("mov %0, %%cr4" ::"r"(cr4));

		uint64_t xcr0 = 0b111; // x87 + SSE + AVX
		asm volatile("xsetbv" ::"a"((uint32_t)xcr0),
		             "d"((uint32_t)(xcr0 >> 32)), "c"(0));

		// LOG2_INFO("SIMD", "AVX OSX initialized via XCR0");
		simd_has_avx = true;
	} else if (has_xsave && !has_avx) {
		uint64_t xcr0 = 0b11; // x87 + SSE only
		__asm__ volatile("xsetbv"
		                 :
		                 : "a"((uint32_t)xcr0),
		                   "d"((uint32_t)(xcr0 >> 32)), "c"(0));
		// LOG2_INFO("SIMD", "SSE initialized via XCR0 (no AVX)");
	} else {
		LOG2_WARN("SIMD",
		          "XSAVE/AVX not supported, fallback to SSE only");
	}

	// LOG2_INFO("SIMD", "SIMD (SSE/AVX) enabled successfully");
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
	asm volatile("vmovapd (%1), %%ymm0\n"               // load a
	             "vmovapd (%2), %%ymm1\n"               // load b
	             "vmovapd (%3), %%ymm2\n"               // load c
	             "vfmadd132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
	                                                    // ymm1) + ymm2
	             "vmovapd %%ymm0, (%0)\n"               // store result
	             :
	             : "r"(dst), "r"(a), "r"(b), "r"(c)
	             : "ymm0", "ymm1", "ymm2");
}

void fma_mul_sub_pd(double* dst, const double* a, const double* b,
                    const double* c) {
	asm volatile("vmovapd (%1), %%ymm0\n"               // load a
	             "vmovapd (%2), %%ymm1\n"               // load b
	             "vmovapd (%3), %%ymm2\n"               // load c
	             "vfmsub132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
	                                                    // ymm1) - ymm2
	             "vmovapd %%ymm0, (%0)\n"
	             :
	             : "r"(dst), "r"(a), "r"(b), "r"(c)
	             : "ymm0", "ymm1", "ymm2");
}

// void simd_fma_
// }