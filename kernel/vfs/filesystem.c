#include "filesystem.h"
#include <libk/str.h>

static filesystem_t *registered_filesystems = 0;

void
filesystem_register(const char *name, filesystem_t *fs)
{
    // fs->name = name;
    fs->next = 0;
    if (registered_filesystems == 0)
    {
        registered_filesystems = fs;
        return;
    }

    fs->next               = registered_filesystems;
    registered_filesystems = fs;
}

filesystem_t *
filesystem_find(const char *name)
{
    filesystem_t *curr = registered_filesystems;
    while (curr != 0)
    {
        if (strncmp(curr->name, name, strlen(name)) == 0)
            return curr;
        curr = curr->next;
    }
    return 0;
}