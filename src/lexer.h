#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "da.h"

typedef enum {
    TOK_WORD,
    TOK_WRITE_OUT,
    TOK_APPEN_OUT,
    TOK_WRITE_ERR,
    TOK_APPEN_ERR,
    TOK_JOB,
    TOK_PIPE,
} TokenType;

typedef struct {
    char *data;
    size_t len;
    TokenType type;
} Token;

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} StringBuilder;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

#define TOK_FMT(tok) (int)(tok).len, (tok).data
#define TOK_NULL (Token){0}

static bool is_space(char c)
{
    return (
        c == ' '  ||
        c == '\n' ||
        c == '\t' ||
        c == '\r'
    );
}

static void tok_free(Token *t)
{
    if (!t) return;
    FREE(t->data);
    *t = TOK_NULL;
}

static void toklist_free(TokenList *tl)
{
    if (!tl) return;
    da_foreach(*tl, tok) {
        tok_free(tok);
    }
    da_free(*tl);
}

static Token tok_dup(Token t)
{
    if (t.len == 0 || !t.data) return TOK_NULL;

    return (Token) {
        .data = strndup(t.data, t.len),
        .len = t.len,
        .type = t.type
    };
}

static Token sb_to_tok(StringBuilder *sb)
{
    if (!sb || sb->count == 0) return TOK_NULL;

    da_push(*sb, '\0');
    return (Token) {
        .data = sb->items,
        .len = sb->count - 1,
        .type = TOK_WORD
    };
}

static Token cstr_to_tok(const char *s)
{
    if (!s) return TOK_NULL;

    return (Token) {
        .data = strdup(s),
        .len = strlen(s),
        .type = TOK_WORD
    };
}

char** toklist_to_cstrlist(TokenList tl, size_t n)
{
    if (n > tl.count)
        n = tl.count;

    char **list = (char**)malloc(sizeof(*list) * (n + 1));
    if (!list) {
        fprintf(stderr, "Failed to allocated list of size: %zu\n", n);
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        list[i] = strndup(tl.items[i].data, tl.items[i].len);
    }
    list[n] = NULL;

    return list;
}

void free_cstrlist(char* **list)
{
    if (!list || !(*list)) return;
    for (char **ptr = *list; *ptr != NULL; ptr++) {
        FREE(*ptr);
    }
    FREE(*list);
    *list = NULL;
}

static Token to_tok_fmt(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    char *p;
    int printed = vasprintf(&p, fmt, va);
    va_end(va);

    if (printed < 0)
        return TOK_NULL;

    return (Token) {
        .data = p,
        .len = (size_t)printed,
        .type = TOK_WORD
    };
}

static bool tok_equ(Token x, const char *s)
{
    return s && strlen(s) == x.len && memcmp(s, x.data, x.len) == 0;
}

static bool tok_cmp(Token x, Token y)
{
    return x.len == y.len && memcmp(x.data, y.data, x.len) == 0;
}

static void tok_inc(Token *s)
{
    if (!s || s->len == 0) return;

    s->data++;
    s->len--;
}

static void tokn_inc(Token *s, size_t n)
{
    if (!s || s->len == 0) return;

    for (size_t i = 0; i < n && s->len > 0; i++) {
        s->data++;
        s->len--;
    }
}

static TokenList split_by_delim(char *s, char delim)
{
    if (s == NULL) return (TokenList){0};

    TokenList words = {0};
    char *cur = s;

    for (;; s++) {
        if (*s == delim || *s == '\0') {
            if (s > cur) {
                Token word = to_tok_fmt("%.*s", (int)(s - cur), cur);
                da_push(words, word);
            }
            if (*s == '\0')
                break;
            cur = s + 1;
        }
    }

    return words;
}

static void trim_left(Token *s)
{
    if (s == NULL) return;

    for (; s->len > 0 && is_space(*s->data); tok_inc(s));
}

static void trim_right(Token *s)
{
    if (s == NULL) return;

    for (size_t i = s->len - 1; s->len > 0 &&
        is_space(s->data[i]); i--) s->len--;
}

static bool expected(Token *s, char c)
{
    return s->len > 1 && s->data[1] == c;
}

static bool expected_tok(Token *s, const char *exp)
{
    if (exp == NULL || s->len == 0) return false;

    size_t exp_len = strlen(exp);
    size_t i = 0;

    for (; i < exp_len && i < s->len; i++) {
        if (s->data[i] != exp[i]) return false;
    }

    if (exp_len == i) {
        s->data = &s->data[i];
        s->len -= i;
        return true;
    }

    return false;
}

static Token next_tok(TokenList *list)
{
    if (list->count == 0) return TOK_NULL;

    list->count--;
    return *(list->items++);
}

