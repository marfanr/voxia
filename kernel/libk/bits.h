#ifndef __LIBK__BITS_H__
#define __LIBK__BITS_H__

// ambil bit ke-n dari byte b (0 = bit paling kiri / MSB)
// #define BIT_GET(b, n) (((b) >> (7 - (n))) & 1)

// // ambil bit ke-n dari byte b (jika 0 = LSB)
// #define BIT_GET_LSB(b, n) (((b) >> (n)) & 1)

// // ambil n bit dari posisi tertentu dalam byte (contoh ambil 3 bit dari MSB)
// #define BIT_RANGE(b, start, len) (((b) >> (8 - (start) - (len))) & ((1 << (len)) - 1))

#include <libk/type.h>

typedef struct
{
    uint8_t *data;
    uint64_t bit_pos;
    size_t   size;
} bitstream_t;

uint8_t  read_bit(bitstream_t *bs);
uint32_t read_bits(bitstream_t *bs, int n);
#endif // __LIBK__BITS_H__