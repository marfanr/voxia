#include "core.h"
#include "autoconf.h"
#include "hal/acpi/acpi.h"
#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "hal/timer/timer.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "procc/scheduler.h"
#include "sys/syscall.h"
#include <ioforge/ioforge.h>
#include <str.h>
#include <type.h>

#define INIT_CORE_MAGIC 0x00EEDDAB
#define INIT_CORE_ENTRYPOINT 0x8000

extern char _binary_hal_cpu_core_ap_bin_start[];
extern char _binary_hal_cpu_core_ap_bin_end[];

extern char __cpu_trampoline_start[];
extern char __cpu_trampoline_end[];

extern void initGdt(init_context_t* _);
extern void initSIMD(init_context_t* _);
extern void initTimer(init_context_t* _);

extern uint8_t x2_apic_supported;

each_core_data core_data[VOXIA_MAX_CORE] = {0};

extern uint8_t ap_stack_top[VOXIA_MAX_CORE][65536];

void update_core_gs(uint8_t id) {
	core_data[id].canary = (id + 0x56) ^ 0x595e9fbd94fda766;
	core_data[id].core_id = id;
	core_data[id].usleep_trigerred = false;
	core_data[id].scheduler = vxGetSchedulerCore(id);
	core_data[id].workqueue_count = 0;
	core_data[id].kernel_rsp =
	    (uintptr_t)ap_stack_top[id] + sizeof(ap_stack_top[id]);
	core_data[id].user_rsp = 0;

	const uintptr_t core_data_addr = (uintptr_t)&core_data[id];
	msrSetGSBase(core_data_addr);
	msrSetKernelGSBase(core_data_addr);
}

each_core_data* get_current_core_data(void) {
	each_core_data* core = (each_core_data*)msrReadGSBase();
	return core;
}

KERNEL_API
uint8_t get_current_core_cpuid() { return get_current_core_data()->core_id; }

each_core_data* vxGetCoreDataByCoreID(uint8_t core_id) {
	each_core_data* core = (each_core_data*)&core_data[core_id];
	return core;
}

extern void vxInitializeAPICTimer();
extern void init_simd();
extern void setup_gdt(int core);

extern uintptr_t __stack_chk_guard;
static volatile uint8_t active_core_count = 1;

__attribute__((no_stack_protector, noreturn))
__attribute__((section(".cpu_trampoline"))) void
cpuTrampolinePhase2(uint64_t core_id);

__attribute__((no_stack_protector, noreturn))
__attribute__((section(".cpu_trampoline"))) void
cpuTrampolinePhase2(uint64_t core_id) {
	__atomic_fetch_add(&active_core_count, 1, __ATOMIC_SEQ_CST);

	serial_setup();
	setup_gdt((uint8_t)core_id);
	update_core_gs((uint8_t)core_id);
	__stack_chk_guard = get_current_core_data()->canary;

	irq_setup((uint8_t)core_id);
	apicInitialize();
	init_simd();
	vxInitializeAPICTimer();

	setup_timer_interrupt();
	syscall_init();
	serial2_printf("core %d %d successfully running\n", core_id,
	               get_current_core_cpuid());
	vxGetCpuInfo((uint8_t)core_id)->status = Active;

	vxStartScheduler();

	for (;;)
		__asm__ volatile("hlt");
}

boolean_t multicore_start = false;

static uint32_t get_bsp_apic_id(void) {
	uint32_t eax, ebx, ecx, edx;
	__asm__ volatile("cpuid"
	                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	                 : "a"(1));
	return (ebx >> 24) & 0xFF;
}

