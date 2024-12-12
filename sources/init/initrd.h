#ifndef __INIT__INITRD_H__
#define __INIT__INITRD_H__

#include <libk/type.h>

typedef struct initrd_module {
  size_t size;
  uint64_t start;
} initrd_module_t;

typedef struct initrd_file {
  char name[100];
  uint32_t size;
  uint64_t data;
} initrd_file_t;

char * initrd_load(initrd_module_t module, const char *name);

#endif // __INIT__INITRD_H__
