#ifndef __LIBK__UNICODE_H__
#define __LIBK__UNICODE_H__

#include <type.h>

#define HIGH_SURROGATE_START 0xD800
#define HIGH_SURROGATE_END 0xDBFF
#define LOW_SURROGATE_START 0xDC00
#define LOW_SURROGATE_END 0xDFFF

void decode_utf16(const uint16_t* utf16, uint32_t* cp);
int encode_utf16(uint32_t cp, uint16_t* utf16);

int decode_utf8(const uint8_t* utf8, uint32_t* cp);

#endif // __LIBK__UNICODE_H__