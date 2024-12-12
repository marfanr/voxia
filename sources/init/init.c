#include "initrd.h"
#include "loader.h"
#include <dev/cpu/apic/apic.h>
#include <dev/cpu/apic/ioapic.h>
#include <dev/cpu/int/idt.h>
#include <dev/cpu/pic/pic.h>
#include <dev/graphic/fb.h>
#include <dev/initrd/initrd.h>
#include <firmw/acpi/acpi.h>
#include <firmw/acpi/madt.h>
#include <firmw/acpi/rsdp.h>
#include <firmw/acpi/rsdt.h>
#include <firmw/display/edid.h>
#include <firmw/ehci/ehci.h>
#include <firmw/pci/pci.h>
#include <firmw/register.h>
#include <hal/cpu/gdt.h>
#include <hal/cpu/paging.h>
#include <hal/graphic/framebuffer.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/executable/elf.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/stivale2.h>
#include <libk/str/memset.h>
#include <libk/str/strncmp.h>
#include <libk/timer.h>
#include <memory/buddy_allocator.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

uintptr_t task_addr;
uint64_t highest_loaded_task_addr = 0;
uint64_t low_loaded_task_addr = 0;

extern void jump_usermode(uintptr_t addr);
extern void umode();

extern unsigned char userspace_start;
extern unsigned char userspace_end;

void setup_stack(uint8_t *stack, const char *argv[], const char *envp[]) {}

void syscall(unsigned long func, unsigned long arg1, unsigned long arg2,
             unsigned long arg3) {
  asm volatile("mov %0, %%rdi" : : "r"(arg1));
  asm volatile("mov %0, %%rsi" : : "r"(arg2));
  asm volatile("mov %0, %%rdx" : : "r"(arg3));
  asm volatile("mov %0, %%rax" : : "r"(func));
  asm volatile("int $0x73");

  // call task_addr as function
}

void task_switch() {
  char *txt = "Hello from userspace\n";
  // asm volatile("mov %0, %%rsi" : : "r"((unsigned long)0));
  // asm volatile("mov %0, %%rdx" : : "r"(txt));
  // asm volatile("mov %0, %%rdi" : : "r"((unsigned long)0));
  // asm volatile("mov %0, %%rax" : : "r"((unsigned long)0x1));
  // asm volatile("int $0x73");
  syscall(0x1, 1, "Hello from userspace\n", 20);

  uint64_t r8 = 0;
  asm volatile("mov %%r8, %0" : "=r"(r8));

  // call addr on  r8
  asm volatile("call *%0" : : "r"(r8));

  // // call task_addr as function
  // // ((void (*)(void))task_addr)();

  // void (*task)() = (void (*)())r8;
  // task();
  for (;;) {
  }
}

extern bool is_running_program;

/**
 * @brief Fungsi _start merupakan entry point dari kernel yang akan dijalankan
 * oleh bootloader. Fungsi ini akan dipanggil oleh bootloader setelah proses
 * booting selesai. Fungsi ini bertugas untuk melakukan inisialisasi awal
 * sebelum menjalankan kernel.
 *
 * @param stivale2_struct Pointer ke struktur stivale2_struct yang berisi
 * informasi dari bootloader.
 */
