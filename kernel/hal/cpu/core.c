#include "hal/cpu/core.h"
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
#include <str.h>
#include "libk/type.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "procc/scheduler.h"

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

static each_core_data core_data[VOXIA_MAX_CORE];

void coreUpdateGs(uint16_t id) {
	core_data[id].core_id = id;
	core_data[id].usleep_trigerred = false;
	core_data[id].scheduler = vxGetSchedulerCore(id);
	core_data[id].workqueue_count = 0;

	const uintptr_t core_data_addr = (uintptr_t) &core_data[id];
	msrSetGSBase(core_data_addr);
	msrSetKernelGSBase(core_data_addr);
}

KERNEL_API
uint16_t coreGetCpuID() {
	uint16_t id;
	__asm__ volatile("movw %%gs:0, %0" : "=r"(id));
	return id;
}

each_core_data* vxGetCoreData(void) {
	each_core_data* core = (each_core_data*) msrReadGSBase();
	return core;
}

each_core_data* vxGetCoreDataByCoreID(uint16_t core_id) {
	each_core_data* core = (each_core_data*) &core_data[core_id];
	return core;
}

extern void vxInitializeAPICTimer();

static volatile int active_core_count = 1;

__attribute__((section(".cpu_trampoline"))) void
cpuTrampolinePhase2(uint64_t core_id) {
	__atomic_fetch_add(&active_core_count, 1, __ATOMIC_SEQ_CST);

	serial_setup();

	initSIMD(nullptr);
	initGdt(nullptr);
	coreUpdateGs(core_id);
	irq_setup(core_id);
	apicInitialize();
	initTimer(nullptr);
	serial2_printf("core %d successfully running\n", core_id);

	// // workqueue init
	// // LOG_INFO("workqueue", "still running %f ns",
	// // (double)2.3f/(double)3.f);

	vxStartScheduler();

	for (;;)
		__asm__ volatile("hlt");
}

boolean_t multicore_start = false;

uint32_t get_bsp_apic_id(void) {
	uint32_t eax, ebx, ecx, edx;
	__asm__ volatile("cpuid"
			 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			 : "a"(1));
	return (ebx >> 24) & 0xFF; // Initial APIC ID ada di EBX[31:24]
}

void sipi_sequential(uint32_t apic_id, uint64_t entrypoint_addr) {
	__asm__ volatile("mfence" ::: "memory");

	uint8_t vector = (entrypoint_addr >> 12) & 0xFF;

	if (!x2_apic_supported) {
		// ======================
		// xAPIC (MMIO)
		// ======================

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
		icr |= ((uint64_t) apic_id << 32);
		LOG_DEBUG("APIC", "ICR INIT assert  = 0x%lx", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(10));
		LOG_DEBUG("APIC", "INIT assert OK");

		// SIPI #1
		icr = 0;
		icr |= (0b110ULL << 8);
		icr |= (uint8_t) vector;
		icr |= ((uint64_t) apic_id << 32);
		LOG_DEBUG("APIC", "ICR SIPI #1      = 0x%lx", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(200));
		LOG_DEBUG("APIC", "SIPI #1 OK");

		// SIPI #2
		icr = 0;
		icr |= (0b110ULL << 8);
		icr |= (uint8_t) vector;
		icr |= ((uint64_t) apic_id << 32);
		LOG_DEBUG("APIC", "ICR SIPI #2      = 0x%lx", icr);
		vxWRSR(0x830, icr);
		vxHPETSleep(ms2ns(200));
		LOG_DEBUG("APIC", "SIPI #2 OK");
	}
}

