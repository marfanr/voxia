#ifndef __LIBK__OCT2BIN_H__
#define __LIBK__OCT2BIN_H__

inline static int oct2bin(unsigned char* str, int len) {
	int n = 0;
	unsigned char* c = str;
	while (len-- > 0) {
		n *= 8;
		n += *c - '0';
		c++;
	}
	return n;
}

#endif // __LIBK__OCT2BIN_H__