void _start(struct stivale2_struct *stivale2_struct) {
  serial_setup();

  // insialisasi memori allocator
  struct stivale2_struct_tag_memmap *memmap_info =
      (struct stivale2_struct_tag_memmap *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_MEMMAP_ID);
  phys_base_allocator_install(memmap_info);

  // inisialisasi kernel modules
  struct stivale2_struct_tag_modules *modules_info =
      (struct stivale2_struct_tag_modules *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_MODULES_ID);

  if (modules_info->module_count < 1) {
    serial_trace("error, tidak ada kernel module yang tersedia!!\n");
    for (;;)
      ;
  }

  // mencari initial ramdisk
  // todo(arfan): memindah kefile terpisah
  register initrd_module_t initrd_module;
  for (uint64_t i = 0; i < modules_info->module_count; i++) {
    struct stivale2_module *module = &modules_info->modules[i];
    if (strncmp(module->string, "boot:///initrd.tar", 18) == 0) {
      serial_send_string("\ninitrd found\n");
      initrd_module.start = module->begin;
      initrd_module.size = module->end - module->begin;
      break;
    }
  }

  // inisialisasi initrd (initial ramdisk)
  // initrd yaitu file yang di embed di kernel yang berisi keperluan kernel
  // seperti font, image, sub program, dan lain-lain
  char *font = initrd_load(initrd_module, "fonts/unifont.sfn");
  serial_trace("font name : 0x%x\n", font);

  initrd_init(modules_info);

  // setup framebuffer
  struct stivale2_struct_tag_framebuffer *framebuffer_info =
      (struct stivale2_struct_tag_framebuffer *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);

  // inisialisasi framebuffer
  register framebuffer_t fb = {
      .addr = framebuffer_info->framebuffer_addr,
      .width = framebuffer_info->framebuffer_width,
      .height = framebuffer_info->framebuffer_height,
      .pitch = framebuffer_info->framebuffer_pitch,
      .bpp = framebuffer_info->framebuffer_bpp,
  };

  fb_init(framebuffer_info, FB_COLOR_BLACK);

  paging_install();
  KDEBUG(DEBUG_LEVEL_INFO, "Virtual Memory Allocator initialized");

  gdt_setup();
  KDEBUG(DEBUG_LEVEL_INFO, "Global Descriptor Table initialized");

  // IDT
  idt_setup();
  KDEBUG(DEBUG_LEVEL_INFO, "Interrupt Descriptor Table initialized");

  // RSDP
  struct stivale2_struct_tag_rsdp *rsdp_info =
      (struct stivale2_struct_tag_rsdp *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_RSDP_ID);
  KDEBUG(DEBUG_LEVEL_INFO, "RSDP address: 0x%x", rsdp_info->rsdp);

  // ACPI
  acpi_setup(rsdp_info);

  // APIC
  apic_setup();
  ioapic_setup();

  // get edid strcut
  struct stivale2_struct_tag_edid *edid_info =
      (struct stivale2_struct_tag_edid *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_EDID_ID);

  //  print edid size
  edid_data_t *edid_data = (edid_data_t *)edid_info->edid_information;
  serial_trace("EDID size: %d\n", edid_info->edid_size);

  struct stivale2_struct_tag_boot_volume *bvol =
      (struct stivale2_struct_tag_boot_volume *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_BOOT_VOLUME_ID);

  serial_trace("\nbvol : 0x%x\n", (uint64_t)bvol->tag.identifier);
  serial_trace("bvol flags : 0%b\n", (uint64_t)bvol->flags);

  //   read all guid
  struct stivale2_guid guid = (struct stivale2_guid)bvol->guid;

  struct stivale2_struct_tag_kernel_file_v2 *kernel =
      (struct stivale2_struct_tag_kernel_file_v2 *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_KERNEL_FILE_V2_ID);

  serial_trace("\nkernel : 0x%x\n", (uint64_t)kernel->kernel_file);
  serial_trace("kernel size : %d\n", (uint64_t)kernel->kernel_size);

  struct stivale2_struct_tag_hhdm *hhdm =
      (struct stivale2_struct_tag_hhdm *)stivale2_get_tag(
          stivale2_struct, STIVALE2_STRUCT_TAG_HHDM_ID);
  serial_trace("\nhhdm : 0x%x\n", (uint64_t)hhdm->tag.identifier);

  install_fimware();
  fb_cls(FB_COLOR_BLACK);
  console_set_pos(0, 0);
  KDEBUG(DEBUG_LEVEL_INFO, "Welcome to NAYA 0.0.1 DEV");

  // // draw rectangle green on top
  // for (uint32_t i = 0; i < 100; i++) {
  //   for (uint32_t j = 0; j < 100; j++) {
  //     fb_put_pixel(i, j, 0x00FF00);
  //   }
  // }

  char *file = initrd_load(initrd_module, "modules/flui.elf");
  serial_trace("file addr : 0x%x\n", file);
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)file;
  serial_trace("ehdr : 0x%x\n", ((uint64_t)ehdr));
  // print EL IDENT
  for (uint32_t i = 0; i < 16; i++) {
    serial_trace("0x%x ", ehdr->e_ident[i]);
  }
  serial_trace("\n");

  Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)ehdr + ehdr->e_phoff);
  Elf64_Shdr *shdr = (Elf64_Shdr *)((uint64_t)ehdr + ehdr->e_shoff);
  serial_trace("\nshdr ofset : %d\n", ((uint64_t)ehdr->e_shoff));

  // Elf64_Shdr *str_tab = &shdr[ehdr->e_shstrndx];
  uint8_t *str_tab =
      (uint8_t *)((uint64_t)ehdr + shdr[ehdr->e_shstrndx].sh_offset);
  Elf64_Shdr *symtab = NULL;
  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (strncmp((char *)(str_tab + shdr[i].sh_name), ".symtab", 7) == 0) {
      serial_trace("symtab found at %d\n", i);
      symtab = &shdr[i];
      break;
    }
  }

  Elf64_Sym *symbols = (Elf64_Sym *)((uint64_t)file + symtab->sh_offset);
  for (int i = 0; i < symtab->sh_size / sizeof(Elf64_Sym); i++) {
    // if (strncmp((char *)(str_tab + symbols[i].st_name), "perty", 5) == 0) {
    serial_trace("Function %s: Start: 0x%x, Size: %d bytes, end : 0x%x\n",
                 str_tab + symbols[i].st_name, symbols[i].st_value,
                 symbols[i].st_size, symbols[i].st_value + symbols[i].st_size);
    // }
  }

  for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
    serial_trace("phdr type : 0x%x ", phdr->p_type);
    serial_trace("phdr paddr: 0x%x vaddr: 0x%x\n", ((uint64_t)phdr->p_paddr),
                 ((uint64_t)phdr->p_vaddr));
    phdr = (Elf64_Phdr *)((uint64_t)phdr + ehdr->e_phentsize);
  }

  serial_trace("\n");
  for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
    char *name = (char *)(str_tab + shdr->sh_name);
    serial_trace("shdr name : %s , ", name);
    serial_trace("shdr addr : 0x%x\n", shdr->sh_addr);
    shdr = (Elf64_Shdr *)((uint64_t)shdr + ehdr->e_shentsize);
  }
  // program entry offset
  uint64_t entry = ehdr->e_entry;
  serial_trace("entry : 0x%x\n", entry);

  page_t p = paging_get_highest_page_map();

  // load program to memory
  Elf64_Phdr *phdr2 = (Elf64_Phdr *)((uint64_t)ehdr + ehdr->e_phoff);

  int loop = 0;
  for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr2->p_type == 1) {
      void *a = (void *)VIRT2PHYS(phys_base_alloc(2));
      serial_trace("a : 0x%x\n", (uint64_t)a);

      serial_trace("phdr type : 0x%x ", phdr2->p_type);
      serial_trace("phdr paddr: 0x%x vaddr: 0x%x flag : 0x%x\n",
                   ((uint64_t)phdr2->p_paddr, phdr2->p_flags),
                   ((uint64_t)phdr2->p_vaddr));

      // experimental
      if (loop == 1) {
        low_loaded_task_addr = phdr2->p_vaddr;
      } else if (loop == 2) {
        highest_loaded_task_addr = phdr2->p_vaddr;
      }
      loop++;

      for (uint64_t j = 0; j < phdr2->p_memsz; j += 0x1000) {
        paging_mmap(p, phdr2->p_vaddr + j, (uint64_t)a + j, 0b111);
      }
      paging_reload(p);

      for (uint64_t j = 0; j < phdr2->p_memsz; j++) {
        *(uint8_t *)((uint64_t)a + j) =
            *(uint8_t *)(file + phdr2->p_offset + j);
      }
    }
    phdr2 = (Elf64_Phdr *)((uint64_t)phdr2 + ehdr->e_phentsize);
  }
  serial_trace("pmem size : 0x%x", (uint64_t)phdr2->p_memsz);

  // run

  // serial_trace("mmap test_user_function to user space\n");
  // uint64_t uspace_len = (uint64_t)&userspace_end -
  // (uint64_t)&userspace_start; uint8_t *uspace = (uint8_t
  // *)phys_base_alloc(4);
  // // copy test_user_function to user space
  // for (register uint32_t i = 0; i < 0x10000; i++) {
  //   uspace[i] = ((uint8_t *)task_switch)[i];
  // }
  // serial_trace("test_user_function : 0x%x\n", (uint64_t)task_switch);
  serial_trace("success entering userspace\n");

  // for (uint32_t i = 0; i < 0x10000; i += 0x1000) {
  //   paging_mmap(p, (uint64_t)2 * GB + i, (uint64_t)VIRT2PHYS(uspace) + i,
  //               0b111);
  // }

  // paging_reload(p);
  KDEBUG(DEBUG_LEVEL_INFO, "highest addr : 0x%x", highest_loaded_task_addr);
  is_running_program = 1;

  // save entry on register r2
  asm volatile("mov %0, %%r8" : : "r"(entry));

  jump_usermode(entry);
  // KDEBUG(DEBUG_LEVEL_INFO, "Jump to usermode");
  // syscall(0x1, 1, "Hello from userspace\n", 20);
  // just loop
  for (;;) {
  }
}
