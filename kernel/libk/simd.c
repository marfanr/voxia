#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include <libk/serial.h>
#include <libk/simd.h>

INIT(simd)
{

    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);

    if (edx & (1 << 0))
        LOG_INFO("FPU", "fpu available");
    if (edx & (1 << 25)) // bit 25: SSE
        LOG_INFO("SSE", "sse available");
    if (edx & (1 << 26)) // bit 26: SSE2
        LOG_INFO("SSE2", "sse2 available");
    if (ecx & (1 << 0)) // bit 0 di ECX: SSE3
        LOG_INFO("SSE3", "sse3 available");
    if (ecx & (1 << 28)) // bit 28: AVX
        LOG_INFO("AVX", "avx available");
    if (ecx & (1 << 29)) // bit 2
        LOG_INFO("AVX2", "avx2 available");
    if (ecx & (1 << 30)) // bit 3
        LOG_INFO("BMI1", "bmi1 available");
    if (((ecx >> 12) & 1) == 1)
        LOG_INFO("FMA", "fma available");

    // 2️⃣ Aktifkan FPU dan SSE di CR0 & CR4
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & (1 << 2)))
        serial_trace("✅ EM=0 → FPU enabled\n");
    else
        serial_trace("❌ EM=1 → FPU disabled\n");
    cr0 &= ~(1 << 2); // Clear EM (bit 2) -> enable FPU
    cr0 |= (1 << 1);  // Set MP (bit 1)
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // OSFXSR: enable SSE support
    cr4 |= (1 << 10); // OSXMMEXCPT: enable SSE exceptions
    cr4 |= (1 << 18); // OSXSAVE : enable AVX and OSX
    __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

    // 3️⃣ Inisialisasi FPU
    __asm__ volatile("fninit");
    LOG_INFO("CPU", "FPU initialized");

    // avx
    uint64_t xcr0 = 0b111; // x87 + SSE + AVX
    asm volatile("xsetbv" ::"a"((uint32_t)xcr0), "d"((uint32_t)(xcr0 >> 32)), "c"(0));
    LOG_INFO("AVX", "avx initialized");

    // fma
    LOG_INFO("FMA", "fma initialized");
}

void
sse_add_pd(double *dst, const double *a, const double *b)
{
    asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a
                 "movapd (%2), %%xmm1\n"  // load 2 double dari b
                 "addpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 + xmm1
                 "movapd %%xmm0, (%0)\n"  // store hasil ke dst
                 :
                 : "r"(dst), "r"(a), "r"(b)
                 : "xmm0", "xmm1");
}

void
simd_sub_pd(double *dst, const double *a, const double *b)
{
    asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a");
                 "movapd (%2), %%xmm1\n"  // load 2 double dari b
                 "subpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 - xmm1
                 "movapd %%xmm0, (%0)\n"  // store hasil ke dst
                 :
                 : "r"(dst), "r"(a), "r"(b)
                 : "xmm0", "xmm1");
}

void
simd_mul_pd(double *dst, const double *a, const double *b)
{
    asm volatile("movapd (%1), %%xmm0\n"  // load 2 double dari a");
                 "movapd (%2), %%xmm1\n"  // load 2 double dari b
                 "mulpd %%xmm1, %%xmm0\n" // xmm0 = xmm0 * xmm1
                 "movapd %%xmm0, (%0)\n"  // store hasil ke dst
                 :
                 : "r"(dst), "r"(a), "r"(b)
                 : "xmm0", "xmm1");
}
// void simd_div(double *dst, const double *a, const double *b);

void
fma_mul_add_pd(double *dst, const double *a, const double *b, const double *c)
{
    asm volatile("vmovapd (%1), %%ymm0\n"               // load a
                 "vmovapd (%2), %%ymm1\n"               // load b
                 "vmovapd (%3), %%ymm2\n"               // load c
                 "vfmadd132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 * ymm1) + ymm2
                 "vmovapd %%ymm0, (%0)\n"               // store result
                 :
                 : "r"(dst), "r"(a), "r"(b), "r"(c)
                 : "ymm0", "ymm1", "ymm2");
}

void
fma_mul_sub_pd(double *dst, const double *a, const double *b, const double *c)
{
    asm volatile("vmovapd (%1), %%ymm0\n"               // load a
                 "vmovapd (%2), %%ymm1\n"               // load b
                 "vmovapd (%3), %%ymm2\n"               // load c
                 "vfmsub132pd %%ymm1, %%ymm2, %%ymm0\n" // ymm0 = (ymm0 * ymm1) - ymm2
                 "vmovapd %%ymm0, (%0)\n"
                 :
                 : "r"(dst), "r"(a), "r"(b), "r"(c)
                 : "ymm0", "ymm1", "ymm2");
}

// void simd_fma_
// }