#ifndef __HAL__CPU__MSR_H__
#define __HAL__CPU__MSR_H__

#include <type.h>

void      vxWRSR(uint32_t msr, uint64_t value);
uint64_t  vxRDMSR(uint32_t msr);
void      msrSetFSBase(uint64_t base);
void      msrSetGSBase(uint64_t base);
void      msrSetKernelGSBase(uint64_t base);
uintptr_t msrReadGSBase();

#endif // __HAL__CPU__MSR_H__