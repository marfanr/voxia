#ifndef __MEMORY_KALLOC_H__
#define __MEMORY_KALLOC_H__

#include <libk/type.h>

void *kalloc(size_t size);
void  kfree(void *ptr, size_t size);
void  kalloc_log();

#endif // __MEMORY_KALLOC_H__
