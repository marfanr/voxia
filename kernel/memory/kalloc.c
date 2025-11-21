#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <hal/cpu/paging.h>
#include <libk/str.h>
#include <memory/kalloc.h>

#define KALLOC_BASE_ADDR 0xFFFFFE0000000000
static uintptr_t kalloc_next_addr = KALLOC_BASE_ADDR;

struct kalloc_cache
{
    size_t c_128_count;
    size_t c_256_count;
    size_t c_512_count;
    size_t c_1024_count;
    size_t c_2048_count;

    void *c_128;
    void *c_256;
    void *c_512;
    void *c_1024;
    void *c_2048;
};

static struct kalloc_cache cache;

#define MAX_FREED_VADDRS 512

typedef struct
{
    uintptr_t addr;
    size_t    size;
} freed_t;

static freed_t freed_vaddrs[MAX_FREED_VADDRS] = {0};
static size_t  freed_vaddr_count              = 0;

static uintptr_t
get_vaddr(size_t count)
{
    if (freed_vaddr_count > 0)
    {
        // return freed_vaddrs[--freed_vaddr_count].addr;
        for (size_t i = 0; i < freed_vaddr_count; i++)
        {
            if (freed_vaddrs[i].size >= count)
            {
                freed_vaddrs[i].size -= count;

                if (freed_vaddrs[i].size == 0)
                {
                    freed_vaddr_count--;
                }
                else
                {
                    freed_vaddrs[i].addr += count * BLOCK_SIZE;
                }
                return freed_vaddrs[i].addr;
            }
        }
    }
    uintptr_t addr = kalloc_next_addr;
    kalloc_next_addr += BLOCK_SIZE;
    return addr;
}

static void *
__alloc_4k(void)
{
    uintptr_t phys_addr = (uintptr_t)vxPhysBaseAlloc(1);
    uintptr_t virt_addr = get_vaddr(1);
    vxMmap(paging_get_highest_page_map(), virt_addr, phys_addr,
           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    vma_register((uintptr_t)phys_addr, (uintptr_t)virt_addr, BLOCK_SIZE);
    void *ret = (void *)virt_addr;
    return ret;
}

/**
 * 128
 * 256
 * 512
 * 1024
 * 2048
 */
void *
kalloc(size_t size)
{
    if (size == 0)
        return NULL;

    if (size <= 128)
    {
        if (cache.c_128_count == 0)
        {
            void *new_page = __alloc_4k();
            for (int i = 0; i < BLOCK_SIZE / 128; i++)
            {
                *(void **)((uintptr_t)new_page + i * 128) = cache.c_128;
                cache.c_128                               = (void *)((uintptr_t)new_page + i * 128);
                cache.c_128_count++;
            }
        }

        void *ret = cache.c_128;
        // serial_trace("...kalloc 128 returning 0x%x\n", (uint64_t)ret);
        // serial_trace("...kalloc 128 count now %d\n", cache.c_128_count - 1);
        cache.c_128 = *(void **)cache.c_128;
        cache.c_128_count--;
        return ret;
    }
    else if (size <= 256)
    {
        if (cache.c_256_count == 0)
        {
            void *new_page = __alloc_4k();
            for (int i = 0; i < BLOCK_SIZE / 256; i++)
            {
                *(void **)((uintptr_t)new_page + i * 256) = cache.c_256;
                cache.c_256                               = (void *)((uintptr_t)new_page + i * 256);
                cache.c_256_count++;
            }
        }

        void *ret   = cache.c_256;
        cache.c_256 = *(void **)cache.c_256;
        cache.c_256_count--;
        return ret;
    }
    else if (size <= 512)
    {
        if (cache.c_512_count == 0)
        {
            void *new_page = __alloc_4k();
            for (int i = 0; i < BLOCK_SIZE / 512; i++)
            {
                *(void **)((uintptr_t)new_page + i * 512) = cache.c_512;
                cache.c_512                               = (void *)((uintptr_t)new_page + i * 512);
                cache.c_512_count++;
            }
        }

        void *ret   = cache.c_512;
        cache.c_512 = *(void **)cache.c_512;
        cache.c_512_count--;
        return ret;
    }
    else if (size <= 1024)
    {
        if (cache.c_1024_count == 0)
        {
            void *new_page = __alloc_4k();
            for (int i = 0; i < BLOCK_SIZE / 1024; i++)
            {
                *(void **)((uintptr_t)new_page + i * 1024) = cache.c_1024;
                cache.c_1024 = (void *)((uintptr_t)new_page + i * 1024);
                cache.c_1024_count++;
            }
        }

        void *ret    = cache.c_1024;
        cache.c_1024 = *(void **)cache.c_1024;
        cache.c_1024_count--;
        return ret;
    }
    else if (size <= 2048)
    {
        if (cache.c_2048_count == 0)
        {
            void *new_page = __alloc_4k();
            for (int i = 0; i < BLOCK_SIZE / 2048; i++)
            {
                *(void **)((uintptr_t)new_page + i * 2048) = cache.c_2048;
                cache.c_2048 = (void *)((uintptr_t)new_page + i * 2048);
                cache.c_2048_count++;
            }
        }

        void *ret    = cache.c_2048;
        cache.c_2048 = *(void **)cache.c_2048;
        cache.c_2048_count--;
        return ret;
    }

    size_t allocate_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;

    uintptr_t phys_addr = (uintptr_t)vxPhysBaseAlloc(allocate_size);
    // serial_trace("phys addr 0x%x\n", phys_addr);
    uintptr_t vaddr = kalloc_next_addr;
    kalloc_next_addr += BLOCK_SIZE * allocate_size;
    vxMultipleMmap(paging_get_highest_page_map(), vaddr, phys_addr, allocate_size,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    vma_register((uintptr_t)phys_addr, (uintptr_t)vaddr, allocate_size);
    void *ret = (void *)vaddr;
    return ret;
}

void
kfree(void *ptr, size_t size)
{
    if (ptr == NULL || size == 0)
        return;

    memset(ptr, 0, size);

    if (size <= 128)
    {
        *(void **)ptr = cache.c_128;
        cache.c_128   = ptr;
        cache.c_128_count++;
    }
    else if (size <= 256)
    {
        *(void **)ptr = cache.c_256;
        cache.c_256   = ptr;
        cache.c_256_count++;
    }
    else if (size <= 512)
    {
        *(void **)ptr = cache.c_512;
        cache.c_512   = ptr;
        cache.c_512_count++;
    }
    else if (size <= 1024)
    {
        *(void **)ptr = cache.c_1024;
        cache.c_1024  = ptr;
        cache.c_1024_count++;
    }
    else if (size <= 2048)
    {
        *(void **)ptr = cache.c_2048;
        cache.c_2048  = ptr;
        cache.c_2048_count++;
    }
    else
    {
        // Free whole pages
        virtual_memory_t *v = vma_find((uintptr_t)ptr);
        if (!v)
            return;

        size_t allocate_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
        vxPhysBaseFree((void *)v->phys_address, allocate_size);
        paging_unmap_fill(paging_get_highest_page_map(), (uintptr_t)ptr, allocate_size);

        vma_unregister((uintptr_t)ptr);
        freed_vaddrs[freed_vaddr_count++] = (freed_t){
            .addr = (uintptr_t)ptr,
            .size = allocate_size,
        };
    }
}