#ifndef __TYPE_H__
#define __TYPE_H__

#ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ uint8_t;
#else
typedef unsigned char uint8_t;
#endif

#ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ uint16_t;
#else
typedef unsigned short uint16_t;
#endif

#ifdef __UINT32_TYPE__
typedef __UINT32_TYPE__ uint32_t;
#else
typedef unsigned int uint32_t;
#endif

#ifdef __UINT64_TYPE__
typedef __UINT64_TYPE__ uint64_t;
#else
typedef unsigned long uint64_t;
#endif

#ifdef __INT8_TYPE__
typedef __INT8_TYPE__ int8_t;
#else
typedef signed char int8_t;
#endif

#ifdef __INT16_TYPE__
typedef __INT16_TYPE__ int16_t;
#else
typedef signed short int16_t;
#endif

#ifdef __INT32_TYPE__
typedef __INT32_TYPE__ int32_t;
#else
typedef signed int int32_t;
#endif

#ifdef __INT64_TYPE__
typedef __INT64_TYPE__ int64_t;
#else
typedef signed long int64_t;
#endif

#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long size_t;
#endif

#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef signed long ptrdiff_t;
#endif

#ifdef __INTPTR_TYPE__
typedef __INTPTR_TYPE__ intptr_t;
#else
#typedef signed long intptr_t;
#endif

#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ uintptr_t;
#else
typedef unsigned long uintptr_t;
#endif

#define NULL 0
#define nullptr 0

typedef struct {
	int counter;
} atomic_t;

// boolean
#define false 0
#define true 1
#ifdef __BOOL_TYPE__
typedef __BOOL_TYPE__ boolean_t;
#else
typedef uint8_t boolean_t;
#endif

// TODO: move this to internal kernel
#define KERNEL_API                                                             \
	__attribute__((used, visibility("default"), section(".export")))

#define DEPRECATED                                                             \
	__attribute__((deprecated("This function is deprecated and may be "    \
				  "removed in future versions.")))

#define UNUSED(x) (void)(x)

#if defined(__clang__) || defined(__GNUC__)
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
#define offsetof(type, member) ((size_t) & (((type*) 0)->member))
#endif

#define container_of(ptr, type, member)                                        \
	((type*) ((uintptr_t) (ptr) - offsetof(type, member)))

#endif // __TYPE_H__
