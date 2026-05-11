#include <str.h>
#include <vector.h>
#include "memory/kalloc.h"
#include "type.h"

extern void __fast__memcpy__(void* dst, void* val, size_t len);
extern void __fast__memcpy_aligned__(void* dst, void* val, size_t len);
extern void __fast_memset__(void* dst, int val, size_t len);
extern void __fast_memset_aligned__(void* dst, int val, size_t len);
extern int __fast__strncmp__(const char* s1, const char* s2, size_t n);
extern boolean_t simd_has_avx;

int strcmp(const char* s1, const char* s2) {
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(unsigned char*) s1 - *(unsigned char*) s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
	if (!simd_has_avx) {
		while (n-- != 0) {
			if (*s1 != *s2++)
				return *(unsigned char*) s1
				       - *(unsigned char*) --s2;
			if (*s1++ == 0)
				break;
		}
		return 0;
	}

	return __fast__strncmp__(s1, s2, n);
}

// warn: modifies the input string
char* rtrim(char* str) {
	int i = strlen(str) - 1;
	while (i >= 0 && str[i] == ' ') {
		str[i] = 0;
		i--;
	}
	return str;
}

void strcpy(char* dest, const char* src) {
	while (*src) {
		*dest++ = *src++;
	}
	*dest = 0;
}

char* strncpy(char* dest, const char* src, size_t n) {
	size_t i;
	for (i = 0; i < n && src[i] != '\0'; i++)
		dest[i] = src[i];
	for (; i < n; i++)
		dest[i] = '\0';
	return dest;
}

void strcat(char* dest, const char* src) {
	while (*dest)
		dest++;
	strcpy(dest, src);
}

size_t strlen(const char* s) {
	size_t len = 0;
	while (s[len]) {
		len++;
	}
	return len;
}

char* strchr(const char* s, int c) {
	while (*s != (char) c) {
		if (!*s++)
			return 0;
	}
	return (char*) s;
}

size_t strspn(const char* s, const char* accept) {
	const char* p = s;
	const char* a;
	size_t count = 0;
	for (; *p != '\0'; ++p) {
		for (a = accept; *a != '\0'; ++a) {
			if (*p == *a)
				break;
		}
		if (*a == '\0')
			return count;
		++count;
	}
	return count;
}

size_t strcspn(const char* s, const char* reject) {
	size_t count = 0;
	while (*s) {
		if (strchr(reject, *s++))
			return count;
		count++;
	}
	return count;
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
	char* end;
	if (str == NULL)
		str = *saveptr;
	if (*str == '\0') {
		*saveptr = str;
		return NULL;
	}
	str += strspn(str, delim);
	if (*str == '\0') {
		*saveptr = str;
		return NULL;
	}
	end = str + strcspn(str, delim);
	if (*end == '\0') {
		*saveptr = end;
		return str;
	}
	*end = '\0';
	*saveptr = end + 1;
	return str;
}

void KERNEL_API memset(void* ptr, uint8_t value, size_t num) {
	if (!simd_has_avx) {
		uint8_t* ptr_ = (uint8_t*) ptr;

		uint64_t fill = 0;
		for (size_t i = 0; i < 8; i++) {
			fill <<= 8;
			fill |= value;
		}

		size_t blocks = num / 8;
		size_t tail = num % 8;

		uint64_t* p64 = (uint64_t*) ptr_;
		for (size_t i = 0; i < blocks; i++)
			p64[i] = fill;

		ptr_ += blocks * 8;
		for (size_t i = 0; i < tail; i++)
			ptr_[i] = value;

		return;
	}

	// check is ptr alignmen 32
	if (((uintptr_t) ptr % 32) == 0)
		__fast_memset_aligned__(ptr, value, num);
	else
		__fast_memset__(ptr, value, num);
}

char* strpbrk(const char* s, const char* accept) {
	if (!s || !accept) {
		return NULL;
	}

	while (*s != '\0') {
		const char* a = accept;

		while (*a != '\0') {
			if (*a == *s) {
				return (char*) s;
			}
			a++;
		}
		s++;
	}

	return NULL;
}

char* strsep2(char** stringp, const char* delim) {
	char* start = *stringp;
	char* p;

	if (start == NULL) {
		return NULL;
	}

	// strpbrk mencari karakter apa pun yang ada di dalam string delim
	p = strpbrk(start, delim);

	if (p) {
		*p = '\0';
		*stringp = p + 1;
	} else {
		*stringp = NULL;
	}

	return start;
}

const char* strsep(char** str, const char delim) {
	if (*str == 0 || **str == '\0') {
		return 0;
	}
	char* start = *str;
	char* end = start;

	while (*end && *end != delim) {
		end++;
	}

	if (*end) {
		*end = 0;
		*str = end + 1;
	} else {
		*str = 0;
	}

	return start;
}

void memcopy(void* dest, void* src, size_t size) {
	if (!simd_has_avx) {
		uint8_t* d = (uint8_t*) dest;
		uint8_t* s = (uint8_t*) src;
		for (size_t i = 0; i < size; i++) {
			d[i] = s[i];
		}
		return;
	}

	if ((((uintptr_t) dest & 31) == 0) && (((uintptr_t) src & 31) == 0)) {
		__fast__memcpy_aligned__(dest, src, size);
		return;
	}
	__fast__memcpy__(dest, src, size);
}

void* memmove(void* dest, const void* src, size_t n) {
	unsigned char* d = (unsigned char*) dest;
	const unsigned char* s = (const unsigned char*) src;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}

char* itoa(int value, char* str, int base) {
	if (base < 2 || base > 36) {
		*str = '\0';
		return str;
	}
	int temp = 0;
	char* last = str;
	char* start = str;

	while (value) {
		temp = value;
		value /= base;
		int digit = temp - value * base;
		*last++ = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
	}
	*last = '\0';

	// reverse
	{
		last--;
		while (start < last) {
			char temp = *start;
			*start++ = *last;
			*last-- = temp;
		}
	}

	return str;
}