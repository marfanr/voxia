#include "hal/cpu/core.h"
#include "hal/cpu/msr.h"
#include "libk/serial.h"
#include "procc/thread.h"
#include <sys/syscall.h>

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004
#define ARCH_GET_CPUID 0x1011
#define ARCH_SET_CPUID 0x1012

int syscall_arch_prctl(int code, unsigned long addr) {
	auto thr = get_current_core_data()->active_thread;
	switch (code) {
	case ARCH_SET_FS: {
		serial2_printf("arch_prctl: set fs into 0x%x\n", addr);
		thr->fs_base = addr;
		msrSetFSBase(addr);
		break;
	}
	case ARCH_SET_GS: {
		serial2_printf("arch_prctl: set gs into 0x%x\n", addr);
		thr->gs_base = addr;
		msrSetKernelGSBase(addr);
		break;
	}
	case ARCH_GET_FS: {
		serial2_printf("arch_prctl: get fs into 0x%x\n", addr);
        *(unsigned long*)addr = thr->fs_base;
		break;
	}
    case ARCH_GET_GS: {
        serial2_printf("arch_prctl: get gs into 0x%x\n", addr);
        *(unsigned long*)addr = thr->gs_base;
        break;
    }
	default:
		return -1;
	}
	return 0;
}
