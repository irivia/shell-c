#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define da_push(da, data) \
    do { \
        (da).capacity = (da).capacity > 0 ? (da).capacity * 2 : 32;        \
        (da).items = realloc(da.items, da.capacity * sizeof(*(da).items)); \
        (da).items[(da).count] = (data);                                   \
        (da).count += 1;                                                   \
    } while (0)

#define da_free(da)        \
    do {                   \
        free((da).items);  \
        (da).count = 0;    \
        (da).capacity = 0; \
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

String trim_left_by_delim(const String s, char delim)
{
    String str = s;
    for (size_t i = 0; i < s.len && s.data[i] == delim; i++) {
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

String chop_by_delim(String *s, char delim)
{
    String str = { .data = s->data };
    for (size_t i = 0; i < s->len && s->data[i] != delim; i++) {
        str.len++;
    }
    s->data += str.len;
    s->len -= str.len;
    if (s->len >= 1) {
        s->data[0] = '\0';
        s->data += 1;
        s->len -= 1;
    }

    return str;
}

StrList split_by_delim(const char *s, char delim)
{
    StrList words = {0};
    String str = { .data = (char*)s, .len = strlen(s) };
    str = trim_left_by_delim(str, delim);

    while (str.len > 0) {
        da_push(words, chop_by_delim(&str, delim));
    }

    return words;
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

typedef enum {
    CMD_EXIT,
    CMD_ECHO,
    CMD_TYPE,
    CMD_COUNT,
} Commands;

static String commands[CMD_COUNT] = {
    { "exit", 4 },
    { "echo", 4 },
    { "type", 4 },
};

const char* get_file_name(const char *path)
{
    int64_t len = strlen(path);
    for (int64_t i = len - 1; i >= 0; i--) {
        if (path[i] == '/' && i + 1 < len) {
            return &path[i+1];
        }
    }
    return NULL;
}

const char* search_path(String cmd)
{
    char* path = getenv("PATH");
    if (path == NULL) return NULL;
    StrList dirs = split_by_delim(path, ':');
    DIR *dir;
    struct dirent *ent;
    for (size_t i = 0; i < dirs.count; i++) {
        if ((dir = opendir(dirs.items[i].data)) == NULL)
            continue;
        while ((ent = readdir(dir)) != NULL) {
            if (strlen(ent->d_name) != cmd.len)
                continue;
            char temp[512] = {0};
            size_t wrote = snprintf(temp, 512, "%s/%s", dirs.items[i].data, ent->d_name);
            if (wrote == 0)
                continue;
            char *real_path = (char*)malloc(wrote + 1);
            memcpy(real_path, temp, wrote);
            real_path[wrote] = '\0';
            struct stat sb;
            struct stat path_stat;
            stat(real_path, &path_stat);
            bool is_file = S_ISREG(path_stat.st_mode);
            if (is_file && stat(real_path, &sb) == 0 && sb.st_mode & S_IXUSR) { // File has executable permission
                if (memcmp(ent->d_name, cmd.data, cmd.len) == 0) {
                    closedir(dir);
                    return real_path;
                }
            }
        }
        closedir(dir);
    }

    return NULL;
}

void command_echo(StrList words)
{
    for (size_t i = 1; i < words.count; i++) {
        printf("%.*s", (int)words.items[i].len, words.items[i].data);
        if (i < words.count - 1)
            printf(" ");
    }
    printf("\n");
    fflush(stdout);
}

void command_type(StrList words)
{
    if (words.count < 1) {
        printf("No command was provided.");
        fflush(stdout);
        return;
    }
    int matched = -1;
    const char *buf;
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (words.items[1].len == commands[i].len && memcmp(words.items[1].data, commands[i].data, commands[i].len) == 0) {
            matched = i;
        }
    }
    if (matched != -1) {
        printf("%.*s is a shell builtin\n", (int)words.items[1].len, words.items[1].data);
    }
    else if ((buf = search_path(words.items[1])) != NULL) {

        printf("%.*s is %s\n", (int)words.items[1].len, words.items[1].data, buf);
    }
    else {
        printf("%.*s: not found\n", (int)words.items[1].len, words.items[1].data);
    }
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    enum { BUFFER_SZ = 2048 };
    char BUFFER[BUFFER_SZ];

    #define MATCH_CMDS(item)                                                                                                    \
    do {                                                                                                                        \
        for (size_t i = 0; i < CMD_COUNT; i++) {                                                                                \
            if (item.len == commands[i].len && memcmp(item.data, commands[i].data, commands[i].len) == 0) {                     \
                matched = i;                                                                                                    \
            }                                                                                                                   \
        }                                                                                                                       \
    } while (0)

    while (true) {
        printf("$ ");
        if (fgets(BUFFER, BUFFER_SZ, stdin) == NULL)
            continue;
        StrList words = extract_words(BUFFER);
        if (words.count == 0) continue;
        int matched = -1;
        MATCH_CMDS(words.items[0]);
        switch (matched) {
        case CMD_EXIT:
            exit(0);
        case CMD_ECHO:
                command_echo(words);
            break;
        case CMD_TYPE:
                command_type(words);
            break;
        default:
            printf("%.*s: command not found\n", (int)words.items[0].len, words.items[0].data);
            fflush(stdout);
            break;
        }
    }


    return 0;
}
