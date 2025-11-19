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

string
str_concat(string s, const char *suffix)
{
    size_t suffix_len = strlen(suffix);
    size_t new_len    = s->len + suffix_len;

    string s_new = (string)kalloc(sizeof(*s_new) + new_len + 1);
    s_new->c_str = (char *)((uint8_t *)s_new + sizeof(*s_new));
    s_new->len   = new_len;
    s_new->cap   = new_len + 1;

    strcpy(s_new->c_str, s->c_str);
    strcpy(s_new->c_str + s->len, suffix);

    return s_new;
}

void
str_trim(string str)
{
    for (size_t i = str->len - 1; i >= 0; i--)
    {
        if (str->c_str[i] == ' ' || str->c_str[i] == '\n' || str->c_str[i] == '\t' ||
            str->c_str[i] == '\r')
        {
            str->c_str[i] = '\0';
            str->len -= 1;
        }
        else
        {
            break;
        }
    }
}