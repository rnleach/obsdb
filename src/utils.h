#pragma once
/** \file utils.h
 *
 * \brief Common internal utilities.
 */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*-------------------------------------------------------------------------------------------------
 *                                        Error handling.
 *-----------------------------------------------------------------------------------------------*/
/** Clean error handling. */
#define StopIf(assertion, error_action, ...)                                                       \
    {                                                                                              \
        if (assertion) {                                                                           \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            {                                                                                      \
                error_action;                                                                      \
            }                                                                                      \
        }                                                                                          \
    }

/*-------------------------------------------------------------------------------------------------
 *                                    String Utilities
 *-----------------------------------------------------------------------------------------------*/
/** Lowercase a string. */
inline void
obs_util_str_to_lower(char *str)
{
    char *c = str;
    while (*c) {
        *c = tolower(*c);
        ++c;
    }
}

/** Copy a string into a new buffer and lowercase it. */
inline void
obs_util_strcpy_to_lowercase(size_t buf_size, char buf[buf_size], char const *const src)
{
    assert(src && strlen(src) + 1 < buf_size);
    strcpy(buf, src);
    obs_util_str_to_lower(buf);
}
