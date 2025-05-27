#include "./library.h"
#include <hal/cpu/paging.h>
#include <libk/executable/elf.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <vfs/vfs.h>

static struct Library *libraries = NULL;

void library_add(const char *name, enum LibraryType type) {
    int                   fd = vfs_open(name, 0);
    struct vfs_file_stats stats;
    int                   fs_resp = vfs_fstat(fd, &stats);

    uint8_t *file = (uint8_t *)(phys_base_alloc(1 + stats.size / 4096));
    memset(file, 0, stats.size);
    vfs_read(fd, file, stats.size);

    struct Library *lib = (phys_base_alloc(1 + sizeof(struct Library *) / 4096));
    memset((void *)lib, 0, sizeof(struct Library));
    lib->name  = stats.name;
    lib->type  = type;
    lib->entry = (uintptr_t)file;

    if (libraries == 0) {
        libraries = lib;
    } else {
        struct Library *current = libraries;
        while (current->next != 0) {
            current = current->next;
        }
        current->next = lib;
    }
    serial_trace("loaded %s \n", stats.name);
}

struct Library *
library_load(const char *name) {
    struct Library *current = libraries;
    while (current != 0) {
        if (strncmp(current->name, name, strlen(name)) == 0) {
            return current;
        }
        current = current->next;
    }
    return 0;
}