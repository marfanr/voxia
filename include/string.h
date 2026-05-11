#ifndef __LIBK__STRING_H__
#define __LIBK__STRING_H__

#include <vector.h>
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char* c_str;
	uint32_t len;
	uint32_t cap;
}* kstring;

define_vector(kstring);
kstring str(const char* str);
void str_release(kstring str);
boolean_t stringcmp(kstring s1, kstring s2);
kstring str_concat(kstring s, const char* suffix);
void str_trim(kstring str);

#ifdef __cplusplus
}
#endif

#endif // __LIBK__STRING_H__