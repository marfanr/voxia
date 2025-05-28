#ifndef __SYS__API_H__
#define __SYS__API_H__

#include <libk/type.h>

typedef struct {
  void (*draw_rect)(int x, int y, int w, int h, uint64_t color);
} graphic_api_t;

uint64_t api_graphic();
void api_setup();

#endif // __SYS__API_H__
