#include "init/init.h"
#include "graphic.h"
#include "hal/timer/timer.h"
#include "init/loader.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "notify.h"
#include "procc/process.h"
#include "tty/tty.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include <console/console.h>
#include <hal/acpi/hpet.h>
#include <libk/simd.h>
#include <memory/kalloc.h>
#include <net/netutils.h>
#include <procc/scheduler.h>
#include <procc/thread.h>
#include <str.h>

// prototypes
__attribute__((noreturn)) void _start(void);

KERNEL_API void _ZdlPv(void* ptr);
KERNEL_API void _ZdlPvm(void* ptr, size_t size);
int atexit(void (*function)(void));
KERNEL_API void __cxa_finalize(void* dso_handle);
__attribute__((noreturn)) void kernel_main(void);

__attribute__((unused)) static init_context_t ctx = {};

static void kernel_post_init(void* arg) {
	(void)arg;

	{
		dentry_ptr mnt;
		vxnamei("/mnt", &mnt);
		auto vnode = create_and_attach_vnode();
		mnt->vnode = vnode;
	}

	wait_until_receive_notify("/vfs/root", 5000);

	pmm_log_usage();

	start_tty();

	run_process("/sbin/init.elf", 0, 0);

}

static uint64_t profiler_last_run[VOXIA_MAX_NUMBER_THREAD] = {0};

__attribute__((unused))
static void cpu_profiler_thread(void) {
	uint64_t last_time = vxHPETGetMainCount();

	while (1) {
		thread_sleep(10000); // Tidur 1 detik

		uint64_t curr_time = vxHPETGetMainCount();
		uint64_t delta_time = (curr_time - last_time) * vxHPETMinTickNs();
		
		if (delta_time > 0) {
			uint64_t core_delta_run[VOXIA_MAX_CORE] = {0};

			for (uint32_t idx = 0; idx < VOXIA_MAX_NUMBER_THREAD; idx++) {
				thread_t* t = vxGetThreadByIndex(idx);
				if (!t) continue;
				
				uint64_t curr_run = vxGetThreadTotalRunTime(t);
				uint64_t delta_run = curr_run - profiler_last_run[idx];
				
				uint64_t usage = (delta_run * 100) / delta_time;
				
				uint16_t core_id = vxGetThreadCurrentCore(t);
				if (core_id < VOXIA_MAX_CORE) {
					core_delta_run[core_id] += delta_run;
				}
				
				if (usage > 0) {
					serial2_printf("[PROFILER] Thread ID %d Usage: %ld%%\n", t->id, usage);
				}
				
				profiler_last_run[idx] = curr_run;
			}
			
			for (uint16_t c = 0; c < VOXIA_MAX_CORE; c++) {
				uint64_t core_usage = (core_delta_run[c] * 100) / delta_time;
				if (core_usage > 0) {
					serial2_printf("[PROFILER] Core %d Usage: %ld%%\n", c, core_usage);
				}
			}
		}

		last_time = curr_time;
	}
}

__attribute__((unused, noreturn)) void _start(void) { kernel_main(); }

__attribute__((unused, noreturn)) void kernel_main(void) {
	serial_setup();
	serial_printf("Voxia OS starting...\n");
	build_context_from_limine(&ctx);
	
	extern void init_simd();
	init_simd();

	run_all_init_calls(&ctx);

	kernel_post_init(0);

	thread_t* profiler = create_thread((uintptr_t)cpu_profiler_thread, 0, 0, 0, 1, 0);
	attach_to_scheduler(profiler);

	extern void vxStartScheduler(void);
	vxStartScheduler();

	// Enable interrupts and halt, letting the scheduler take over
	__asm__ volatile("sti");
	INFLOOP;
}

// cpp runtime stub
KERNEL_API void _ZdlPv(void* ptr) { kfree2(ptr); }

KERNEL_API void _ZdlPvm(void* ptr, size_t size) {
	(void)size;
	kfree2(ptr);
}

KERNEL_API
int atexit(__attribute__((unused)) void (*function)(void)) {
	LOG_DEBUG("LIBC", "atexit called, but not implemented");
	return 0;
}

KERNEL_API void __cxa_finalize(__attribute__((unused)) void* dso_handle) {
	LOG_DEBUG("LIBC", "__cxa_finalize called, but not implemented");
}