INIT(Core) {
	LOG_INFO("CORE", "preparing to send IPI");
	multicore_start = true;

	// 0x8000 - 0x9000 digunakan untuk entry point di tiap core
	uint64_t entrypoint_addr = INIT_CORE_ENTRYPOINT;
	size_t size = _binary_hal_cpu_core_ap_bin_end
		      - _binary_hal_cpu_core_ap_bin_start;
	size_t aligned_size = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	vxMultipleMmap(paging_get_highest_page_map(), entrypoint_addr,
		       entrypoint_addr, aligned_size, 0x3);
	paging_reload(paging_get_highest_page_map());
	memcopy((void*) entrypoint_addr,
		(void*) _binary_hal_cpu_core_ap_bin_start, size);

	uintptr_t data_paddr = (uintptr_t) vxPhysBaseAlloc(1);
	uintptr_t data_vaddr = vma_lookup_free_vaddr(VMA_REGION_A, 1);
	vxMultipleMmap(paging_get_highest_page_map(), data_vaddr, data_paddr, 1,
		       0b111);
	paging_reload(paging_get_highest_page_map());
	vma_register(data_paddr, data_vaddr, 1);

	volatile uint64_t* data = (volatile uint64_t*) data_vaddr;
	LOG_DEBUG("CORE", "data at 0x%x", data);
	data[0] = (uint64_t) cpuTrampolinePhase2;
	LOG_DEBUG("CORE", "cpu trampoline phase 2 0x%x", data[0]);

	// prepare trampoline
	// dengan menyisipkan data di 24 byte pertama
	volatile uint64_t* trampoline_data =
		(volatile uint64_t*) INIT_CORE_ENTRYPOINT;
	trampoline_data[0] = INIT_CORE_MAGIC;
	trampoline_data[1] = (uintptr_t) paging_get_highest_page_map();
	trampoline_data[2] = (uint64_t) data_vaddr;

	auto bsp_id = (int) get_bsp_apic_id();

	// kirim SIPI (StartUp IPI)
	auto jum_core = vxGetNumberOfCores();
	LOG_DEBUG("CORE", "terdeteksi %d core", jum_core);
	for (int i = 0; i < jum_core; i++) {
		if (i == bsp_id)
			continue;

		auto core_info = vxGetCpuInfo(i);
		auto cpu_id = core_info->apicid;

		uint64_t pstack = (uint64_t) vxPhysBaseAlloc(5);
		uint64_t stack = (uint64_t) vma_lookup_free_vaddr(VMA_REGION_A,
								  5); // 8kb
		vxMultipleMmap(paging_get_highest_page_map(), stack, pstack, 5,
			       0b111);
		paging_reload(paging_get_highest_page_map());

		LOG_DEBUG("CORE", "stack untuk core %d = 0x%x", cpu_id, stack);
		data[1] = (uint64_t) stack;
		data[2] = (uint64_t) cpu_id;
		data[3] = 0; // untuk handshake

		LOG_DEBUG("CORE", "kirim sipi ke CPU Core %d", cpu_id);

		sipi_sequential(cpu_id, entrypoint_addr);

		uint64_t timeout = 10000000ULL;
		while (data[3] == 0 && timeout-- > 0)
			__asm__ volatile("pause");

		if (data[3] == 0) {
			LOG_WARN("CORE", "core %d tidak merespons!", cpu_id);

			// retry
			for (int j = 0; j < 3; j++) {
				LOG_INFO("CORE", "retry %d pada core %d", j,
					 cpu_id);

				sipi_sequential(cpu_id, entrypoint_addr);

				timeout = 10000000ULL;
				while (data[3] == 0 && timeout-- > 0)
					vxHPETSleep(ms2ns(1000));

				if (data[3] != 0)
					LOG_INFO("CORE",
						 "core %d sudah berhasil nyala",
						 cpu_id);
				core_info->status = Active;
				break;
			}
		} else {
			LOG_DEBUG("CORE", "core %d sudah ambil data", cpu_id);
			core_info->status = Active;
		}
	}

	LOG_INFO("core", "active core count %d", active_core_count);

	// debug
	// apic_write(APIC_ICR_HIGH, (1 << 24));
	// apic_write(APIC_ICR_LOW, 0x3D | 0x0 | 0x00004000 | 0x0);
}

// TODO: ganti mantain status per core
// ini termausk bsp
int vxGetActiveCoreCount() {
	return active_core_count;
}