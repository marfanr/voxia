#include "libk/type.h"
#include <libk/str.h>

extern int       __fast__strncmp__(const char *s1, const char *s2, size_t n);
extern boolean_t simd_has_avx;

int
strncmp(const char *s1, const char *s2, size_t n)
{
    if (!simd_has_avx)
    {
        while (n-- != 0)
        {
            if (*s1 != *s2++)
                return *(unsigned char *)s1 - *(unsigned char *)--s2;
            if (*s1++ == 0)
                break;
        }
        return 0;
    }

    return __fast__strncmp__(s1, s2, n);
}
