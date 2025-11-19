#include "hal/cpu/core.h"
#include "autoconf.h"
#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "hal/timer/timer.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "procc/scheduler.h"

extern char _binary_hal_cpu_core_ap_bin_start[];
extern char _binary_hal_cpu_core_ap_bin_end[];

extern char __cpu_trampoline_start[];
extern char __cpu_trampoline_end[];

extern void initGdt(init_context_t *_);
extern void initSIMD(init_context_t *_);
extern void initTimer(init_context_t *_);

static each_core_data core_data[VOXIA_MAX_CORE];

void
coreUpdateGs(uint16_t id)
{
    core_data[id].core_id          = id;
    core_data[id].usleep_trigerred = false;
    core_data[id].scheduler        = vxGetSchedulerCore(id);
    core_data[id].workqueue_count  = 0;

    const uintptr_t core_data_addr = (uintptr_t)&core_data[id];
    msrSetGSBase(core_data_addr);
    msrSetKernelGSBase(core_data_addr);
}

KERNEL_API
uint16_t
coreGetCpuID()
{
    uint16_t id;
    __asm__ volatile("movw %%gs:0, %0" : "=r"(id));
    return id;
}

each_core_data *
vxGetCoreData(void)
{
    each_core_data *core = (each_core_data *)msrReadGSBase();
    return core;
}

each_core_data *
vxGetCoreDataByCoreID(uint16_t core_id)
{
    each_core_data *core = (each_core_data *)&core_data[core_id];
    return core;
}

static uint16_t current_core_id = 1;

extern void vxInitializeAPICTimer();

__attribute__((section(".cpu_trampoline"))) void
cpuTrampolinePhase2()
{
    serial_setup();
    LOG_DEBUG("CORE", "core init at %d", current_core_id);

    initSIMD(nullptr);
    initGdt(nullptr);
    coreUpdateGs(current_core_id);
    irq_setup(current_core_id);
    current_core_id++;
    apicInitialize();
    initTimer(nullptr);
    // workqueue init
    // LOG_INFO("workqueue", "still running %f ns",  (double)2.3f/(double)3.f);

    vxStartScheduler();

    for (;;)
        __asm__ volatile("hlt");
}

INIT(Core)
{
    // bsp
    // set_fs_base(0x1000);
    LOG_INFO("CORE", "preparing to send IPI\n");
    uint64_t addr         = 0x8000;
    size_t   size         = _binary_hal_cpu_core_ap_bin_end - _binary_hal_cpu_core_ap_bin_start;
    size_t   aligned_size = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    vxMultipleMmap(paging_get_highest_page_map(), addr, addr, aligned_size, 0x3);
    paging_reload(paging_get_highest_page_map());
    LOG_INFO("CORE", "dma addr %x offset 0x%x", addr, addr - ALIGN_DOWN(addr, BLOCK_SIZE));
    memcopy((void *)addr, (void *)_binary_hal_cpu_core_ap_bin_start, size);

    uintptr_t data_paddr = (uintptr_t)vxPhysBaseAlloc(1);
    uintptr_t data_vaddr = vma_lookup_free_vaddr(VMA_REGION_A, 1);
    vxMultipleMmap(paging_get_highest_page_map(), data_vaddr, data_paddr, 1, 0b111);
    paging_reload(paging_get_highest_page_map());
    vma_register(data_paddr, data_vaddr, 1);

    volatile uint64_t *data = (volatile uint64_t *)data_vaddr;
    LOG_DEBUG("CORE", "data at 0x%x", data);
    data[0] = (uint64_t)cpuTrampolinePhase2;
    LOG_DEBUG("CORE", "cpu trampoline phsae 2 0x%x", data[0]);

    // // prepare trampoline
    volatile uint64_t *trampoline_data = (volatile uint64_t *)0x8000;
    trampoline_data[0]                 = 0x00EEDDAB;
    trampoline_data[1]                 = (uintptr_t)paging_get_highest_page_map();
    trampoline_data[2]                 = (uint64_t)data_vaddr;

    // kirim SIPI
    for (int i = current_core_id; i < 8; i++)
    {
        KDEBUG(DEBUG_LEVEL_INFO, "Waking up core %d ...\n", i);
        uint64_t pstack = (uint64_t)vxPhysBaseAlloc(5);
        uint64_t stack  = (uint64_t)vma_lookup_free_vaddr(VMA_REGION_A, 5);
        vxMultipleMmap(paging_get_highest_page_map(), stack, pstack, 5, 0b111);
        paging_reload(paging_get_highest_page_map());

        LOG_DEBUG("CORE", "core %d stack at 0x%x", i, stack);
        data[1] = (uint64_t)stack;

        __asm__ volatile("mfence\n\t"
                         "wbinvd\n\t" ::
                             : "memory");

        // Reset core target (INIT IPI)
        apic_write(APIC_ICR_HIGH, i << 24);
        apic_write(APIC_ICR_LOW, (0b101 << 8) | (1 << 14)); // INIT assert
        usleep(30);                                         // 10ms delay

        apic_write(APIC_ICR_HIGH, i << 24);
        apic_write(APIC_ICR_LOW, (0b101 << 8)); // INIT de-assert
        usleep(30);

        // Startup IPI (SIPI)
        apic_write(APIC_ICR_HIGH, i << 24);
        apic_write(APIC_ICR_LOW, (0b110 << 8) | ((addr >> 12) & 0xFF));
        usleep(ms2ns(100)); // delay cukup supaya core mulai eksekusi

        // Kadang perlu kirim SIPI kedua
        apic_write(APIC_ICR_HIGH, i << 24);
        apic_write(APIC_ICR_LOW, (0b110 << 8) | ((addr >> 12) & 0xFF));
        usleep(ms2ns(200));

        LOG_DEBUG("CORE", "Core %d SIPI sent", i);
    }

    apic_write(APIC_ICR_HIGH, (1 << 24));
    apic_write(APIC_ICR_LOW, 0x3D | 0x0 | 0x00004000 | 0x0);
    // paging_unmap_page(paging_get_highest_page_map(), addr);
    // paging_reload(paging_get_highest_page_map());
}