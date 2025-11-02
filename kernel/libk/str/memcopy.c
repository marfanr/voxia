#include <libk/str.h>

extern boolean_t simd_enabled;
extern void      __fast__memcpy__(void *dst, void *val, size_t len);
extern void      __fast__memcpy_aligned__(void *dst, void *val, size_t len);

void
memcopy(void *dest, void *src, size_t size)
{
    if (!simd_enabled)
    {
        uint8_t *d = (uint8_t *)dest;
        uint8_t *s = (uint8_t *)src;
        for (size_t i = 0; i < size; i++)
        {
            d[i] = s[i];
        }
        return;
    }

    if ((((uintptr_t)dest & 31) == 0) && (((uintptr_t)src & 31) == 0))
    {
        __fast__memcpy_aligned__(dest, src, size);
        return;
    }
    __fast__memcpy__(dest, src, size);
}
