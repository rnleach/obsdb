#pragma once
/** \file utils.h
 *
 * \brief Common internal utilities.
 */


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

