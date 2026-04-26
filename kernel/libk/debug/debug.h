#ifndef __LIBK__DEBUG_H_
#define __LIBK__DEBUG_H_

#include <libk/type.h>

typedef enum
{
    DEBUG_LEVEL_DEBUG,
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_WARN,
    DEBUG_LEVEL_ERROR
} DEBUG_LEVEL;

#define DEBUG_DEBUG_FG 0x00FF00

#define KDEBUG(...) kernel_debug_impl(__FILE__, __LINE__, __VA_ARGS__)

#define KASSERT(...) kernel_assert_impl(__FILE__, __LINE__)

void kernel_debug_impl(const char *file_, uint16_t line_num_, DEBUG_LEVEL level_,
                       const char *message_, ...);

void kernel_assert_impl(const char *file_, uint16_t line_num_);

#endif // __LIBK__DEBUG_H_