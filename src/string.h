#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "da.h"

typedef struct {
    char *data;
    size_t len;
} String;

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} StringBuilder;

typedef struct {
    String *items;
    size_t count;
    size_t capacity;
} StrList;

#define STR_FMT(str) (int)(str).len, (str).data
#define STR_NULL (String){0}

static bool is_space(char c)
{
    return (
        c == ' '  ||
        c == '\n' ||
        c == '\t' ||
        c == '\r'
    );
}

static String sb_to_str(StringBuilder *sb)
{
    if (!sb || sb->count == 0) return STR_NULL;

    da_push(*sb, '\0');
    return (String) {
        .data = sb->items,
        .len = sb->count - 1
    };
}

static String to_str(const char *s)
{
    if (!s) return STR_NULL;

    return (String) {
        .data = (char*)s,
        .len = strlen(s),
    };
}

static String to_str_fmt(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    char *p;
    int printed = vasprintf(&p, fmt, va);
    va_end(va);

    if (printed < 0)
        return STR_NULL;

    return (String) {
        .data = p,
        .len = (size_t)printed,
    };
}

static bool str_equ(String x, const char *s)
{
    return s && strlen(s) == x.len && memcmp(s, x.data, x.len) == 0;
}

static bool str_cmp(String x, String y)
{
    return x.len == y.len && memcmp(x.data, y.data, x.len) == 0;
}

static void str_inc(String *s)
{
    if (!s || s->len == 0) return;

    s->data++;
    s->len--;
}

static void strn_inc(String *s, size_t n)
{
    if (!s || s->len == 0) return;

    for (size_t i = 0; i < n && s->len > 0; i++) {
        s->data++;
        s->len--;
    }
}

static StrList split_by_delim(char *s, char delim)
{
    if (s == NULL) return (StrList){0};

    StrList words = {0};
    char *cur = s;

    for (;; s++) {
        if (*s == delim || *s == '\0') {
            if (s != cur) {
                String word = { .data = cur, .len = s - cur};
                da_push(words, word);
            }
            if (*s == '\0')
                break;
            cur = s + 1;
        }
    }

    return words;
}

static void trim_left(String *s)
{
    if (s == NULL) return;

    for (; s->len > 0 && is_space(*s->data); str_inc(s));
}

static void trim_right(String *s)
{
    if (s == NULL) return;

    for (size_t i = s->len - 1; s->len > 0 &&
        is_space(s->data[i]); i--) s->len--;
}

static bool expected(String *s, char c)
{
    return s->len > 1 && s->data[1] == c;
}

static bool expected_str(String *s, const char *exp)
{
    if (exp == NULL || s->len == 0) return false;

    size_t exp_len = strlen(exp);
    size_t i = 0;

    for (; i < exp_len && i < s->len; i++) {
        if (s->data[i] != exp[i]) return false;
    }

    return exp_len == i;
}

static String next_str(StrList *list)
{
    if (list->count == 0) return STR_NULL;

    list->count--;
    return *(list->items++);
}
