#ifndef __LIBK__BMP_H__
#define __LIBK__BMP_H__

#include <libk/type.h>

// Struktur BMP header minimal
typedef struct
{
    uint16_t signature; // "BM" = 0x4D42
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t pixel_offset;    // offset pixel data
    uint32_t dib_header_size; // biasanya 40
    int32_t  width;
    int32_t  height;
    uint16_t planes;      // harus 1
    uint16_t bpp;         // bits per pixel, harus 24
    uint32_t compression; // harus 0
    uint32_t image_size;  // bisa 0
    uint32_t xppm;
    uint32_t yppm;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_header_t;

#endif // __LIBK__BMP_H_