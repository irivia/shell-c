#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>

#define da_push(da, data)                                                  \
    do {                                                                   \
        (da).capacity = (da).capacity > 0 ? (da).capacity * 2 : 32;        \
        (da).items = realloc(da.items, da.capacity * sizeof(*(da).items)); \
        (da).items[(da).count] = (data);                                   \
        (da).count += 1;                                                   \
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

typedef enum {
    STR_WORD,
    STR_STR,
} StrType; // Too lazy to rename everything to 'Token'

typedef struct {
    char *data;
    size_t len;
    StrType type;
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

bool is_alnum(char c)
{
    return (
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 0 && c <= 9)
    );
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

StrList split_by_delim(char *s, char delim)
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

void str_inc(String *s)
{
    if (s == NULL || s->len == 0) return;

    s->data++;
    s->len--;
}

void trim_left(String *s)
{
    if (s == NULL || s->len == 0) return;

    for (; s->len > 0 && is_space(*s->data); str_inc(s));
}

String chop_string(String *s)
{
    if (s == NULL || s->len == 0 || s->data[0] != '\'')
        return (String){0};

    str_inc(s);
    String word = { .data = s->data };

    for (; s->len > 0 && *s->data != '\''; str_inc(s))
        word.len++;

    if (*s->data == '\'')
        str_inc(s);

    return word;
}

String chop_word(String *s)
{
    if (s == NULL || s->len == 0)
        return (String){0};

    String word = { .data = s->data };

    for (; s->len > 0 && is_alnum(*s->data); str_inc(s))
        word.len++;

    return word;
}

StrList extract_words(char *str)
{
    if (str == NULL)
        return (StrList){0};

    StrList words = {0};

    String s = { .data = str, .len = strlen(str) };
    trim_left(&s);

    while (s.len > 0) {
        char c = *s.data;
        if (is_alnum(c)){
            String word = chop_word(&s);
            if (word.len == 0) continue;

            char *data = (char*)malloc(word.len + 1);
            memcpy(data, word.data, word.len);
            data[word.len] = '\0';
            word.data = data;
            word.type = STR_WORD;
            da_push(words, word);
            trim_left(&s);
        }
        else if (c == '\'') {
            String word = chop_string(&s);
            if (word.len == 0) continue;

            char *data = (char*)malloc(word.len + 1);
            memcpy(data, word.data, word.len);
            data[word.len] = '\0';
            word.data = data;
            word.type = STR_STR;
            da_push(words, word);
            trim_left(&s);
        }
        else {
            str_inc(&s);
        }
    }

    return words;
}

typedef enum {
    CMD_EXIT,
    CMD_ECHO,
    CMD_TYPE,
    CMD_PWD,
    CMD_CD,
    CMD_COUNT,
} Commands;

static String commands[CMD_COUNT] = {
    { "exit", 4 },
    { "echo", 4 },
    { "type", 4 },
    { "pwd", 3 },
    { "cd", 2 },
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

bool is_file_executable(const char *file)
{
    struct stat path_stat;

    return (
        file != NULL && stat(file, &path_stat) == 0 &&
        S_ISREG(path_stat.st_mode) && access(file, X_OK) == 0
    );
}

char* search_path(const StrList path_dirs, const String cmd)
{
    if (path_dirs.count == 0 || cmd.data == NULL || cmd.len == 0)
        return NULL;
    DIR *dir;
    struct dirent *ent;
    char temp_buf[PATH_MAX];
    for (size_t i = 0; i < path_dirs.count; i++) {
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", (int)path_dirs.items[i].len, path_dirs.items[i].data);
        if ((dir = opendir(temp_buf)) == NULL)
            continue;
        while ((ent = readdir(dir)) != NULL) {
            size_t ent_len = strlen(ent->d_name);
            if (ent_len != cmd.len)
                continue;
            size_t real_path_sz = path_dirs.items[i].len + ent_len + 2; // one for '/' and one for null terminator
            char *real_path = (char*)malloc(real_path_sz);
            if (real_path == NULL)
                continue; 
            if (snprintf(real_path, real_path_sz, "%.*s/%s", (int)path_dirs.items[i].len, path_dirs.items[i].data, ent->d_name) != real_path_sz - 1) {
                free(real_path);
                continue;
            }
            if (is_file_executable(real_path)) {
                if (memcmp(ent->d_name, cmd.data, cmd.len) == 0) {
                    closedir(dir);
                    return real_path;
                }
            }
            free(real_path);
        }
        closedir(dir);
    }

    return NULL;
}

void command_echo(StrList words)
{
    for (size_t i = 1; i < words.count; i++) {
        printf("%.*s", (int)words.items[i].len, words.items[i].data);
        if (i < words.count - 1 && words.items[i].type == STR_WORD)
            printf(" ");
    }
    printf("\n");
    fflush(stdout);
}

void command_type(StrList path_dirs, StrList words)
{
    if (words.count < 2) {
        printf("No command was provided.\n");
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
    else if ((buf = search_path(path_dirs, words.items[1])) != NULL) {
        printf("%.*s is %s\n", (int)words.items[1].len, words.items[1].data, buf);
    }
    else {
        printf("%.*s: not found\n", (int)words.items[1].len, words.items[1].data);
    }
    fflush(stdout);
}

void command_pwd()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        fflush(stdout);
    }
}

void command_cd(String path)
{
    if (path.len == 0 || (path.len == 1 && path.data[0] == '~')) {
        char *homedir = getenv("HOME");
        if (homedir == NULL) {
            struct passwd *pw = getpwuid(getuid());
            homedir = pw->pw_dir;
        }
        chdir(homedir);
    }
    else if (chdir(path.data) != 0) {
        printf("cd: %s: No such file or directory\n", path.data);
        fflush(stdout);
    }
}

void execute_program(const char *path, StrList args)
{
    if (path == NULL || args.count == 0)
        return;
    char* *arguments = (char**)malloc((args.count + 1) * sizeof(*arguments));
    for (size_t i = 0; i < args.count; i++)
        arguments[i] = args.items[i].data;
    arguments[args.count] = NULL;
    int pid = fork();
    if (pid == 0) {
        execv(path, arguments);
    }
    else {
        wait(NULL);
        free(arguments);
    }
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    enum { BUFFER_SZ = 2048 };
    char BUFFER[BUFFER_SZ];
    char* path = getenv("PATH");
    StrList path_dirs = {0};
    if (path != NULL) {
        path_dirs = split_by_delim(path, ':');
    }

    while (true) {
        printf("$ ");
        if (fgets(BUFFER, BUFFER_SZ, stdin) == NULL)
            continue;
        StrList words = extract_words(BUFFER);
        if (words.count == 0) continue;
        int matched = -1;
        for (size_t i = 0; i < CMD_COUNT; i++) {
            if (words.items[0].len == commands[i].len &&
                memcmp(words.items[0].data, commands[i].data, commands[i].len) == 0) {
                matched = i;
            }
        }
        char *program = NULL;
        switch (matched) {
        case CMD_EXIT:
            exit(0);
        case CMD_ECHO:
            command_echo(words);
            break;
        case CMD_TYPE:
            command_type(path_dirs, words);
            break;
        case CMD_PWD:
            command_pwd();
            break;
        case CMD_CD:
            if (words.count > 1)
                command_cd(words.items[1]);
            else
                command_cd((String){0});
            break;
        default:
            if ((program = search_path(path_dirs, words.items[0])) != NULL) {
                execute_program(program, words);
            }
            else {
                printf("%.*s: command not found\n", (int)words.items[0].len, words.items[0].data);
                fflush(stdout);
            }
            break;
        }
        da_free(words);
    }


    return 0;
}
