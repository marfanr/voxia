#ifndef __LIBK__VECTOR_H_
#define __LIBK__VECTOR_H_

#include <libk/type.h>
#include <memory/kalloc.h>

#define VECTOR_MINIMUM_ITEM 5

#define vector(T) struct vector_##T
#define define_vector(T)                                                                           \
    vector(T)                                                                                      \
    {                                                                                              \
        T     *data;                                                                               \
        size_t size;                                                                               \
        size_t capacity;                                                                           \
        size_t alloc_size;                                                                         \
    }

/* fungsi dasar */
#define vector_init(v)                                                                             \
    do                                                                                             \
    {                                                                                              \
        (v)->alloc_size = sizeof(typeof(*(v)->data));                                              \
        (v)->data       = (typeof(*(v)->data) *)kalloc(VECTOR_MINIMUM_ITEM * (v)->alloc_size);     \
        (v)->capacity   = VECTOR_MINIMUM_ITEM;                                                     \
        (v)->size       = 0;                                                                       \
    } while (0)

#define vector_expand_capacity(v)                                                                   \
    do                                                                                              \
    {                                                                                               \
        size_t            new_capacity = (v)->capacity ? (v)->capacity * 2 : 4;                     \
        typeof((v)->data) new_data     = (typeof((v)->data))kalloc(new_capacity * (v)->alloc_size); \
                                                                                                    \
        if ((v)->data)                                                                              \
        {                                                                                           \
            memcopy(new_data, (v)->data, (v)->capacity *(v)->alloc_size);                           \
            kfree((v)->data, (v)->capacity *(v)->alloc_size);                                       \
        }                                                                                           \
                                                                                                    \
        (v)->data     = new_data;                                                                   \
        (v)->capacity = new_capacity;                                                               \
    } while (0)

#define vector_push_back(v, val)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if ((v)->size >= (v)->capacity)                                                            \
            vector_expand_capacity((v));                                                           \
        (v)->data[(v)->size++] = (val);                                                            \
    } while (0)

#define vector_destroy(v)                                                                          \
    do                                                                                             \
    {                                                                                              \
        kfree((v)->data, (v)->capacity *(v)->alloc_size);                                          \
    } while (0)

#define vector_clear(v)                                                                            \
    do                                                                                             \
    {                                                                                              \
        (v)->size = 0;                                                                             \
    } while (0)

#define vector_pop_back(v)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if ((v)->size == 0)                                                                        \
            return NULL;                                                                           \
        (typeof((v)->data))*ret =                                                                  \
            (typeof((v)->data))((uint8_t *)(v)->data + ((v)->size - 1) * (v)->alloc_size);         \
        (v)->size--;                                                                               \
        return ret;                                                                                \
    } while (0)

typedef struct
{
    void  *data; // Pointer to the array of elements
    size_t size;
    size_t capacity;   // Maximum number of elements the vector can hold before resizing
    size_t alloc_size; // Size of each element in bytes
} vector_T;

#endif // __LIBK__VECTOR_H_