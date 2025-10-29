#include "hal/cpu/paging.h"
#include "hal/graphic/graphic.h"
#include "libk/bmp.h"
#include "libk/debug/debug.h"
#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "modules/voxmo.h"
#include <init/loader.h>
#include <libk/str.h>

// don't mess up with this sequence
// #define SEQUENCE_INITIALS_LIST \
//     L(simd) \
//     L(gdt) \
//     L(interrupt) \
//     L(phys_base_allocator) \
//     L(paging) \
//     L(vma) \
//     L(block) \
//     L(descriptor) \
//     L(vfs) \
//     L(initrd) \
//     L(graphic) \
//     L(acpi) \ L(apic_timer)

// #define L(name)                                                                                    \
//     extern void name##_init(init_context_t *ctx);                                                  \
//     REGISTER_INIT_PRIORITY(name);

// SEQUENCE_INITIALS_LIST
// #undef L

extern boolean_t                          is_running_program;
extern void                               init_runtime();
extern void                               rust_main();
extern struct stivale2_struct_tag_memmap *saved_memmap_info;

void
__r()
{
    // call exit handler
    // asm("movq %%rax, %%rdi;\n"
    //     "movq $0x9, %%rax;\n"
    //     "int $0x73"
    //     :
    //     :
    //     : "rax", "rdi", "rsi", "rdx"); // Add rdx to the clobber list
    INFLOOP;
}

static init_context_t ctx = {};

void render_bmp32_with_alpha(uint8_t *pixels, int width, int height, int new_w, int new_h, int posx,
                             int posy);

// entry point of kernel
extern void
_start(struct stivale2_struct *stivale2_struct)
{
    serial_setup();
    build_context_from_stivale2(stivale2_struct, &ctx);

    LOG_INFO("INIT", "run all init");
    run_all_init_calls(&ctx);

    voxmo_register("/dev/initrd/modules/ehci.voxmo");

    KDEBUG(DEBUG_LEVEL_INFO, "init done");

    // TEST RENDER BMP
    int img_fd = vfs_open("/dev/initrd/media/boot/logo.bmp", OPEN_MODE_R);
    if (img_fd < 0)
    {
        LOG_ERROR("BMP", "Failed to open image file");
        // goto end;
    }
    struct vfs_file_stats img_stats;
    vfs_fstat(img_fd, &img_stats);
    LOG_DEBUG("JPG", "image size : %d kb", img_stats.size / 1024);
    uint8_t *img = (uint8_t *)kalloc(img_stats.size);
    vfs_read(img_fd, img, img_stats.size);

    bmp_header_t *hdr = (bmp_header_t *)img;

    if (hdr->signature != 0x4D42)
        LOG_WARN("BMP", "bukan BMP");
    if (hdr->bpp != 24)
        LOG_WARN("BMP", "bpp bukan 24");

    LOG_INFO("BMP", "compression : %d", hdr->compression);

    int width  = hdr->width;
    int height = hdr->height;
    LOG_INFO("BMP", "width : %d", width);
    LOG_INFO("BMP", "height : %d", height);

    // int row_size = width * 4;
    // int      row_size = ((width * 3 + 3) / 4) * 4;
    uint8_t *pixels = img + hdr->pixel_offset;

    // bmp mask
    if (hdr->compression != 0)
    {
        uint8_t *ptr        = (uint8_t *)hdr;
        uint32_t red_mask   = *((uint32_t *)(ptr + 14 + hdr->dib_header_size + 0));
        uint32_t green_mask = *((uint32_t *)(ptr + 14 + hdr->dib_header_size + 4));
        uint32_t blue_mask  = *((uint32_t *)(ptr + 14 + hdr->dib_header_size + 8));
        uint32_t alpha_mask = *((uint32_t *)(ptr + 14 + hdr->dib_header_size + 12)); // opsional
    }

    double scale = 0.4;
    int    w     = width * scale;
    int    h     = height * scale;
    render_bmp32_with_alpha(pixels, width, height, w, h,
                            ctx.framebuffer.framebuffer_width / 2 - w / 2,
                            ctx.framebuffer.framebuffer_height / 2 - h / 2);

    LOG_INFO("INIT", "end of init");
    INFLOOP;
}

void
render_bmp32_with_alpha(uint8_t *pixels, int width, int height, int new_w, int new_h, int posx,
                        int posy)
{
    int row_size = width * 4; // BGRA
    for (int y = 0; y < new_h; y++)
    {
        for (int x = 0; x < new_w; x++)
        {
            int    srcx = (x * width) / new_w;
            int    srcy = (y * height) / new_h;
            size_t idx  = (height - 1 - srcy) * row_size + srcx * 4;

            pixel_t src = {
                .b = pixels[idx + 0],
                .g = pixels[idx + 1],
                .r = pixels[idx + 2],
                .a = pixels[idx + 3],
            };

            put_pixel_alpha(posx + x, posy + y, src);
        }
    }
}