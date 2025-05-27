#ifndef __LIBK__TYPE_H__
#define __LIBK__TYPE_H__

#ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ uint8_t;
#else
typedef unsigned char  uint8_t;
#endif

#ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ uint16_t;
#else
typedef unsigned short uint16_t;
#endif

typedef unsigned int    uint32_t;
typedef __UINT64_TYPE__ uint64_t;

typedef signed char  int8_t;
typedef signed short int16_t;
typedef signed int   int32_t;
typedef signed long  int64_t;

typedef __SIZE_TYPE__    size_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#define NULL 0

typedef struct {
    int counter;
} atomic_t;

typedef _Bool boolean_t;

#endif // __LIBK__TYPE_H__
