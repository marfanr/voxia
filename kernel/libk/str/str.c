#include "libk/simd.h"
#include "type.h"
#include <str.h>
#include <vector.h>

extern void __fast__memcpy__(void* dst, void* val, size_t len);
extern void __fast_memset__(void* dst, int val, size_t len);
extern int __fast__strncmp__(const char* s1, const char* s2, size_t n);
extern void* __fast__memchr__(const void* buf, int c, size_t len);

KERNEL_API int strcmp(const char* s1, const char* s2) {
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(unsigned char*)s1 - *(unsigned char*)s2;
}

KERNEL_API int strncmp(const char* s1, const char* s2, size_t n) {
#ifdef VOXIA_USE_FAST_STR
	return __fast__strncmp__(s1, s2, n);
#endif
	while (n-- != 0) {
		if (*s1 != *s2++)
			return *(unsigned char*)s1 - *(unsigned char*)--s2;
		if (*s1++ == 0)
			break;
	}
	return 0;
}

KERNEL_API void* memchr(const void* buf, int c, size_t len) {
#ifdef VOXIA_USE_FAST_STR
	return __fast__memchr__(buf, c, len);
#endif
	const unsigned char* p = (const unsigned char*)buf;
	const unsigned char target = (unsigned char)c;
	while (len--) {
		if (*p == target)
			return (void*)p;
		p++;
	}
	return NULL;
}

KERNEL_API int memcmp(const void* s1, const void* s2, size_t n) {
	const unsigned char* a = (const unsigned char*)s1;
	const unsigned char* b = (const unsigned char*)s2;

	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			return (int)a[i] - (int)b[i];
		}
	}

	return 0;
}

void to_lowercase(char* str) {
	while (*str) {
		if (*str >= 'A' && *str <= 'Z') {
			*str += ('a' - 'A');
		}
		str++;
	}
}

// warn: modifies the input string
char* rtrim(char* str) {
	size_t i = strlen(str) - 1;
	while (i >= 0 && str[i] == ' ') {
		str[i] = 0;
		i--;
	}
	return str;
}

KERNEL_API void strcpy(char* dest, const char* src) {
	while (*src) {
		*dest++ = *src++;
	}
	*dest = 0;
}

KERNEL_API char* strncpy(char* dest, const char* src, size_t n) {
	size_t i;
	for (i = 0; i < n && src[i] != '\0'; i++)
		dest[i] = src[i];
	for (; i < n; i++)
		dest[i] = '\0';
	return dest;
}

KERNEL_API void strcat(char* dest, const char* src) {
	while (*dest)
		dest++;
	strcpy(dest, src);
}

KERNEL_API size_t strlen(const char* s) {
	size_t len = 0;
	while (s[len]) {
		len++;
	}
	return len;
}

KERNEL_API char* strchr(const char* s, int c) {
	while (*s != (char)c) {
		if (!*s++)
			return 0;
	}
	return (char*)s;
}

KERNEL_API size_t strspn(const char* s, const char* accept) {
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

KERNEL_API size_t strcspn(const char* s, const char* reject) {
	size_t count = 0;
	while (*s) {
		if (strchr(reject, *s++))
			return count;
		count++;
	}
	return count;
}

KERNEL_API char* strtok_r(char* str, const char* delim, char** saveptr) {
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

KERNEL_API void memset(void* ptr, int value, size_t num) {
#ifdef VOXIA_USE_FAST_STR
	return __fast_memset__(ptr, value, num);
#endif

	uint8_t* ptr_ = (uint8_t*)ptr;

	uint64_t fill = 0;
	for (size_t i = 0; i < 8; i++) {
		fill <<= 8;
		fill |= (uint64_t)value;
	}

	while (num > 0 && ((uintptr_t)ptr_ & 7)) {
		*ptr_++ = (uint8_t)value;
		num--;
	}

	size_t blocks = num / 8;
	size_t tail = num % 8;

	for (size_t i = 0; i < blocks; i++) {
		memcopy(ptr_ + (i * 8), &fill, 8);
	}

	ptr_ += blocks * 8;

	for (size_t i = 0; i < tail; i++)
		ptr_[i] = (uint8_t)value;

	return;
}

char* strpbrk(const char* s, const char* accept) {
	if (!s || !accept) {
		return NULL;
	}

	while (*s != '\0') {
		const char* a = accept;

		while (*a != '\0') {
			if (*a == *s) {
				return (char*)s;
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


KERNEL_API void memcopy(void* dest, void* src, size_t size) {
#ifdef VOXIA_USE_FAST_STR
	return __fast__memcpy__(dest, src, size);
#endif
	uint8_t* d = (uint8_t*)dest;
	uint8_t* s = (uint8_t*)src;
	for (size_t i = 0; i < size; i++) {
		d[i] = s[i];
	}
}

KERNEL_API void* memmove(void* dest, const void* src, size_t n) {
	unsigned char* d = (unsigned char*)dest;
	const unsigned char* s = (const unsigned char*)src;
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

char* itoa(int64_t value, int base) {
	static char str[32];
	if (base < 2 || base > 36) {
		*str = '\0';
		return str;
	}

	if (value == 0) {
		str[0] = '0';
		str[1] = '\0';
		return str;
	}

	int64_t tmp = 0;
	char* last = str;
	char* start = str;

	while (value) {
		tmp = value;
		value /= base;

		int64_t digit = tmp - value * base;

		*last++ =
		    (char)((digit < 10) ? ('0' + digit) : ('a' + (digit - 10)));
	}

	*last = '\0';

	// reverse
	last--;

	while (start < last) {
		char swap = *start;
		*start++ = *last;
		*last-- = swap;
	}

	return str;
}