static void sipi_sequential(uint32_t apic_id, uint64_t entrypoint_addr) {
	__asm__ volatile("mfence" ::: "memory");

	uint8_t vector = (entrypoint_addr >> 12) & 0xFF;

	if (!x2_apic_supported) {
		// INIT assert
		apic_write(APIC_ICR_HIGH, apic_id << 24);
		apic_write(APIC_ICR_LOW, (0b101 << 8) | (1 << 14));
		vxHPETSleep(ms2ns(10));

		while (apic_read(APIC_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");

		// INIT deassert
		apic_write(APIC_ICR_HIGH, apic_id << 24);
		apic_write(APIC_ICR_LOW, (0b101 << 8));
		vxHPETSleep(ms2ns(10));

		while (apic_read(APIC_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");

		// SIPI #1
		apic_write(APIC_ICR_HIGH, apic_id << 24);
		apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
		vxHPETSleep(ms2ns(10));

		while (apic_read(APIC_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");

		// SIPI #2
		apic_write(APIC_ICR_HIGH, apic_id << 24);
		apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
		vxHPETSleep(ms2ns(10));

		while (apic_read(APIC_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");

	} else {
		uint64_t icr;

		// INIT assert
		icr = 0;
		icr |= (0b101ULL << 8);
		icr |= (1ULL << 14);
		icr |= ((uint64_t)apic_id << 32);
		LOG_DEBUG("APIC", "ICR INIT assert  = 0x%x", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(10));
		LOG_DEBUG("APIC", "INIT assert OK");

		// SIPI #1
		icr = 0;
		icr |= (0b110ULL << 8);
		icr |= (uint8_t)vector;
		icr |= ((uint64_t)apic_id << 32);
		LOG_DEBUG("APIC", "ICR SIPI #1      = 0x%x", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(1));
		LOG_DEBUG("APIC", "SIPI #1 OK");

		// SIPI #2
		icr = 0;
		icr |= (0b110ULL << 8);
		icr |= (uint8_t)vector;
		icr |= ((uint64_t)apic_id << 32);
		LOG_DEBUG("APIC", "ICR SIPI #2      = 0x%x", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(1));
		LOG_DEBUG("APIC", "SIPI #2 OK");
	}
}

INIT(Core) {
	LOG_INFO("CORE", "preparing to send IPI");
	multicore_start = true;

	// 0x8000 - 0x9000 used for entry point in each core
	uintptr_t entrypoint_addr = INIT_CORE_ENTRYPOINT;
	size_t size = (uintptr_t)_binary_hal_cpu_core_ap_bin_end -
	              (uintptr_t)_binary_hal_cpu_core_ap_bin_start;
	size_t aligned_size = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	vxMultipleMmap(paging_get_highest_page_map(), entrypoint_addr,
	               entrypoint_addr, aligned_size, 0x3);
	paging_reload(paging_get_highest_page_map());
	memcopy((void*)entrypoint_addr,
	        (void*)_binary_hal_cpu_core_ap_bin_start, size);

	// prepare trampoline
	volatile uint64_t* trampoline_data =
	    (volatile uint64_t*)INIT_CORE_ENTRYPOINT;
	trampoline_data[1] = INIT_CORE_MAGIC;
	trampoline_data[2] = (uintptr_t)paging_get_highest_page_map();

	auto bsp_id = (int)get_bsp_apic_id();

	auto jum_core = vxGetNumberOfCores();
	LOG_DEBUG("CORE", "terdeteksi %d core", jum_core);
	for (uint8_t i = 0; i < jum_core; i++) {
		if (i == bsp_id)
			continue;

		auto core_info = vxGetCpuInfo(i);
		auto cpu_id = core_info->apicid;

		uint64_t pstack = (uint64_t)phys_base_alloc(5);
		uint64_t stack = (uint64_t)vma_lookup_free_vaddr(VMA_REGION_A,
		                                                 5); // 8kb
		vxMultipleMmap(paging_get_highest_page_map(), stack, pstack, 5,
		               0b111);
		paging_reload(paging_get_highest_page_map());

		LOG_DEBUG("CORE", "stack untuk core %d = 0x%x", cpu_id, stack);
		auto stack_top = stack + 5 * BLOCK_SIZE;

		// Allocate separate data page for each core to avoid collisions
		uintptr_t per_core_data_paddr = (uintptr_t)phys_base_alloc(1);
		uintptr_t per_core_data_vaddr =
		    vma_lookup_free_vaddr(VMA_REGION_A, 1);
		vxMultipleMmap(paging_get_highest_page_map(),
		               per_core_data_vaddr, per_core_data_paddr, 1,
		               0b111);
		paging_reload(paging_get_highest_page_map());
		vma_register(per_core_data_paddr, per_core_data_vaddr,
		             BLOCK_SIZE);

		volatile uint64_t* core_handshake =
		    (volatile uint64_t*)per_core_data_vaddr;
		core_handshake[0] = (uint64_t)cpuTrampolinePhase2;
		core_handshake[1] = stack_top;
		core_handshake[2] = 0;

		// Update trampoline data for this core
		trampoline_data[3] = (uint64_t)per_core_data_vaddr;

		LOG_DEBUG("CORE", "kirim sipi ke CPU Core %d", cpu_id);

		sipi_sequential(cpu_id, entrypoint_addr);

		while (__atomic_load_n(&core_handshake[2], __ATOMIC_SEQ_CST) ==
		       0) {
			vxHPETSleep(us2ns(100));
		}
	}

	LOG_INFO("core", "active core count %d", active_core_count);
}

uint8_t vxGetActiveCoreCount() { return active_core_count; }