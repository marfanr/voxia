#ifndef __DEV__INITRD__INITRD_H_
#define __DEV__INITRD__INITRD_H_

#include <libk/stivale2.h>
#include <libk/type.h>

boolean_t initrd_init(struct stivale2_struct_tag_modules *modules_tag);
char     *initrd_find_file(const char *name);

#endif // __DEV__INITRD__INITRD_H_
