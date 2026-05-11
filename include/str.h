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
void memset(void* ptr, uint8_t value, size_t num);

/**
 * @brief Menghitung panjang string.
 *
 * Fungsi ini menghitung panjang string hingga karakter null pertama.
 *
 * @param s Pointer ke string.
 * @return Panjang string.
 */
size_t strlen(const char* s);

/**
 * @brief Membandingkan dua string secara leksikografis hingga n karakter.
 *
 * @param s1 Pointer ke string pertama.
 * @param s2 Pointer ke string kedua.
 * @param n Jumlah karakter yang akan dibandingkan.
 * @return Nilai negatif jika s1 < s2, nilai positif jika s1 > s2, dan 0 jika s1
 * = s2.
 */
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
char* itoa(int value, char* str, int base);

static inline void
explode(const char* path, const char delim, vector(string) * out) {
	// size_t len = strlen(path);
	// // serial_trace("exploding path len %d \n", len);

	// char* buf = (char*) kalloc(len + 1);
	// strcpy(buf, path);
	// buf[len] = 0;

	// char* rest = buf;
	// const char* token;

	// while ((token = strsep(&rest, delim)) != 0) {
	// 	// serial_trace("%s \n", rest);
	// 	if (strlen(token) > 0) {
	// 		kstring tmp = str(token);
	// 		vector_push_back(out, tmp);
	// 	}
	// }
	// kfree(buf, len + 1);
}

#ifdef __cplusplus
}
#endif

#endif // __STR_H__