#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include "procc/thread.h"
#include <libk/serial.h>
#include <libk/simd.h>
#include <type.h>

uint32_t g_xsave_size = 512; // Default size for legacy fxsave

void init_simd() {
	uint32_t eax, ebx, ecx, edx;
	each_core_data* core = get_current_core_data();

	cpuid(1, 0, &eax, &ebx, &ecx, &edx);

	bool has_xsave = (ecx & (1U << 26)) != 0;
	bool has_avx = (ecx & (1U << 28)) != 0;
	bool has_sse = (edx & (1U << 25)) != 0;

	cpuid(7, 0, &eax, &ebx, &ecx, &edx);
	bool has_avx2 = (ebx & (1U << 5)) != 0;

	if (!has_sse) {
		LOG2_INFO("SIMD", "SSE not supported, SIMD disabled");
		if (core) {
			core->simd_has_avx = false;
			core->simd_has_avx2 = false;
		}
		return;
	}

	if (has_avx && !has_xsave)
		has_avx = false;

	if (has_avx2 && !has_avx)
		has_avx2 = false;

	uint64_t cr0_val;
	uint64_t cr4_val;

	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));

	cr0_val &= ~(1ULL << 2); // Clear EM bit
	cr0_val |= (1ULL << 1);  // Set MP bit

	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4_val));

	cr4_val |= (1ULL << 7);   // PGE
	cr4_val |= (1ULL << 9);  // OSFXSR
	cr4_val |= (1ULL << 10); // OSXMMEXCPT

	if (has_xsave)
		cr4_val |= (1ULL << 18); // OSXSAVE

	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4_val));

	// Init FPU
	__asm__ volatile("fninit");

	// Setup XCR0
	if (has_xsave) {
		// Verify OSXSAVE was actually set in CR4
		uint64_t current_cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(current_cr4));

		if (current_cr4 & (1ULL << 18)) {
			uint32_t xcr0_low = 0, xsave_ebx = 0, xsave_ecx = 0,
			         xcr0_high = 0;
			cpuid(0xD, 0, &xcr0_low, &xsave_ebx, &xsave_ecx,
			      &xcr0_high);

			// Enable states supported by CPU (x87=bit0, SSE=bit1,
			// AVX=bit2)
			uint64_t xcr0 =
			    xcr0_low &
			    0b11ULL; // default to x87 + SSE if supported

			if (has_avx && (xcr0_low & (1ULL << 2))) {
				xcr0 |= (1ULL << 2);
				if (core) {
					core->simd_has_avx = true;
					core->simd_has_avx2 = has_avx2;
				}
			} else {
				if (core) {
					core->simd_has_avx = false;
					core->simd_has_avx2 = false;
				}
			}

			if (xcr0 != 0) {
				__asm__ volatile("xsetbv"
				                 :
				                 : "a"((uint32_t)xcr0),
				                   "d"((uint32_t)(xcr0 >> 32)),
				                   "c"(0)
				                 : "memory");

				// Re-query cpuid to get the active maximum size
				// in ECX after updating XCR0
				cpuid(0xD, 0, &xcr0_low, &xsave_ebx, &xsave_ecx,
				      &xcr0_high);
				if (xsave_ecx > 512) {
					g_xsave_size = xsave_ecx;
				}
			}
		} else {
			LOG2_INFO("SIMD", "OSXSAVE bit could not be set in CR4, fallback to SSE only");
			if (core) {
				core->simd_has_avx = false;
				core->simd_has_avx2 = false;
			}
		}
	} else {
		LOG2_INFO("SIMD", "XSAVE/AVX not supported, fallback to SSE only");

		if (core) {
			core->simd_has_avx = false;
			core->simd_has_avx2 = false;
		}
	}

	if (core) {
		if (core->simd_has_avx2) {
			LOG2_INFO("SIMD", "AVX2 enabled");
		} else if (core->simd_has_avx) {
			LOG2_INFO("SIMD", "AVX enabled");
		} else {
			LOG2_INFO("SIMD", "SSE enabled");
		}
	} else {
		LOG2_INFO("SIMD", "SIMD enabled (no core data)");
	}
}

INIT(SIMD) {
	serial2_printf("init simd\n");
	init_simd();
}

void sse_add_pd(double* dst, const double* a, const double* b) {
	asm volatile(
	    "movupd (%1), %%xmm0\n"  // load 2 double from a (unaligned safe)
	    "movupd (%2), %%xmm1\n"  // load 2 double from b (unaligned safe)
	    "addpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 + xmm1
	    "movupd %%xmm0, (%0)\n"  // store result to dst
	    :
	    : "r"(dst), "r"(a), "r"(b)
	    : "xmm0", "xmm1", "memory");
}

