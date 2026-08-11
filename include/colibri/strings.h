
#ifndef COLIBRI_RUNTIME_STRINGS_H
#define COLIBRI_RUNTIME_STRINGS_H

// TODO: we need this only in environment.c, but perhaps build out a string library around that
//       type of strings instead. Tough call.
typedef struct
{
    char *data;
    size_t size;
} string_t;

char* trim(char* s);

#endif //COLIBRI_RUNTIME_STRINGS_H
