/*
  BSD 3-Clause License

  Copyright (c) 2023, Mohammad Arfan
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <dev/cpu/apic/apic.h>
#include <firmw/acpi/acpi.h>
#include <hal/cpu/paging.h>
#include <libk/debug/debug.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <libk/timer.h>
#include <memory/memory_utils.h>

extern cpu_core_t *cpu_list;

void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
  asm volatile("cpuid"
               : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
               : "0"(*eax), "2"(*ecx));
}

int apic_is_supported() {
  uint32_t eax, edx, unused;
  cpuid(&eax, &unused, &unused, &edx);
  return edx & (1 << 9);
}

void cpuGetMSR(uint32_t msr, uint32_t *lo, uint32_t *hi) {
  asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

void cpuSetMSR(uint32_t msr, uint32_t lo, uint32_t hi) {
  asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

#define APIC_TPR 0x80
#define APIC_DFR 0xE0
#define APIC_LDR 0xD0
#define APIC_SVR 0xF0
#define APIC_EOI 0xB0
#define APIC_ICR_LOW 0x300
#define APIC_ICR_HIGH 0x310
#define APIC_LOGIC_DEST 0xD0
#define APIC_ARBITATION_PRIOR 0x090
#define APIC_IRR 0x200
#define APIC_IEA 0x480

#define APIC_LVT_TIMER 0x320
#define APIC_TIMER_INIT_CNT 0x380
#define APIC_TIMER_CUR_CNT 0x390
#define APIC_TIMER_DIVIDE 0x3E0

#define APIC_LVT_MASK (1 << 16) && 0b1
#define APIC_LVT_VECTOR 0xFF

#define APIC_ICR_MSG_FIXED (0b000 << 8)
#define APIC_ICR_MSG_LOWEST_PRIOR (0b001 << 8)
#define APIC_ICR_MSG_SMI (0b010 << 8)
#define APIC_ICR_MSG_NMI (0b100 << 8)
#define APIC_ICR_MSG_INIT (0b101 << 8)

#define APIC_ICR_TGM_LEVEL (1 << 15)
#define APIC_ICR_TGM_EDGE (0 << 15)

#define APIC_LINT0 0x350

void apic_write(uint32_t reg, uint32_t value) {
  *((volatile uint32_t *)(local_apic_addr + reg)) = value;
}

uint32_t apic_read(uint32_t reg) {
  return *((volatile uint32_t *)(local_apic_addr + reg));
}

void apic_send_ipi(uint8_t vector, uint8_t dest) {
  apic_write(APIC_ICR_HIGH, (dest << 24));
  apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
}

void apic_timer_setup(uint32_t count, uint8_t vector) {
  apic_write(APIC_TIMER_DIVIDE, 0x1);

  // Set the LVT Timer Register to periodic mode and the timer interrupt
  // vector
  apic_write(APIC_LVT_TIMER, 0x20000 | vector);

  // Set the initial count
  apic_write(APIC_TIMER_INIT_CNT, count);
}

void cpu_trampoline() {
  // asm volatile("hlt");
}

void apic_memcpy(void *dest, void *src, size_t size) {
  for (size_t i = 0; i < size; i++) {
    ((char *)dest)[i] = ((char *)src)[i];
  }
}

void apic_setup() {
  // check is apic supported
  if (apic_is_supported())
    KDEBUG(DEBUG_LEVEL_DEBUG, "APIC supported\n");

  // check is x2 apic supported
  // uint32_t eax, ebx, ecx, edx;
  // cpuid(&eax, &ebx, &ecx, &edx);
  // if (ecx & (1 << 21))
  //   KDEBUG(DEBUG_LEVEL_DEBUG, "x2APIC supported");

  KDEBUG(DEBUG_LEVEL_DEBUG, "APIC Base: 0x%x\n", local_apic_addr);

  apic_write(APIC_TPR, 0x00);
  apic_write(APIC_DFR, 0xFFFFFFFF);
  // apic_write(APIC_LDR, 0x0100000);
  apic_write(APIC_SVR, 0xff | 0x100);

  // print apic version

  KDEBUG(0, "ICR : 0x%x, delivered status 0b%b, read status : 0b%b\n",
         apic_read(APIC_ICR_LOW), apic_read(APIC_ICR_LOW >> 12) & 0x1,
         (apic_read(APIC_ICR_LOW) >> 16) & 0x3);

  // accept interrupt
  KDEBUG(0, "APIC Version : 0x%x, max LVT : %d, APIC CUR ID: %d\n",
         apic_read(0x30) & 0xFF, ((apic_read(0x30) >> 16) & 0xFF) + 1,
         (uint8_t)(apic_read(0x20) >> 24));

  KDEBUG(0, "error 0b%b\n", apic_read(0x370));

  outb((0x43), 0b00010100);
  uint16_t reload_value = (uint16_t)(1193182 / 20);
  outb((0X40), reload_value & 0xFF);        // LSB
  outb((0X40), (reload_value >> 8) & 0xFF); // MSB

  serial_trace("PIT Reload Value : %d\n", reload_value);

  // setup apic timer as micro second
  apic_write(APIC_TIMER_INIT_CNT, 0xFFFFFFFF);
  apic_write(APIC_TIMER_DIVIDE, 0x3);
  apic_write(APIC_LVT_TIMER, 0x20000 | 48); // Periodic mode
  // Set the initial count

  uint32_t __start = apic_read(0x390);
  serial_trace("APIC Timer Start : %d\n", __start);
  // menunggu pit
  uint16_t pit_status;
  do {
    outb(0x43, 0x00);
    pit_status = inb(0x40); // Baca status PIT
    pit_status |= inb(0x40) << 8;
  } while ((pit_status & 0x20) == 0);

  uint32_t __end = apic_read(0x390);
  serial_trace("APIC Timer End : %d\n", __end);
  uint32_t freq = (__start - __end) * 20;
  serial_trace("APIC Timer Frequency : %d\n", freq);

  apic_write(APIC_TIMER_INIT_CNT, freq / 1000000);
  apic_write(APIC_LVT_TIMER, 0x20000 | 48); // Periodic mode

  // turnoff pit
  outb(0x61, inb(0x61) & 0xFE);
  __asm__ volatile("sti");

  // set cpu mode apic di IA32_APIC_BASE_MSR
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
  lo |= 0x800;
  asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

  serial_trace("preparing to send IPI\n");
  paging_mmap_fill((page_t)PHYS2VIRT((uint64_t)paging_get_highest_page_map()),
                   0x8000, 0x8000, 1, 0x3);
  // apic_write(APIC_ICR_HIGH, (0x0 << 24));
  // apic_write(APIC_ICR_LOW, 40 | 0x0 | 0x00004000 | 0x0);
  // apic_send_ipi(0x20, 0x0);

  apic_memcpy((void *)0x8000, (void *)cpu_trampoline, 0x1000);
  for (int i = 0; i < 2; i++) {
    cpu_core_t core = cpu_list[i];
    if (core.cpuid != 0) {
      apic_write(0x280, 0);

      // init ipi
      apic_write(APIC_ICR_HIGH, (core.apicid << 24));
      apic_write(APIC_ICR_LOW, (0b101 << 8) | (1 << 14));
      usleep(10000);

      apic_write(APIC_ICR_HIGH, (core.apicid << 24));
      apic_write(APIC_ICR_LOW, (0b101 << 8));
      usleep(10000);

      for (int i = 0; i < 2; i++) {
        apic_send_ipi(8, core.apicid);
        usleep(1000);
        do {
          __asm__ __volatile__("pause" : : : "memory");
        } while (*((volatile uint32_t *)(local_apic_addr + 0x300)) & (1 << 12));
      }
    }
    usleep(1000);
  }
  serial_trace("IPI sent\n");
}

void apic_eoi() {
  apic_write(APIC_EOI, 0);
  // serial_send_string("EOI\n");
}
