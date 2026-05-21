#include "init/init.h"
#include "hal/apic/apic.h"
#include "hal/cpu/interrupt.h"
#include "init/loader.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "notify.h"
#include "procc/proccess.h"
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
void render_bmp32_with_alpha(uint8_t* pixels, int width, int height, int new_w,
                             int new_h, int posx, int posy);

// static void kernel_init() {
// 	execve("/dev/initrd/sbin/runner.elf", 0, 0);
// 	LOG_INFO("KNIT", "kernel init...");
// 	vxThreadExit();
// }

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

	pmm_log_usage();
	KDEBUG(DEBUG_LEVEL_INFO, "Boot complete, entering idle loop...\n");

	wait_until_receive_notify("/vfs/root", 10000);

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

	// TODO: start scheduler on bsp

	// TODO: spawn /sbin/term
	// jump into userspace
	// execve("/sbin/term.elf", 0, 0);

	// // TEST
	// create_netdev("eth", NETDEV_TYPE_ETHERNET);

	// bind netdev to nic
	// auto dev = lookup_netdev("eth");
	// if (!dev) {
	// 	LOG2_ERROR("Netdev", "netdev gagal dibuat");
	// 	return;
	// }
	// dev->ops->bind_nic(dev, nic);

	// socket test, soon add a workqueue
	// socket_t* test_sock = 0;
	// vxSocket(AF_RAW, SOCK_RAW, 0, &test_sock);
	// if (test_sock) {
	// 	LOG2_DEBUG("init", "success create socket");

	// 	if (test_sock->ops->set_sockopt(test_sock, SOL_SOCKET,
	// 					SO_BINDTODEVICE, "eth", 0)
	// 	    == SOCK_OK) {
	// 		LOG2_DEBUG("init",
	// 			   "success bind socket into eth device");
	// 	}

	// 	sockaddr_in_t addr = {
	// 		.sin_family = AF_INET,
	// 		.sin_port = vxHtons(3000),
	// 		.sin_addr = vxInetAddr("127.0.0.1"),
	// 	};

	// 	test_sock->ops->bind(test_sock, &addr, sizeof(addr));

	// 	uint8_t* buffer = (uint8_t*) kalloc(2048);

	// 	while (true) {
	// 		int n = test_sock->ops->recv(test_sock, buffer, 2048);
	// 	}
	// }

	// LOG_INFO("INIT", "Hello from serial2_printf! Value: %d, okokok %x\n",
	//          42, 0x76);

	// serial2_flush();

	// task_initialize();

	// //  --- will be moved
	// //
	// //
	// // TEST RENDER BMP
	// time_counter_t counter;
	// vxTimerCounterInit(&counter);

	// //
	// auto img_fd = vxFileInternalOpen("/dev/initrd/media/boot/logo.bmp",
	// OPEN_MODE_R);

	// if (img_fd < 0)
	// {
	//     LOG_ERROR("BMP", "Failed to open image file");
	//     // goto end;
	// }

	// struct vfs_file_stats img_stats;
	// vxVFSFileStat(img_fd, &img_stats);
	// LOG_DEBUG("JPG", "image size : %d kb", img_stats.size / 1024);
	// vxTimerCounterInit(&counter);
	// uint8_t *img = (uint8_t *)kalloc(img_stats.size);

	// vxVFSRead(img_fd, img, img_stats.size);
	// LOG_DEBUG("VFS", "load bmp in %.4f ms",
	// vxTimerCounterCountInMs(&counter));

	// bmp_header_t *hdr = (bmp_header_t *)img;

	// if (hdr->signature != 0x4D42)
	//     LOG_WARN("BMP", "bukan BMP");
	// if (hdr->bpp != 24)
	//     LOG_WARN("BMP", "bpp bukan 24");

	// LOG_INFO("BMP", "compression : %d", hdr->compression);

	// int width  = hdr->width;
	// int height = hdr->height;
	// LOG_INFO("BMP", "width : %d", width);
	// LOG_INFO("BMP", "height : %d", height);

	// uint8_t *pixels = img + hdr->pixel_offset;
	// vxTimerCounterInit(&counter);

	// // bmp mask
	// if (hdr->compression != 0)
	// {
	//     uint8_t *ptr        = (uint8_t *)hdr;
	//     uint32_t red_mask   = *((uint32_t *)(ptr + 14 +
	//     hdr->dib_header_size + 0)); uint32_t green_mask = *((uint32_t
	//     *)(ptr + 14 + hdr->dib_header_size + 4)); uint32_t blue_mask  =
	//     *((uint32_t *)(ptr + 14 + hdr->dib_header_size + 8)); uint32_t
	//     alpha_mask = *((uint32_t *)(ptr + 14 + hdr->dib_header_size +
	//     12)); // opsional
	// }

	// double scale = 0.4;
	// int    w     = width * scale;
	// int    h     = height * scale;
	// vxTimerCounterInit(&counter);
	// render_bmp32_with_alpha(pixels, width, height, w, h,
	//                         ctx.framebuffer.framebuffer_width / 2 - w /
	//                         2, ctx.framebuffer.framebuffer_height / 2 - h
	//                         / 2);

	// LOG_DEBUG("BMP", "end render bmp in %.4f ms",
	// vxTimerCounterCountInMs(&counter)); LOG_INFO("INIT", "end of init");

	// rtc_initialize();

	// // start scheduler on BSP
	// // create kernel init
	// vxCreateThread((uintptr_t)kernel_init, 0, TASK_PRIORITY_HIGH, 0);
	// vxStartScheduler();

	INFLOOP;
}

// extern framebuffer_t* g__fb;

// void render_bmp32_with_alpha(uint8_t* pixels, int width, int height, int
// new_w, 			     int new_h, int posx, int posy) { 	int*
// srcx_table = kalloc(new_w * sizeof(int)); 	int* srcy_table = kalloc(new_h *
// sizeof(int));

// 	for (int x = 0; x < new_w; x++)
// 		srcx_table[x] = (x * width) / new_w;

// 	for (int y = 0; y < new_h; y++)
// 		srcy_table[y] = (y * height) / new_h;

// 	int row_size = width * 4; // BGRA
// 	for (int y = 0; y < new_h; y++) {
// 		int srcy = srcy_table[y];
// 		size_t row_idx = (height - 1 - srcy) * row_size;

// 		for (int x = 0; x < new_w; x++) {
// 			int srcx = srcx_table[x];
// 			size_t idx = row_idx + srcx * 4;

// 			pixel_t src = {.b = pixels[idx + 0],
// 				       .g = pixels[idx + 1],
// 				       .r = pixels[idx + 2],
// 				       .a = pixels[idx + 3]};
// 			put_pixel_alpha_fast(posx + x, posy + y, src);
// 		}
// 	}
// }

// cpp runtime stub

KERNEL_API void _ZdlPv(void* ptr) {
	// Simple operator delete implementation
	kfree(ptr, sizeof(ptr)); // Atau memory manager Anda
}

KERNEL_API void _ZdlPvm(void* ptr, size_t size) {
	// Operator delete dengan size
	kfree(ptr, size);
}

KERNEL_API
int atexit(__attribute__((unused)) void (*function)(void)) {
	// Stub implementation - return success
	LOG_DEBUG("LIBC", "atexit called, but not implemented");
	return 0;
}

// Di kernel code
KERNEL_API void __cxa_finalize(__attribute__((unused)) void* dso_handle) {
	// Stub implementation
	LOG_DEBUG("LIBC", "__cxa_finalize called, but not implemented");
}
