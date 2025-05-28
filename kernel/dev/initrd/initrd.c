// Deprecated; move to vfs
#include "initrd.h"
#include "libk/type.h"
#include <libk/fs/tar.h>
#include <libk/serial.h>
#include <libk/str/strncmp.h>

static uint8_t *initrd_base_;
static uint8_t *initrd_end_;

int oct2bin(unsigned char *str, int len) {
    int            n = 0;
    unsigned char *c = str;
    while (len-- > 0) {
        n *= 8;
        n += *c - '0';
        c++;
    }
    return n;
}

boolean_t
initrd_init(struct stivale2_struct_tag_modules *modules_tag) {
    for (uint64_t i = 0; i < modules_tag->module_count; i++) {
        struct stivale2_module *module = &modules_tag->modules[i];
        if (strncmp(module->string, "boot:///initrd.tar", 18) == 0) {
            serial_send_string("\ninitrd found\n");
            initrd_base_ = (uint8_t *)module->begin;
            initrd_end_  = (uint8_t *)module->end;
        }
    }
    return 1;
}

// find file in initrd
// TODO: add support for recursive search
char *
initrd_find_file(const char *name) {
    uint8_t *addr = initrd_base_;

    TarHeader *header = (TarHeader *)addr;
    serial_trace("initrd base: 0x%x\n", (uint64_t)addr);
    while (strncmp(header->ustar, "ustar", 5) == 0) {
        int size = oct2bin(header->size, 11);
        if (strncmp(header->filename, name, sizeof(name)) == 0) {
            serial_send_string(header->filename);
            serial_send_string(" loaded from rootdir\n");
            if (header->typeflag == '5') {
                uint8_t *subaddr = addr + 512;
                header           = (TarHeader *)subaddr;
                serial_send_string(header->filename);
                serial_send_string(" loaded from subdir\n");
                char *out = subaddr + 512;
                return out;
            }
            char *out = addr + 512;
            return out;
        }
        addr += (((size + 511) / 512) + 1) * 512;
        header = (TarHeader *)addr;
    }
}
