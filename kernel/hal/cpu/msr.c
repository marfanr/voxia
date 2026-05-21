#include "hal/cpu/msr.h"


void vxWRSR(uint32_t msr, uint64_t value) {
	// serial_printf("value : 0x%lx\n", value);

	uint32_t lo = (uint32_t) value;
	uint32_t hi = (uint32_t) (value >> 32);
	// serial_printf("wrmsr 0x%lx: hi=0x%lx lo=0x%lx\n", msr, hi, lo);

	asm volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

uint64_t vxRDMSR(uint32_t msr) {
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
	return ((uint64_t) hi << 32) | lo;
}

void msrSetFSBase(uint64_t base) {
	vxWRSR(MSR_FS_BASE, base);
}

void msrSetGSBase(uint64_t base) {
	vxWRSR(MSR_GS_BASE, base);
}

uintptr_t msrReadGSBase() {
	return vxRDMSR(MSR_GS_BASE);
}

void msrSetKernelGSBase(uint64_t base) {
	vxWRSR(MSR_KERNEL_GS_BASE, base);
}