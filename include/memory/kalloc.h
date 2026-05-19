#ifndef __MEMORY_KALLOC_H__
#define __MEMORY_KALLOC_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KALLOC_REDZONE_SIZE 16
#define KALLOC_REDZONE_MAGIC 0xFDEAABEEU


void* kalloc(size_t size);
void kfree(void* ptr, size_t size);
void kfree2(void* ptr);

typedef struct {
	size_t size;	/* original requested size */
	uint32_t magic; /* magic number for validation */
	uint32_t _pad;	/* explicit padding untuk ensure 16 bytes */
} kalloc_metadata_t;

#ifdef __cplusplus
}
#endif

#endif // __MEMORY_KALLOC_H__
