#include "api.h"
#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

graphic_api_t* g__graphic_api__ = 0;

static void draw_rect(int x, int y, int w, int h, uint64_t color) {
	serial_trace("draw_rect x: %d y: %d w: %d h: %d color: %d\n", x, y, w,
		     h, color);
	for (int i = 0; i < h; i++) {
		for (int j = 0; j < w; j++) {
			// fb_put_pixel(x + j, y + i, color);
		}
	}
}

void api_setup() {
	g__graphic_api__ = (graphic_api_t*) vxPhysBaseAlloc(
		1 + sizeof(graphic_api_t) / 4096);
	g__graphic_api__->draw_rect = draw_rect;
}

uint64_t api_graphic() {
	return (uint64_t) VIRT2PHYS((uint64_t) g__graphic_api__);
}
