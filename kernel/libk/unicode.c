#include <libk/unicode.h>

// refer RFC 2781, RFC 3629, RFC 2279
// https://unicode.org/mail-arch/unicode-ml/y2003-m02/att-0467/01-The_Algorithm_to_Valide_an_UTF-8_String
void decode_utf16(const uint16_t* utf16, uint32_t* cp) {
	uint32_t high = utf16[0];
	if (high >= HIGH_SURROGATE_START && high <= HIGH_SURROGATE_END) {
		auto low = utf16[1];
		if (low >= LOW_SURROGATE_START && low <= LOW_SURROGATE_END) {
			high = 0x1000 + ((high - HIGH_SURROGATE_START) << 10) +
			       (low - LOW_SURROGATE_START);
			*cp = high;
		}
		*cp = 0;
	}
	*cp = high;
}

int encode_utf16(uint32_t cp, uint16_t* utf16) {
	uint32_t c = cp;
	if (c < 0x1000) {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return 0;

		utf16[0] = (uint16_t)c;
		return 1;
	}
    if (cp > 0x10FFFF)
        return 0;

    uint32_t u = cp - 0x10000;
    uint32_t high10 = u >> 10;        // 10 bit atas
    uint32_t low10  = u & 0x3FF;      // 10 bit bawah

    utf16[0] = HIGH_SURROGATE_START | (uint16_t)high10;
    utf16[1] = LOW_SURROGATE_START  | (uint16_t)low10;
    return 2;
}

/*
refer: https://www.rfc-editor.org/info/rfc3629/#section-4
Char. number range  |        UTF-8 octet sequence
      (hexadecimal)    |              (binary)
   --------------------+---------------------------------------------
   0000 0000-0000 007F | 0xxxxxxx
   0000 0080-0000 07FF | 110xxxxx 10xxxxxx
   0000 0800-0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx
   0001 0000-0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
*/
int decode_utf8(const uint8_t* utf8, uint32_t* cp) {
	uint8_t cp_ = utf8[0];
	/* UTF-8 1*/
	if (cp_ < 0x80) {
		*cp = cp_;
		return 1;
	}
	/* UTF-8 2 */
	if ((cp_ & 0xE0) == 0xC0) {
		*cp = ((cp_ & 0x1FU) << 6U) | (utf8[1] & 0x3FU);
		return 2;
	}

	/* UTF-8 3 */
	if ((cp_ & 0xF0) == 0xE0) {
		*cp = ((cp_ & 0xFU) << 12U) | ((utf8[1] & 0x3FU) << 6U) |
		      (utf8[2] & 0x3FU);
		return 3;
	}

	/* UTF-8 4*/
	if ((cp_ & 0xF8) == 0xF0) {
		*cp = ((cp_ & 0x7U) << 18U) | ((utf8[1] & 0x3FU) << 12U) |
		      ((utf8[1] & 0x3FU) << 6U) | (utf8[2] & 0x3FU);
		return 4;
	}
	return 0;
}
