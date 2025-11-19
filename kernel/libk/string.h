#ifndef __LIBK__STRING_H__
#define __LIBK__STRING_H__

#include "libk/vector.h"
#include <libk/type.h>

typedef struct
{
    char    *c_str;
    uint32_t len;
    uint32_t cap;
} *string;

define_vector(string);
string    str(const char *str);
void      str_release(string str);
boolean_t stringcmp(string s1, string s2);
string    str_concat(string s, const char *suffix);
void      str_trim(string str);

#endif // __LIBK__STRING_H__