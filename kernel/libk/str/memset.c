#include "libk/type.h"
#include <libk/str.h>

extern void      __fast_memset__(void *dst, int val, size_t len);
extern void      __fast_memset_aligned__(void *dst, int val, size_t len);
extern boolean_t simd_enabled;

/**
 * Mengatur setiap byte dalam blok memori ke nilai yang ditentukan.
 *
 * @param ptr   Pointer ke blok memori yang akan diisi.
 * @param value Nilai yang akan diatur (diinterpretasikan sebagai unsigned
 * char).
 * @param num   Jumlah byte yang akan diatur ke nilai tersebut.
 */
void
memset(void *ptr, uint8_t value, size_t num)
{
    if (!simd_enabled)
    {
        uint8_t *ptr_ = (uint8_t *)ptr;

        uint64_t fill = 0;
        for (size_t i = 0; i < 8; i++)
        {
            fill <<= 8;
            fill |= value;
        }

        size_t blocks = num / 8;
        size_t tail   = num % 8;

        uint64_t *p64 = (uint64_t *)ptr_;
        for (size_t i = 0; i < blocks; i++)
            p64[i] = fill;

        ptr_ += blocks * 8;
        for (size_t i = 0; i < tail; i++)
            ptr_[i] = value;
    }

    // check is ptr alignmen 32
    if (((uintptr_t)ptr & 31) == 0)

        __fast_memset_aligned__(ptr, value, num);
    else
        __fast_memset__(ptr, value, num);
}