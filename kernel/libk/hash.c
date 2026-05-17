#include <hash.h>

uint64_t hash(const char* str, size_t max_size) {
	uint64_t h = 0xcbf29ce484222325ULL;

	const char* p = str;
	while (*p) {
		h ^= (uint64_t) (unsigned char) *p++;
		h *= 0x9e3779b97f4a7c15ULL;
	}
	h ^= h >> 32;

	return max_size ? (h % max_size) : h;
}

uint32_t hash32(const char* str, size_t max_size) {
	uint32_t h = 0xcbf29ce4ULL;

	const char* p = str;
	while (*p) {
		h ^= (uint32_t) (unsigned char) *p++;
		h *= 0x9e3779bULL;
	}

	return max_size ? (h % max_size) : h;
}