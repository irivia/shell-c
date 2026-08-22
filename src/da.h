#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define da_push(da, data)                                                          \
    do {                                                                           \
        if ((da).count >= (da).capacity) {                                         \
            (da).capacity = (da).capacity > 0 ? (da).capacity * 1.5 : 32;          \
            (da).items = realloc((da).items, (da).capacity * sizeof(*(da).items)); \
        }                                                                          \
        (da).items[(da).count] = (data);                                           \
        (da).count += 1;                                                           \
    } while (0)

#define da_free(da)        \
    do {                   \
        free((da).items);  \
        (da).items = NULL; \
        (da).count = 0;    \
        (da).capacity = 0; \
    } while (0)

#define da_clear(da)    \
    do {                \
        (da).count = 0; \
    } while (0)

#define da_foreach(da, iter) for (typeof((da).items) iter = (da).items; iter != &(da).items[(da).count]; iter++)

#define da_remove(da, i)                            \
    do {                                            \
        if ((i) >= (da).count) break;               \
        (da).items[i] = (da).items[(da).count - 1]; \
        (da).count -= 1;                            \
    } while (0)

