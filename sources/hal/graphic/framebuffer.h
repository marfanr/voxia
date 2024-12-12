#ifndef __HAL_GRAPHIC_FRAMEBUFFER_H__
#define __HAL_GRAPHIC_FRAMEBUFFER_H__

#include <libk/type.h>

typedef struct framebuffer {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;
  uint32_t addr;
} framebuffer_t;

typedef struct framebuffer_handler {

} framebuffer_handler_t;

framebuffer_handler_t framebuffer_setup(framebuffer_t fb);

#endif // __HAL_GRAPHIC_FRAMEBUFFER_H__