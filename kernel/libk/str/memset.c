#include "memset.h"

/**
 * Mengatur setiap byte dalam blok memori ke nilai yang ditentukan.
 *
 * @param ptr   Pointer ke blok memori yang akan diisi.
 * @param value Nilai yang akan diatur (diinterpretasikan sebagai unsigned
 * char).
 * @param num   Jumlah byte yang akan diatur ke nilai tersebut.
 */
void
memset (void *ptr, uint8_t value, size_t num)
{
    uint8_t *ptr_ = (uint8_t *)ptr;
    for (size_t i = 0; i < num; i++)
        {
            *(ptr_ + i) = value;
        }
}