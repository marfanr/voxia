#ifndef __HAL_GRAPHIC_FRAMEBUFFER_H__
#define __HAL_GRAPHIC_FRAMEBUFFER_H__

#include <type.h>

typedef struct __attribute__((aligned(32))) {
	uint64_t framebuffer_addr;
	uint16_t framebuffer_width;
	uint16_t framebuffer_height;
	uint16_t framebuffer_pitch;
	uint16_t framebuffer_bpp;

	uint8_t red_mask_size;
	uint8_t red_mask_shift;
	uint8_t green_mask_size;
	uint8_t green_mask_shift;
	uint8_t blue_mask_size;
	uint8_t blue_mask_shift;
} framebuffer_t;

#endif // __HAL_GRAPHIC_FRAMEBUFFER_H__