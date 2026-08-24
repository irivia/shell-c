#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define FREE(p)                   \
    do {                          \
        /*printf("free %p\n", (p));*/ \
        /*fflush(stdout);          */ \
        free(p);                  \
    } while (0)

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
        FREE((da).items);  \
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

// bool func(void*, void*)
#define da_remove_item(da, elem, func)            \
    do {                                          \
        for (size_t i = 0; i < (da).count; i++) { \
            if (func(&(elem), &(da).items[i])) {  \
                da_remove(da, i);                 \
                break;                            \
            }                                     \
        }                                         \
    } while (0)