static bool toklist_contains(TokenList sl, Token s)
{
    da_foreach(sl, i) {
        if (tok_cmp(*i, s)) return true;
    }

    return false;
}


static int qsort_tok_fun(const void *x, const void *y)
{
    if (!x || !y) return 0;

    Token tok1 = *(Token*)x;
    Token tok2 = *(Token*)y;

    if (tok1.len == 0 || tok2.len == 0) return 0;

    return tok1.data[0] - tok2.data[0];
}

static bool contains_char(char c, char *chars, size_t n)
{
    if (!chars) return false;

    for (size_t i = 0; i < n; i++) {
        if (c == chars[i]) return true;
    }

    return false;
}

static Token chop_string(Token *s);

static Token chop_word(Token *s)
{
    if (s == NULL || s->len == 0)
        return TOK_NULL;

    StringBuilder sb = {0};
    char stoppers[] = {
        ' ',
        '\t',
        '\r',
        '\n',
        '>',
        '&',
        '|'
    };

    for (; s->len > 0 && !contains_char(*s->data, stoppers, sizeof(stoppers)); tok_inc(s)) {
        if (*s->data == '\\') {
            tok_inc(s);
            if (s->len > 0)
                da_push(sb, *s->data);
        }
        else if (*s->data == '\'' || *s->data == '"') {
            if (expected(s, *s->data))
                tok_inc(s);
            else {
                Token string = chop_string(s);
                for (size_t i = 0; i < string.len; i++)
                    da_push(sb, string.data[i]);
                tok_free(&string);
                if (contains_char(*s->data, stoppers, sizeof(stoppers)))
                    break;
            }
        }
        else {
            da_push(sb, *s->data);
        }
    }

    return sb_to_tok(&sb);
}

static Token chop_string(Token *s)
{
    if (s == NULL || s->len == 0 || (*s->data != '\'' && *s->data != '"'))
        return TOK_NULL;

    StringBuilder sb = {0};
    char quote = *s->data;
    tok_inc(s);

    for (; s->len > 0; tok_inc(s)) {
        if (*s->data == quote) {
            if (expected(s, quote)) {
                tok_inc(s);
            }
            else if (s->len > 1 && is_space(s->data[1])) {
                tok_inc(s);
                break;
            }
        }
        else if (*s->data == '\\' && quote == '"') {
            tok_inc(s);
            if (s->len > 0)
                da_push(sb, *s->data);
        }
        else {
            da_push(sb, *s->data);
        }
    }

    return sb_to_tok(&sb);
}

TokenList extract_words(const char *str)
{
    if (str == NULL)
        return (TokenList){0};

    TokenList tokens = {0};

    Token s = { .data = (char*)str, .len = strlen(str), .type = TOK_WORD };
    trim_right(&s);

    while (s.len > 0) {
        trim_left(&s);
        char c = *s.data;
        if (c == '\'' || c == '"') {
            Token word = chop_string(&s);
            if (word.len == 0) continue;
            da_push(tokens, word);
        }
        else if (expected_tok(&s, "1>>")) {
            Token word = cstr_to_tok("1>>");
            word.type = TOK_APPEN_OUT;
            da_push(tokens, word);
        }
        else if (expected_tok(&s, "2>>")) {
            Token word = cstr_to_tok("2>>");
            word.type = TOK_APPEN_ERR;
            da_push(tokens, word);
        }
        else if (expected_tok(&s, "1>")) {
            Token word = cstr_to_tok("1>");
            word.type = TOK_WRITE_OUT;
            da_push(tokens, word);
        }
        else if (expected_tok(&s, "2>")) {
            Token word = cstr_to_tok("2>");
            word.type = TOK_WRITE_ERR;
            da_push(tokens, word);
        }
        else if (expected_tok(&s, ">>")) {
            Token word = cstr_to_tok(">>");
            word.type = TOK_APPEN_OUT;
            da_push(tokens, word);
        }
        else if (c == '>') {
            tok_inc(&s);
            Token word = cstr_to_tok(">");
            word.type = TOK_WRITE_OUT;
            da_push(tokens, word);
        }
        else if (c == '&') {
            tok_inc(&s);
            Token word = cstr_to_tok("&");
            word.type = TOK_JOB;
            da_push(tokens, word);
        }
        else if (c == '|') {
            tok_inc(&s);
            Token word = cstr_to_tok("|");
            word.type = TOK_PIPE;
            da_push(tokens, word);
        }
        else {
            Token word = chop_word(&s);
            if (word.len == 0) continue;
            da_push(tokens, word);
        }
    }

    return tokens;
}

