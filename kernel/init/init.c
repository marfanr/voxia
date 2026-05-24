#include "init/init.h"
#include "hal/apic/apic.h"
#include "hal/cpu/interrupt.h"
#include "init/loader.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "notify.h"
#include "procc/process.h"
#include <console/console.h>
#include <hal/acpi/hpet.h>
#include <memory/kalloc.h>
#include <net/netutils.h>
#include <str.h>

// prototypes
__attribute__((noreturn)) void _start(struct stivale2_struct* stivale2_struct);

KERNEL_API void _ZdlPv(void* ptr);
KERNEL_API void _ZdlPvm(void* ptr, size_t size);
int atexit(void (*function)(void));
KERNEL_API void __cxa_finalize(void* dso_handle);

static init_context_t ctx = {};

// entry point of kernel
__attribute__((unused, noreturn)) extern void
_start(struct stivale2_struct* stivale2_struct) {
	serial_setup();
	build_context_from_stivale2(stivale2_struct, &ctx);
	run_all_init_calls(&ctx);

	// for logger
	auto irq = irq_alloc_entry(0);
	irq_register(0, irq, (void*)serial2_flush, true, 0x28, 0,
	             INTERRUPT_ATTR_KERNEL);
	vxAPICCreateTimer(APIC_TIMER_PERIOD, 100, irq);

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

	
	// KDEBUG(DEBUG_LEVEL_INFO, "Boot complete, entering idle loop...\n");
	
	execve("/sbin/term.elf", 0, 0);
	execve("/sbin/hello.elf", 0, 0);
	
	// print_dentry_tree(get_root_dentry(), 0);
	pmm_log_usage();
	
	INFLOOP;
}

// cpp runtime stub
KERNEL_API void _ZdlPv(void* ptr) {
	kfree(ptr, sizeof(ptr)); 
}

KERNEL_API void _ZdlPvm(void* ptr, size_t size) {
	kfree(ptr, size);
}

KERNEL_API
int atexit(__attribute__((unused)) void (*function)(void)) {
	LOG_DEBUG("LIBC", "atexit called, but not implemented");
	return 0;
}

KERNEL_API void __cxa_finalize(__attribute__((unused)) void* dso_handle) {
	LOG_DEBUG("LIBC", "__cxa_finalize called, but not implemented");
}
