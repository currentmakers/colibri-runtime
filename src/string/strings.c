#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "colibri/strings.h"


/*
 * Strip leading/trailing whitespace (in-place). Returns pointer to the
 * first non-whitespace character. The original buffer is mutated to
 * terminate the string after the last non-whitespace character.
 */
char* trim(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0)
    {
        char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
            break;
        }
        s[--len] = '\0';
    }
    return s;
}

