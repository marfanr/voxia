#include "hal/cpu/msr.h"

#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

void
vxWRSR(uint32_t msr, uint64_t value)
{
    uint32_t lo = value & 0xFFFFFFFF, hi = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

uint64_t
vxRDMSR(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

void
msrSetFSBase(uint64_t base)
{
    vxWRSR(MSR_FS_BASE, base);
}

void
msrSetGSBase(uint64_t base)
{
    vxWRSR(MSR_GS_BASE, base);
}

uintptr_t
msrReadGSBase()
{
    return vxRDMSR(MSR_GS_BASE);
}

void
msrSetKernelGSBase(uint64_t base)
{
    vxWRSR(MSR_KERNEL_GS_BASE, base);
}