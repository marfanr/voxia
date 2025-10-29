#include "hal/cpu/paging.h"
#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "modules/kernel_reader.h"
#include "vfs/vfs.h"
#include <libk/str.h>
#include <modules/voxmo.h>

static void
proccess_elf(uint8_t *data)
{

    elf_section_map sh_map = {0};
    elf_section_map_all(data, &sh_map);

    // mappin got plt
    elf_mmap_got(&sh_map);

    // load
    elf_load(data);

    Elf64_Dyn *dyn = elf_get_phdr_dynamic(data);
    if (dyn)
    {
        LOG_INFO("VOXMO", "dynamic section found at 0x%x", dyn);
        elf_dynamic_map map = {0};
        elf_dyn_map_all(dyn, data, &map);
        LOG_INFO("VOXMO", "strtab found at 0x%x", map.strtab);
        LOG_INFO("VOXMO", "needed found at 0x%x", map.needed);

        uintptr_t base = elf_find_base_addr(data);
        LOG_INFO("VOXMO", "base addr : 0x%x", base);

        if (map.needed)
        {
            char *lib_name = (char *)(map.strtab + map.needed);
            LOG_INFO("VOXMO", "lib name : %s", lib_name);
        }

        if (map.symbols)
        {
            LOG_INFO("VOXMO", "symbols found at 0x%x", map.symbols);
            LOG_INFO("VOXMO", "sym count %d", map.symcount);

            // relocation
            Elf64_Rela *plt_rela   = (Elf64_Rela *)map.jmprel;
            uint64_t    rela_count = map.relasz / sizeof(Elf64_Rela);
            LOG_INFO("VOXMO", "rela count %d", rela_count);
            for (uint32_t i = 0; i < rela_count; i++)
            {
                Elf64_Sym  *symbols = &map.symbols[Elf64_R_SYM(plt_rela[i].r_info)];
                const char *name    = (const char *)(map.strtab + symbols->st_name);
                LOG_INFO("VOXMO", "[offset 0x%x] symbol %s, value 0x%x, size %d",
                         plt_rela[i].r_offset, name, symbols->st_value, symbols->st_size);

                uint64_t *target = (uint64_t *)(base + plt_rela[i].r_offset);

                // resolve
                kernel_symbol *sym = kernel_resolve_symbol(name);
                if (sym)
                {
                    LOG_DEBUG("VOXMO", "resolved target 0x%x at 0x%x type %d", target, sym->value,
                              Elf64_R_TYPE(plt_rela[i].r_info));
                    switch (Elf64_R_TYPE(plt_rela[i].r_info))
                    {
                        case R_X86_64_RELATIVE:
                        case R_X86_64_64:
                            *target = base + plt_rela[i].r_addend;
                            break;

                        case R_X86_64_GLOB_DAT:
                        case R_X86_64_JUMP_SLOT:
                            *target = sym->value; // patch PLT
                            break;

                        case R_X86_64_COPY:
                            memcopy(target, (void *)sym->value, sym->size);
                            break;

                        case R_X86_64_32:
                        case R_X86_64_32S:
                            *(uint32_t *)target = (uint32_t)(sym->value + plt_rela[i].r_addend);
                            break;

                            // TLS types (thread-local storage)
                        case R_X86_64_DTPMOD64:
                        case R_X86_64_DTPOFF64:
                        case R_X86_64_TPOFF64:
                        case R_X86_64_TLSGD:
                        case R_X86_64_TLSLD:
                        case R_X86_64_DTPOFF32:
                        case R_X86_64_GOTTPOFF:
                        case R_X86_64_TPOFF32:
                        case R_X86_64_TLSDESC_CALL:
                        case R_X86_64_TLSDESC:
                            LOG_WARN("ELF", "TLS relocation not implemented");
                            break;

                        default:
                            LOG_ERROR("ELF", "Unsupported relocation type %d",
                                      Elf64_R_TYPE(plt_rela[i].r_info));
                    }
                    *target = sym->value;
                }
                else
                {
                    LOG_ERROR("VOXMO", "symbol %s not found", name);
                }
            }
        }
    }

    // call entry
    uintptr_t entry = elf_get_entry(data);
    LOG_INFO("VOXMO", "entry : 0x%x", entry);
    ((void (*)(void))entry)();
}

