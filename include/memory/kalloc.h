#ifndef __MEMORY_KALLOC_H__
#define __MEMORY_KALLOC_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

void* kalloc(size_t size);
void kfree(void* ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif // __MEMORY_KALLOC_H__
