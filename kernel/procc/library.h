#ifndef __PROCC_LIBRARY_H
#define __PROCC_LIBRARY_H

#include <libk/type.h>

enum LibraryType {
    LIBRARY_TYPE_STATIC,
    LIBRARY_TYPE_DYNAMIC
};

struct Library {
    char            *name;
    boolean_t        is_loaded;
    uintptr_t        entry;
    enum LibraryType type;
    struct Library  *next;
};

void            library_add(const char *name, enum LibraryType type);
struct Library *library_load(const char *name);

#endif // __PROCC_LIBRARY_H
