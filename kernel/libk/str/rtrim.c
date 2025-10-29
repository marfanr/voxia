#include <libk/str.h>

// warn: modifies the input string
char *
rtrim(char *str)
{
    int i = strlen(str) - 1;
    while (i >= 0 && str[i] == ' ')
    {
        str[i] = 0;
        i--;
    }
    return str;
}
