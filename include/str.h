#ifndef __STR_H__
#define __STR_H__

#include <string.h>
#include <type.h>
#include <vector.h>

#ifdef __cplusplus
extern "C" {
#endif

void memcopy(void* dest, void* src, size_t size);
void* memmove(void* dest, const void* src, size_t n);
void memset(void* ptr, int value, size_t num);
int memcmp(const void* s1, const void* s2, size_t n);
void to_lowercase(char* str);
size_t strlen(const char* s);
int strncmp(const char* s1, const char* s2, size_t n);
int strcmp(const char* s1, const char* s2);
void strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
void strcat(char* dest, const char* src);
char* strpbrk(const char* s, const char* accept);
char* strsep2(char** stringp, const char* delim);
const char* strsep(char** stringp, const char delim);
char* strchr(const char* s, int c);
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strtok_r(char* str, const char* delim, char** saveptr);

typedef const char* __str;
char* rtrim(char* str);
char* itoa(int64_t value, int base, char* str);
void* memchr(const void* buf, int c, size_t len);

extern void __memset32__(void* dst, int val, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __STR_H__