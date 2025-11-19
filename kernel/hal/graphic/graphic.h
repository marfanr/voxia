#ifndef __HAL__GRAPHIC__GRAPHIC_H__
#define __HAL__GRAPHIC__GRAPHIC_H__

#include <libk/type.h>

typedef struct
{
    uint8_t b, g, r, a;
} pixel_t;

void put_pixel(int x, int y, uint32_t color);
void put_pixel_alpha(int x, int y, pixel_t src);
void vxPutc(char c, int x, int y, uint32_t fg, uint32_t bg);
void put_pixel_alpha_fast(int x, int y, pixel_t src);
#endif // __HAL__GRAPHIC__GRAPHIC_H__