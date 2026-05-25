#ifndef __HAL__CPU__MSR_H__
#define __HAL__CPU__MSR_H__

#include <type.h>

#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084
#define MSR_EFER   0xC0000080
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102


void      vxWRSR(uint32_t msr, uint64_t value);
uint64_t  vxRDMSR(uint32_t msr);
void      msrSetFSBase(uint64_t base);
void      msrSetGSBase(uint64_t base);
void      msrSetKernelGSBase(uint64_t base);
uintptr_t msrReadGSBase();
uintptr_t msrReadFSBase();
uintptr_t msrReadKernelGSBase();

#endif // __HAL__CPU__MSR_H__