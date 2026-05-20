#include <type.h>
#include <hal/cpu/cpuid.h>

void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx,
	   uint32_t* ecx, uint32_t* edx) {
	uint32_t a, b, c, d;
	asm volatile("cpuid"
		     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
		     : "a"(leaf), "c"(subleaf)
		     : "memory");
	if (eax)
		*eax = a;
	if (ebx)
		*ebx = b;
	if (ecx)
		*ecx = c;
	if (edx)
		*edx = d;
}

int cpuid_get_model(void) {
	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	int family =
		((eax >> 8) & 0xf) + ((eax >> 20) & 0xff); // extended family
	int model = (int) (((eax >> 4) & 0xf)
			   + (((eax >> 16) & 0xf) << 4)); // extended model
	return model + (family << 8);			  // encode family+model
}

boolean_t cpuid_check_apic(void) {
	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	return (edx & (1 << 9)) != 0;
}

int cpuid_get_bsp_id(void) {
	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	return (ebx >> 24) & 0xff;
}
