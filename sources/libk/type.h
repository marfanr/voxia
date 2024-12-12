#ifndef __LIBK__TYPE_H__
#define __LIBK__TYPE_H__

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

typedef unsigned long size_t;
typedef unsigned long uintptr_t;

typedef _Bool bool;
enum { false, true };

#define NULL ((void *)0)

#endif // __LIBK__TYPE_H__