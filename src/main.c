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
#include <stdarg.h>
#include <pwd.h>
#include <readline/readline.h>

#define da_push(da, data)                                                      \
    do {                                                                       \
        if ((da).count >= (da).capacity) {                                     \
            (da).capacity = (da).capacity > 0 ? (da).capacity * 1.5 : 32;      \
            (da).items = realloc(da.items, da.capacity * sizeof(*(da).items)); \
        }                                                                      \
        (da).items[(da).count] = (data);                                       \
        (da).count += 1;                                                       \
    } while (0)

#define da_reserve(da, sz)                                                     \
    do {                                                                       \
        (da).capacity = (sz);                                                  \
        (da).items = realloc((da).items, (da).capacity * sizeof(*(da).items)); \
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
        (c >= '0' && c <= '9')
    );
}

bool is_num(char c)
{
    return c >= '0' && c <= '9';
}

String to_str(const char *s)
{
    if (s == NULL) return (String){0};

    return (String) {
        .data = (char*)s,
        .len = strlen(s)
    };
}

String to_str_fmt(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    char *p;
    int printed = vasprintf(&p, fmt, va);
    va_end(va);

    if (printed <= 0)
        return (String){0};

    return (String) {
        .data = p,
        .len = printed
    };
}

bool str_equ(String x, const char *s)
{
    return s != NULL && strlen(s) == x.len && memcmp(s, x.data, x.len) == 0;
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

void str_inc_n(String *s, size_t n)
{
    if (s == NULL || s->len == 0) return;

    for (size_t i = 0; i < n && s->len > 0; i++) {
        s->data++;
        s->len--;
    }
}

void trim_left(String *s)
{
    if (s == NULL) return;

    for (; s->len > 0 && is_space(*s->data); str_inc(s));
}

void trim_right(String *s)
{
    if (s == NULL) return;

    for (size_t i = s->len - 1; s->len > 0 &&
        is_space(s->data[i]); i--) s->len--;
}

bool expected(String *s, char c)
{
    return s->len > 1 && s->data[1] == c;
}

bool expected_off(String *s, char c, size_t offset)
{
    return s->len > offset && s->data[offset] == c;
}

bool expected_str(String *s, const char *exp)
{
    if (exp == NULL || s->len == 0) return false;

    size_t exp_len = strlen(exp);
    size_t i = 0;

    for (; i < exp_len && i < s->len; i++) {
        if (s->data[i] != exp[i]) return false;
    }

    return exp_len == i;
}

String chop_string(String *s);

String chop_word(String *s)
{
    if (s == NULL || s->len == 0)
        return (String){0};

    StringBuilder str = {0};

    for (; s->len > 0 && !is_space(*s->data); str_inc(s)) {
        if (*s->data == '\\') {
            str_inc(s);
            if (s->len > 0)
                da_push(str, *s->data);
        }
        else if (*s->data == '\'' || *s->data == '"') {
            char quote = *s->data;
            if (expected(s, quote))
                str_inc(s);
            else {
                String string = chop_string(s);
                for (size_t i = 0; i < string.len; i++)
                    da_push(str, string.data[i]);
                free(string.data);
                if (is_space(*s->data)) break;
            }
        }
        else if (*s->data == '>') {
            break;
        }
        else {
            da_push(str, *s->data);
        }
    }

    if (str.count > 0) da_push(str, '\0');

    return (String){ .data = str.items, .len = str.count > 0 ? str.count - 1 : 0 };
}

String chop_string(String *s)
{
    if (s == NULL || s->len == 0 || (*s->data != '\'' && *s->data != '"'))
        return (String){0};

    StringBuilder str = {0};
    char quote = *s->data;
    str_inc(s);

    for (; s->len > 0; str_inc(s)) {
        if (*s->data == quote) {
            if (expected(s, quote)) {
                str_inc(s);
            }
            else if (s->len > 1 && is_space(s->data[1])) {
                str_inc(s);
                break;
            }
        }
        else if (*s->data == '\\' && quote == '"') {
            str_inc(s);
            if (s->len > 0)
                da_push(str, *s->data);
        }
        else {
            da_push(str, *s->data);
        }
    }
    if (str.count > 0) da_push(str, '\0');

    return (String){ .data = str.items, .len = str.count > 0 ? str.count - 1 : 0 };
}

StrList extract_words(char *str)
{
    if (str == NULL)
        return (StrList){0};

    StrList words = {0};

    String s = { .data = str, .len = strlen(str) };
    trim_right(&s);

    while (s.len > 0) {
        trim_left(&s);
        char c = *s.data;
        if (c == '\'' || c == '"') {
            String word = chop_string(&s);
            if (word.len == 0) continue;
            da_push(words, word);
        }
        else if (expected_str(&s, "1>>") || expected_str(&s, "2>>")) {
            str_inc_n(&s, 3);
            da_push(words, to_str_fmt("%c>>", c));
        }
        else if (expected_str(&s, "1>") || expected_str(&s, "2>")) {
            str_inc_n(&s, 2);
            da_push(words, to_str_fmt("%c>", c));
        }
        else if (expected_str(&s, ">>")) {
            str_inc_n(&s, 2);
            da_push(words, to_str(">>"));
        }
        else if (c == '>') {
            str_inc(&s);
            da_push(words, to_str(">"));
        }
        else {
            String word = chop_word(&s);
            if (word.len == 0) continue;
            da_push(words, word);
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

StrList get_execs_from_path(StrList path_dirs)
{
    StrList execs = {0};
    if (path_dirs.count == 0)
        return execs;
    DIR *dir;
    struct dirent *ent;
    char temp_buf[PATH_MAX];
    for (size_t i = 0; i < path_dirs.count; i++) {
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", (int)path_dirs.items[i].len, path_dirs.items[i].data);
        if ((dir = opendir(temp_buf)) == NULL)
            continue;
        while ((ent = readdir(dir)) != NULL) {
            const size_t ent_len = strlen(ent->d_name);
            const size_t real_path_sz = path_dirs.items[i].len + ent_len + 2; // one for '/' and one for null terminator
            if (snprintf(temp_buf, sizeof(temp_buf), "%.*s/%s", (int)path_dirs.items[i].len, path_dirs.items[i].data, ent->d_name) != real_path_sz - 1) {
                continue;
            }
            if (is_file_executable(temp_buf)) {
                String s;
                s.data = strndup(ent->d_name, ent_len);
                s.len = ent_len;
                da_push(execs, s);
            }
        }
        closedir(dir);
    }

    return execs;
}

int redirect_to(int fd, const char *filename, const char *mode)
{
    if (filename == NULL || fd < 0) return -1;

    FILE *f;
    int filed;
    f = fopen(filename, mode);
    if (f == NULL) return -1;

    filed = fileno(f);
    int newfd;
    if (filed == -1 || (newfd = dup2(filed, fd)) == -1) {
        fclose(f);
        return -1;
    }
    close(filed);

    return newfd;
}

int redirect_where(String arg, const char* *mode)
{
    *mode = "wb";
    if (str_equ(arg, ">") || str_equ(arg, "1>"))
        return STDOUT_FILENO;
    if (str_equ(arg, "2>"))
        return STDERR_FILENO;
    if (str_equ(arg, ">>") || str_equ(arg, "1>>")) {
        *mode = "ab";
        return STDOUT_FILENO;
    }
    if (str_equ(arg, "2>>")) {
        *mode = "ab";
        return STDERR_FILENO;
    }
    return -1;
}

void command_echo(StrList words)
{
    for (size_t i = 0; i < words.count; i++) {
        printf("%.*s", (int)words.items[i].len, words.items[i].data);
        if (i < words.count - 1)
            printf(" ");
    }
    printf("\n");
    fflush(stdout);
}

void command_type(StrList path_dirs, StrList words)
{
    if (words.count < 1) {
        printf("No command was provided.\n");
        fflush(stdout);
        return;
    }
    int matched = -1;
    const char *buf;
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (words.items[0].len == commands[i].len && memcmp(words.items[0].data, commands[i].data, commands[i].len) == 0) {
            matched = i;
        }
    }
    if (matched != -1) {
        printf("%.*s is a shell builtin\n", (int)words.items[0].len, words.items[0].data);
    }
    else if ((buf = search_path(path_dirs, words.items[0])) != NULL) {
        printf("%.*s is %s\n", (int)words.items[0].len, words.items[0].data, buf);
    }
    else {
        printf("%.*s: not found\n", (int)words.items[0].len, words.items[0].data);
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

    int redfd = -1;
    String redirect = {0};
    const char *mode;

    StrList program_args = {0};
    for (size_t i = 0; i < args.count; i++) {
        String arg = args.items[i];
        redfd = redirect_where(arg, &mode);
        if (redfd < 0)
            da_push(program_args, arg);
        if (redfd > 0 && i + 1 < args.count) {
            redirect = args.items[i + 1];
            break;
        }
    }

    char* *arguments = (char**)malloc(sizeof(*arguments) * (program_args.count + 1));
    for (size_t i = 0; i < program_args.count; i++)
        arguments[i] = program_args.items[i].data;
    arguments[program_args.count] = NULL;
    da_free(program_args);

    int pid = fork();
    int fd;
    if (pid == 0) {
        if (redfd > 0 && redirect.len > 0) {
            fd = redirect_to(redfd, redirect.data, mode);
        }
        execv(path, arguments);
    }
    else {
        wait(NULL);
        free(arguments);
    }
}

void execute_command(Commands type, StrList cmd, StrList path_dirs)
{
    if (cmd.count == 0) return;

    int redfd = -1;
    String redirect = {0};
    const char *mode;

    StrList program_args = {0};
    for (size_t i = 1; i < cmd.count; i++) { // we start from 1 because 0 is for the command name
        String arg = cmd.items[i];
        redfd = redirect_where(arg, &mode);
        if (redfd < 0)
            da_push(program_args, arg);
        if (redfd > 0 && i + 1 < cmd.count) {
            redirect = cmd.items[i + 1];
            break;
        }
    }

    int saved_fd = dup(redfd);
    if (redfd > 0 && redirect.len > 0) {
        redirect_to(redfd, redirect.data, mode);
    }
    switch (type) {
    case CMD_EXIT:
        exit(0);
    case CMD_ECHO:
        command_echo(program_args);
        break;
    case CMD_TYPE:
        command_type(path_dirs, program_args);
        break;
    case CMD_PWD:
        command_pwd();
        break;
    case CMD_CD:
        if (cmd.count > 1)
            command_cd(program_args.items[0]);
        else
            command_cd((String){0});
        break;
    default:
        fprintf(stderr, "Invalid command: %d\n", type);
    }
    if (redfd > 0 && redirect.len > 0) {
        dup2(saved_fd, redfd);
        close(saved_fd);
    }
    da_free(program_args);
}

StrList completion_cmds = {0};

char* cmd_name_generator(const char *text, int state)
{
    static int list_index, len;
    char *name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    while ((name = completion_cmds.items[list_index++].data)) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

char ** cmd_name_completion(const char *text, int start, int end)
{
    rl_attempted_completion_over = 0;
    return rl_completion_matches(text, cmd_name_generator);
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    char* path = getenv("PATH");
    StrList path_dirs = {0};
    if (path != NULL) {
        path_dirs = split_by_delim(path, ':');
    }
    StrList path_execs = get_execs_from_path(path_dirs);
    for (size_t i = 0; i < CMD_COUNT; i++)
        da_push(completion_cmds, commands[i]);
    for (size_t i = 0; i < path_execs.count; i++)
        da_push(completion_cmds, path_execs.items[i]);
    da_push(completion_cmds, (String){0});
    da_free(path_execs);

    while (true) {
        char *line;
        rl_attempted_completion_function = cmd_name_completion;
        line = readline("$ ");
        if (line == NULL)
            continue;
        StrList words = extract_words((char*)line);
        if (words.count == 0) continue;
        int matched = -1;
        for (size_t i = 0; i < CMD_COUNT; i++) {
            if (words.items[0].len == commands[i].len &&
                memcmp(words.items[0].data, commands[i].data, commands[i].len) == 0) {
                matched = i;
            }
        }
        char *program = NULL;
        if (matched != -1) {
            execute_command(matched, words, path_dirs);
        }
        else {
            if ((program = search_path(path_dirs, words.items[0])) != NULL) {
                execute_program(program, words);
            }
            else {
                printf("%.*s: command not found\n", (int)words.items[0].len, words.items[0].data);
                fflush(stdout);
            }
        }
        free(line);
        da_free(words);
    }


    return 0;
}
