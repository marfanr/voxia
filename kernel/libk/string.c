#include <libk/str.h>
#include <libk/string.h>
#include <memory/kalloc.h>

string
str(const char *src)
{
    size_t len = strlen(src);
    // alokasikan struct + isi string dalam satu blok memori
    string s = (string)kalloc(sizeof(*s) + len + 1);

    // offset c_str ke bagian setelah struct
    s->c_str = (char *)((uint8_t *)s + sizeof(*s));
    s->len   = len;
    s->cap   = len + 1;

    strcpy(s->c_str, src);
    return s;
}

void
str_release(string str)
{
    kfree(str, sizeof(*str) + str->cap);
}

boolean_t
stringcmp(string s1, string s2)
{
    if (s1->len != s2->len)
        return false;

    return strncmp(s1->c_str, s2->c_str, s1->len) == 0;
}
