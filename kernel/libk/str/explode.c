#include "libk/serial.h"
#include "libk/string.h"
#include "libk/vector.h"
#include <libk/str.h>
#include <memory/kalloc.h>

static const char *
strsep(char **str, const char delim)
{
    if (*str == 0 || **str == '\0')
    {
        return 0;
    }
    char *start = *str;
    char *end   = start;

    while (*end && *end != delim)
    {
        end++;
    }

    if (*end)
    {
        *end = 0;
        *str = end + 1;
    }
    else
    {
        *str = 0;
    }

    return start;
}

void
explode(const char *path, const char delim, vector(string) * out)
{
    size_t len = strlen(path);
    // serial_trace("exploding path len %d \n", len);

    char *buf = (char *)kalloc(len + 1);
    strcpy(buf, path);
    buf[len] = 0;

    char       *rest = buf;
    const char *token;

    while ((token = strsep(&rest, delim)) != 0)
    {
        // serial_trace("%s \n", rest);
        if (strlen(token) > 0)
        {
            string tmp = str(token);
            vector_push_back(out, str(token));
        }
    }
    kfree(buf, len + 1);
}