void sse_sub_pd(double* dst, const double* a, const double* b) {
	asm volatile(
	    "movupd (%1), %%xmm0\n"  // load 2 double from a (unaligned safe)
	    "movupd (%2), %%xmm1\n"  // load 2 double from b (unaligned safe)
	    "subpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 - xmm1
	    "movupd %%xmm0, (%0)\n"  // store result to dst
	    :
	    : "r"(dst), "r"(a), "r"(b)
	    : "xmm0", "xmm1", "memory");
}

void sse_mul_pd(double* dst, const double* a, const double* b) {
	asm volatile(
	    "movupd (%1), %%xmm0\n"  // load 2 double from a (unaligned safe)
	    "movupd (%2), %%xmm1\n"  // load 2 double from b (unaligned safe)
	    "mulpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 * xmm1
	    "movupd %%xmm0, (%0)\n"  // store result to dst
	    :
	    : "r"(dst), "r"(a), "r"(b)
	    : "xmm0", "xmm1", "memory");
}

void sse_div_pd(double* dst, const double* a, const double* b) {
	asm volatile(
	    "movupd (%1), %%xmm0\n"  // load 2 double from a (unaligned safe)
	    "movupd (%2), %%xmm1\n"  // load 2 double from b (unaligned safe)
	    "divpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 / xmm1
	    "movupd %%xmm0, (%0)\n"  // store result to dst
	    :
	    : "r"(dst), "r"(a), "r"(b)
	    : "xmm0", "xmm1", "memory");
}

void simd_sub_pd(double* dst, const double* a, const double* b) {
	sse_sub_pd(dst, a, b);
}

void simd_mul_pd(double* dst, const double* a, const double* b) {
	sse_mul_pd(dst, a, b);
}

void fma_mul_add_pd(double* dst, const double* a, const double* b,
                    const double* c) {
	asm volatile("vmovupd (%1), %%ymm0\n" // load a (unaligned safe)
	             "vmovupd (%2), %%ymm1\n" // load b (unaligned safe)
	             "vmovupd (%3), %%ymm2\n" // load c (unaligned safe)
	             "vfmadd213pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
	                                                    // ymm1) + ymm2
	             "vmovupd %%ymm0, (%0)\n"               // store result
	             :
	             : "r"(dst), "r"(a), "r"(b), "r"(c)
	             : "xmm0", "xmm1", "xmm2", "memory");
}

void fma_mul_sub_pd(double* dst, const double* a, const double* b,
                    const double* c) {
	asm volatile("vmovupd (%1), %%ymm0\n" // load a (unaligned safe)
	             "vmovupd (%2), %%ymm1\n" // load b (unaligned safe)
	             "vmovupd (%3), %%ymm2\n" // load c (unaligned safe)
	             "vfmsub213pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 *
	                                                    // ymm1) - ymm2
	             "vmovupd %%ymm0, (%0)\n"               // store result
	             :
	             : "r"(dst), "r"(a), "r"(b), "r"(c)
	             : "xmm0", "xmm1", "xmm2", "memory");
}

void kernel_fpu_begin(void) {
	each_core_data* current_core = get_current_core_data();
	if (!current_core)
		return;
	thread_t* current = (thread_t*)current_core->active_thread;

	if (current && current->fpu_state) {
		uint64_t cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
		if (cr4 & (1ULL << 18)) {
			__asm__ volatile("xor %%ecx, %%ecx\n\t"
			                 "xgetbv\n\t"
			                 "xsave64 (%0)"
			                 :
			                 : "r"(current->fpu_state)
			                 : "eax", "ecx", "edx", "memory");
		} else {
			__asm__ volatile("fxsave64 (%0)"
			                 :
			                 : "r"(current->fpu_state)
			                 : "memory");
		}
	}
}

void kernel_fpu_end(void) {
	each_core_data* current_core = get_current_core_data();
	if (!current_core)
		return;
	thread_t* current = (thread_t*)current_core->active_thread;

	if (current && current->fpu_state) {
		uint64_t cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
		if (cr4 & (1ULL << 18)) {
			__asm__ volatile("xor %%ecx, %%ecx\n\t"
			                 "xgetbv\n\t"
			                 "xrstor64 (%0)"
			                 :
			                 : "r"(current->fpu_state)
			                 : "eax", "ecx", "edx", "memory");
		} else {
			__asm__ volatile("fxrstor64 (%0)"
			                 :
			                 : "r"(current->fpu_state)
			                 : "memory");
		}
	}
}