#include "init/init.h"
#include "init/loader.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "notify.h"
#include "procc/process.h"
#include "tty/tty.h"
#include <console/console.h>
#include <hal/acpi/hpet.h>
#include <memory/kalloc.h>
#include <net/netutils.h>
#include <str.h>
#include <libk/simd.h>

// prototypes
__attribute__((noreturn)) void _start(void);

KERNEL_API void _ZdlPv(void* ptr);
KERNEL_API void _ZdlPvm(void* ptr, size_t size);
int atexit(void (*function)(void));
KERNEL_API void __cxa_finalize(void* dso_handle);
__attribute__((noreturn)) void kernel_main(void);


__attribute__((unused))
static init_context_t ctx = {};

__attribute__((unused, noreturn)) void _start(void) {
	kernel_main();
}

__attribute__((unused, noreturn)) void kernel_main(void) {
	serial_setup();
	serial_printf("Voxia OS starting...\n");
	build_context_from_limine(&ctx);
	run_all_init_calls(&ctx);

	wait_until_receive_notify("/vfs/root", 5000);

	void* test_ptr = kalloc(256);
	if (test_ptr) {
		kalloc_metadata_t* meta =
		    (kalloc_metadata_t*)((uintptr_t)test_ptr -
		                         sizeof(kalloc_metadata_t) - 16);
		LOG2_INFO("KALLOC_TEST",
		          "Allocated 256 bytes at %x, sizeof(meta)=%d",
		          test_ptr, sizeof(kalloc_metadata_t));
		LOG2_INFO("KALLOC_TEST",
		          "metadata: size=%d, magic=0x%x, pad=0x%x", meta->size,
		          meta->magic, meta->_pad);
		kfree2(test_ptr);
		LOG2_INFO("KALLOC_TEST", "Freed successfully");
	}

	/* Large allocation test */
	void* large_ptr = kalloc(4096);
	if (large_ptr) {
		kalloc_metadata_t* meta =
		    (kalloc_metadata_t*)((uintptr_t)large_ptr -
		                         sizeof(kalloc_metadata_t) - 16);
		LOG2_INFO("KALLOC_TEST",
		          "Allocated 4096 bytes at %x, sizeof(meta)=%d",
		          large_ptr, sizeof(kalloc_metadata_t));
		LOG2_INFO("KALLOC_TEST",
		          "metadata: size=%d, magic=0x%x, pad=0x%x", meta->size,
		          meta->magic, meta->_pad);
		kfree2(large_ptr);
		LOG2_INFO("KALLOC_TEST", "Large alloc freed successfully");
	}

	pmm_log_usage();

	// === FPU/SIMD Context Switch Test ===
	LOG2_INFO("FPU_TEST", "Beginning SIMD operation inside Kernel...");
	kernel_fpu_begin();
	
	double vec_a[2] __attribute__((aligned(16))) = { 5.0, 10.0 };
	double vec_b[2] __attribute__((aligned(16))) = { 2.0, 3.0 };
	double vec_res[2] __attribute__((aligned(16))) = { 0.0, 0.0 };
	
	simd_mul_pd(vec_res, vec_a, vec_b);
	
	int r0 = 0, r1 = 0;
	__asm__ volatile("cvttsd2si %1, %0" : "=r"(r0) : "m"(vec_res[0]));
	__asm__ volatile("cvttsd2si %1, %0" : "=r"(r1) : "m"(vec_res[1]));

	LOG2_INFO("FPU_TEST", "SIMD Multiply Result: %d and %d", r0, r1);
	
	kernel_fpu_end();
	LOG2_INFO("FPU_TEST", "Kernel FPU block ended successfully.");
	// =====
	
	start_tty();

	run_process("/sbin/init.elf", 0, 0);
	INFLOOP;
}

// cpp runtime stub
KERNEL_API void _ZdlPv(void* ptr) {
	kfree2(ptr); 
}

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