void
voxmo_register(const char *path)
{
    int fd = vfs_open(path, OPEN_MODE_R);
    if (fd < 0)
    {
        LOG_ERROR("VOXMO", "failed to open voxmo file %s", path);
        return;
    }

    LOG_INFO("VOXMO", "file opened %s on FD %d", path, fd);

    struct vfs_file_stats stats;
    vfs_fstat(fd, &stats);
    LOG_INFO("VOXMO", "file size : %d", stats.size);
    uint8_t *data = (uint8_t *)kalloc(stats.size);
    vfs_read(fd, data, stats.size);

    struct voxmo_metadata_header *header = (struct voxmo_metadata_header *)data;
    LOG_INFO("VOXMO", "header magic 0x%x", header->magic);
    LOG_INFO("VOXMO", "header version %d", header->version);
    LOG_INFO("VOXMO", "header len %d", (header->header_len));
    LOG_INFO("VOXMO", "file count %d", (header->file_counts));
    LOG_INFO("VOXMO", "author len %d", (header->author.length));

    char *author = (char *)((uintptr_t)data + header->author.pos);
    LOG_INFO("VOXMO", "author %s", author);

    char *main_file = (char *)((uintptr_t)data + header->main_file.pos);
    LOG_INFO("VOXMO", "main file %s", main_file);

    // Langkah 1: ambil pointer ke capability.count
    // uint16_t *cap_count_ptr = (uint16_t *)((uintptr_t)ptr + header->header_len -
    // sizeof(uint16_t));
    uint16_t cap_count = header->capability.count;

    // LOG_INFO("VOXMO", "Capability count = %d", header->capability.count);
    // LOG_INFO("VOXMO", "Capability pos = %d", header->capability.pos);
    struct voxmo_metadata_string *cap_array =
        (struct voxmo_metadata_string *)((uint64_t)data + header->capability.pos);

    for (uint16_t i = 0; i < cap_count; i++)
    {
        struct voxmo_metadata_string *cap = &cap_array[i];
        // LOG_INFO("VOXMO", "capability name pos %d", cap->pos);
        // LOG_INFO("VOXMO", "capability name len %d", cap->length);
        char *cap_name = (char *)((uintptr_t)data + cap->pos);
        LOG_INFO("VOXMO", "capability name %s", cap_name);
    }

    // find main
    boolean_t main_found = false;
    for (uint32_t i = 0; i < header->file_counts; i++)
    {
        struct voxmo_metadata_file *file =
            (struct voxmo_metadata_file *)(data + header->header_len +
                                           i * sizeof(struct voxmo_metadata_file));
        LOG_INFO("VOXMO", "file name pos %d", file->nama_file.pos);
        LOG_INFO("VOXMO", "file name len %d", file->nama_file.length);
        char *file_name = (char *)((uintptr_t)data + file->nama_file.pos);
        LOG_INFO("VOXMO", "file name %s", file_name);
        LOG_INFO("VOXMO", "file len %d", file->size);

        if (strncmp(file_name, main_file, header->main_file.length) == 0)
        {
            main_found = true;
            LOG_INFO("VOXMO", "main file found");
            uint8_t *main_data = (uint8_t *)kalloc(file->size);
            memcopy((void *)main_data, (void *)(data + file->offset), file->size);
            // elf_load(main_data);
            proccess_elf(main_data);

            // load
        }
    }

    if (!main_found)
    {
        LOG_ERROR("VOXMO", "main file not found");
        return;
    }
}
