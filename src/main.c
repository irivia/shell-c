#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define da_push(da, data) \
    do { \
        (da).capacity = (da).capacity > 0 ? (da).capacity * 2 : 32;        \
        (da).items = realloc(da.items, da.capacity * sizeof(*(da).items)); \
        (da).items[(da).count] = (data);                                   \
        (da).count += 1;                                                   \
    } while (0)

typedef struct {
    char *data;
    size_t len;
} String;

typedef struct {
    String *items;
    size_t count;
    size_t capacity;
} StrList;

bool is_space(char c)
{
    return (
        c == ' '  ||
        c == '\n' ||
        c == '\t' ||
        c == '\r'
    );
}

String trim_left(const String s)
{
    String str = s;
    for (size_t i = 0; i < s.len && is_space(s.data[i]); i++) {
        str.data++;
        str.len--;
    }
    return str;
}

String next_word(String *s)
{
    String str = { .data = s->data };
    for (size_t i = 0; i < s->len && !is_space(s->data[i]); i++) {
        str.len++;
    }
    s->data += str.len;
    s->len -= str.len;

    return str;
}

StrList extract_words(const char *s)
{
    StrList words = {0};
    String str = { .data = (char*)s, .len = strlen(s) };
    str = trim_left(str);

    while (str.len > 0) {
        da_push(words, next_word(&str));
        str = trim_left(str);
    }

    return words;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    enum { BUFFER_SZ = 2048 };
    char BUFFER[BUFFER_SZ];
    enum commands {
        CMD_EXIT,
        CMD_ECHO,
        CMD_COUNT,
    };
    String commands[CMD_COUNT] = {
        { "exit", 4 },
        { "echo", 4 },
    };

    while (true) {
        printf("$ ");
        if (fgets(BUFFER, BUFFER_SZ, stdin) != NULL) {
            StrList words = extract_words(BUFFER);
            if (words.count == 0) continue;
            int matched = -1;
            for (size_t i = 0; i < CMD_COUNT; i++) {
                if (words.items[0].len == commands[i].len && memcmp(words.items[0].data, commands[i].data, commands[i].len) == 0) {
                    matched = i;
                }
            }
            if (matched == -1) {
                printf("%.*s: command not found\n", (int)words.items[0].len, words.items[0].data);
                fflush(stdout);
                continue;
            }
            switch (matched) {
            case CMD_EXIT:
                exit(0);
            case CMD_ECHO:
                for (size_t i = 1; i < words.count; i++) {
                    printf("%.*s", (int)words.items[i].len, words.items[i].data);
                    if (i < words.count - 1)
                        printf(" ");
                }
                printf("\n");
                fflush(stdout);
                break;
            default:
                break; // UNREACHABLE
            }
        }
    }


    return 0;
}
