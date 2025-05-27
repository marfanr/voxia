#include "initrd.h"
#include "libk/type.h"
#include "loader.h"
#include <dev/cpu/apic/apic.h>
#include <dev/cpu/apic/ioapic.h>
#include <dev/graphic/fb.h>
#include <dev/initrd/initrd.h>
#include <firmw/acpi/acpi.h>
#include <hal/cpu/gdt.h>
#include <hal/cpu/interrupt.h>
#include <hal/cpu/paging.h>
#include <hal/graphic/framebuffer.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/executable/elf.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/stivale2.h>
#include <libk/str.h>
#include <libk/timer.h>
#include <memory/kalloc.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <procc/library.h>
#include <procc/procc.h>
#include <procc/scheduler.h>
#include <procc/spawn.h>
#include <procc/task.h>
#include <sys/api.h>
#include <sys/descriptor.h>
#include <sys/ioforge/ioforge.h>
#include <vfs/vfs.h>

extern boolean_t is_running_program;
extern void      init_runtime();
extern void      rust_main();

extern void
__r()
{
    // call exit handler
    asm("movq %%rax, %%rdi;\n"
        "movq $0x9, %%rax;\n"
        "int $0x73"
        :
        :
        : "rax", "rdi", "rsi", "rdx"); // Add rdx to the clobber list
    for (;;)
        ;
}

// entry point of kernel
extern void
_start(struct stivale2_struct *stivale2_struct)
{
    serial_setup();
    gdt_setup();
    interrupt_setup();

    // initialize kernel modules
    struct stivale2_struct_tag_modules *modules_info =
        (struct stivale2_struct_tag_modules *)stivale2_get_tag(stivale2_struct,
                                                               STIVALE2_STRUCT_TAG_MODULES_ID);

    // this module must be exist
    if (modules_info->module_count < 1)
    {
        serial_trace("error, no kernel module found!!\n");
        for (;;)
            ;
    }

    // finding initrd module
    initrd_module_t initrd_module;
    for (uint64_t i = 0; i < modules_info->module_count; i++)
    {
        struct stivale2_module *module = &modules_info->modules[i];
        if (strncmp(module->string, "boot:///initrd.tar", 18) == 0)
        {
            serial_send_string("\ninitrd found\n");
            initrd_module.start = module->begin;
            initrd_module.size  = module->end - module->begin;
            break;
        }
    }

    // initialize memori allocator
    struct stivale2_struct_tag_memmap *memmap_info =
        (struct stivale2_struct_tag_memmap *)stivale2_get_tag(stivale2_struct,
                                                              STIVALE2_STRUCT_TAG_MEMMAP_ID);
    phys_base_allocator_install(memmap_info);

    serial_trace("initrd module found at 0x%x\n", initrd_module.start);

    // iniialize block device
    // block_install();
    // block_register_device("/block/initrd", initrd_block_impl(&initrd_module),
    //                       0);
    //
    // // initialize vfs
    // vfs_install();
    // descriptor_install();
    // vfs_register_fs("initrd", initrd_vfs_impl(&initrd_module), 0);
    // vfs_mount("/dev/initrd", "/block/initrd", "initrd");
    //
    // int                   font_fd = vfs_open("/dev/initrd/fonts/unifont.sfn",
    // O_RDONLY); struct vfs_file_stats stats; vfs_fstat(font_fd, &stats);
    // serial_trace("(init) font size : %d\n", stats.size);
    //
    // uint8_t *font = (uint8_t *)(phys_base_alloc(1 + stats.size / 4096));
    // memset((void *)font, 0, stats.size);
    // serial_trace("font address : 0x%x\n", font);
    // vfs_read(font_fd, font, stats.size);
    // serial_trace("read font success\n");
    //
    // paging_install();
    //
    // // setup framebuffer
    // struct stivale2_struct_tag_framebuffer *framebuffer_info = (struct
    // stivale2_struct_tag_framebuffer *)stivale2_get_tag(
    //     stivale2_struct, STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);
    // serial_trace("framebuffer address : 0x%x\n",
    //              framebuffer_info->framebuffer_addr);
    //
    // struct framebuffer *fb_info = (struct framebuffer *)(phys_base_alloc(1));
    // *fb_info                    = (struct framebuffer){
    //                        .addr   = framebuffer_info->framebuffer_addr,
    //                        .width  = framebuffer_info->framebuffer_width,
    //                        .height = framebuffer_info->framebuffer_height,
    //                        .pitch  = framebuffer_info->framebuffer_pitch,
    //                        .bpp    = framebuffer_info->framebuffer_bpp,
    //                        .font   = font,
    // };
    // serial_trace("framebuffer address : 0x%x\n", fb_info->addr);
    // serial_trace("framebuffer width : %d\n", fb_info->width);
    // framebuffer_setup(fb_info);
    // fb_init(framebuffer_info, FB_COLOR_BLACK);
    //
    // KDEBUG(DEBUG_LEVEL_INFO, "Framebuffer initialized\n");
    //
    // struct stivale2_struct_tag_rsdp *rsdp_info = (struct
    // stivale2_struct_tag_rsdp *)stivale2_get_tag(
    //     stivale2_struct, STIVALE2_STRUCT_TAG_RSDP_ID);
    // KDEBUG(DEBUG_LEVEL_INFO, "RSDP address: 0x%x\n", rsdp_info->rsdp);
    //
    // ACPI
    // acpi_setup(rsdp_info);
    // apic_setup();
    // ioapic_setup();

    // IO initialization
    // ioforge_init();

    // task_initialize();
    // library_add("/initrd/lib/libnayalib.so", LIBRARY_TYPE_DYNAMIC);

    // // spawn("/initrd/modules/runtimeinit.elf", 0, 0);
    // spawn("/initrd/modules/flui.elf", 0, 0);

    // pmm_log_usage();
    // // menjalankan scheduler (blocking)
    // scheduler_init();

    // just loop
    for (;;)
        ;
}
