#ifndef __LIBK__STRING_H__
#define __LIBK__STRING_H__

#include <libk/type.h>

typedef struct
{
    char    *c_str;
    uint32_t len;
    uint32_t cap;
} *string;

string    str(const char *str);
void      str_release(string str);
boolean_t stringcmp(string s1, string s2);

#endif // __LIBK__STRING_H__