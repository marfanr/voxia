#include "hal/cpu/paging.h"
#include "libk/io.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include <hal/acpi/acpi.h>
#include <hal/apic/apic.h>
#include <hal/cpu/cpuid.h>

static uintptr_t lapic_base_addr = 0;

void
apic_write(uint32_t reg, uint32_t value)
{
    mmio_outl((lapic_base_addr + reg), value);
}

uint32_t
apic_read(uint32_t reg)
{
    return mmio_inl((lapic_base_addr + reg));
}

void
cpu_trampoline()
{
    // asm volatile("hlt");
}

void
apic_initialize(uintptr_t apic_base_addr)
{
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1 << 11);
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if ((ecx & (1 << 21)))
    {
        // TODO: wile be handled soon
        LOG_INFO("APIC", "x2APIC available");
    }

    lapic_base_addr = apic_base_addr;

    apic_write(APIC_TPR, 0x00);
    apic_write(APIC_DFR, 0xFFFFFFFF);
    apic_write(APIC_SVR, 0xff | 0x100);

    // menghidupkan semua core
    serial_trace("preparing to send IPI\n");
    uint64_t addr = (uint64_t)dma_alloc(1);
    paging_mmap_fill(paging_get_highest_page_map(), addr, addr, 1, 0x3);

    LOG_INFO("APIC", "dma addr offset 0x%x", ALIGN_UP(addr, BLOCK_SIZE) - addr);
    memcopy((void *)addr, (void *)cpu_trampoline, 0x1000);

    // test interrupt
    apic_write(APIC_ICR_HIGH, (0x0 << 24));
    apic_write(APIC_ICR_LOW, 0x30 | 0x0 | 0x00004000 | 0x0);
    // apic_send_ipi(0x20, 0x0);

    // for (int i = 0; i < 2; i++)
    // {
    //     cpu_core_t core = cpu_list[i];
    //     if (core.cpuid != 0)
    //     {
    //         apic_write(0x280, 0);

    //         // init ipi
    //         apic_write(APIC_ICR_HIGH, (core.apicid << 24));
    //         apic_write(APIC_ICR_LOW, (0b101 << 8) | (1 << 14));
    //         usleep(10000);

    //         apic_write(APIC_ICR_HIGH, (core.apicid << 24));
    //         apic_write(APIC_ICR_LOW, (0b101 << 8));
    //         usleep(10000);

    //         for (int i = 0; i < 2; i++)
    //         {
    //             apic_send_ipi(8, core.apicid);
    //             usleep(1000);
    //             do
    //             {
    //                 __asm__ __volatile__("pause" : : : "memory");
    //             } while (*((volatile uint32_t *)(local_apic_addr + 0x300)) & (1 << 12));
    //         }
    //     }
    //     usleep(1000);
    // }
}

void
apic_eoi()
{
    apic_write(APIC_EOI, 0);
}