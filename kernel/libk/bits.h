#ifndef __LIBK__BITS_H__
#define __LIBK__BITS_H__

#include <type.h>

typedef struct {
	uint8_t* data;
	uint64_t bit_pos;
	size_t size;
} bitstream_t;

uint8_t read_byte(bitstream_t* bs);
uint8_t read_bit(bitstream_t* bs);
uint32_t read_bits(bitstream_t* bs, int n);
#endif // __LIBK__BITS_H__