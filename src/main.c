#include <linux/limits.h>
#include <readline/chardefs.h>
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
#include <libgen.h>
#include <pwd.h>
#include <readline/readline.h>
#include "da.h"
#include "string.h"

typedef enum {
    TOK_WORD,
    TOK_WRITE_OUT,
    TOK_APPEN_OUT,
    TOK_WRITE_ERR,
    TOK_APPEN_ERR,
} TokenType;

typedef struct {
    String str;
    TokenType type;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

bool contains_char(char c, char *chars, size_t n)
{
    if (!chars) return false;

    for (size_t i = 0; i < n; i++) {
        if (c == chars[i]) return true;
    }

    return false;
}

String chop_string(String *s);

String chop_word(String *s)
{
    if (s == NULL || s->len == 0)
        return STR_NULL;

    StringBuilder str = {0};
    char stoppers[] = {
        ' ',
        '\t',
        '\r',
        '\n',
        '>'
    };

    for (; s->len > 0 && !contains_char(*s->data, stoppers, sizeof(stoppers)); str_inc(s)) {
        if (*s->data == '\\') {
            str_inc(s);
            if (s->len > 0)
                da_push(str, *s->data);
        }
        else if (*s->data == '\'' || *s->data == '"') {
            if (expected(s, *s->data))
                str_inc(s);
            else {
                String string = chop_string(s);
                for (size_t i = 0; i < string.len; i++)
                    da_push(str, string.data[i]);
                free(string.data);
                if (contains_char(*s->data, stoppers, sizeof(stoppers)))
                    break;
            }
        }
        else {
            da_push(str, *s->data);
        }
    }

    return sb_to_str(&str);
}

String chop_string(String *s)
{
    if (s == NULL || s->len == 0 || (*s->data != '\'' && *s->data != '"'))
        return STR_NULL;

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

    return sb_to_str(&str);
}

TokenList extract_words(const char *str)
{
    if (str == NULL)
        return (TokenList){0};

    TokenList tokens = {0};

    String s = to_str(str);
    trim_right(&s);

    while (s.len > 0) {
        trim_left(&s);
        char c = *s.data;
        if (c == '\'' || c == '"') {
            String word = chop_string(&s);
            if (word.len == 0) continue;
            Token t = { .str = word, .type = TOK_WORD };
            da_push(tokens, t);
        }
        else if (expected_str(&s, "1>>")) {
            Token t = { .str = to_str("1>>"), .type = TOK_APPEN_OUT };
            da_push(tokens, t);
        }
        else if (expected_str(&s, "2>>")) {
            Token t = { .str = to_str("2>>"), .type = TOK_APPEN_ERR };
            da_push(tokens, t);
        }
        else if (expected_str(&s, "1>")) {
            Token t = { .str = to_str("1>"), .type = TOK_WRITE_OUT };
            da_push(tokens, t);
        }
        else if (expected_str(&s, "2>")) {
            Token t = { .str = to_str("2>"), .type = TOK_WRITE_ERR };
            da_push(tokens, t);
        }
        else if (expected_str(&s, ">>")) {
            Token t = { .str = to_str(">>"), .type = TOK_APPEN_OUT };
            da_push(tokens, t);
        }
        else if (c == '>') {
            str_inc(&s);
            Token t = { .str = to_str(">"), .type = TOK_WRITE_OUT };
            da_push(tokens, t);
        }
        else {
            String word = chop_word(&s);
            if (word.len == 0) continue;
            Token t = { .str = word, .type = TOK_WORD };
            da_push(tokens, t);
        }
    }

    return tokens;
}

typedef enum {
    CMD_EXIT,
    CMD_ECHO,
    CMD_TYPE,
    CMD_PWD,
    CMD_CD,
    CMD_COMPLETE,
    CMD_COUNT,
} BuiltIns;

static String builtin_cmds[CMD_COUNT] = {
    { "exit", 4 },
    { "echo", 4 },
    { "type", 4 },
    { "pwd", 3 },
    { "cd", 2 },
    { "complete", 8 },
};

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
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", STR_FMT(path_dirs.items[i]));
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
            if (snprintf(real_path, real_path_sz, "%.*s/%s", STR_FMT(path_dirs.items[i]), ent->d_name) != real_path_sz - 1) {
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
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", STR_FMT(path_dirs.items[i]));
        if ((dir = opendir(temp_buf)) == NULL)
            continue;
        while ((ent = readdir(dir)) != NULL) {
            const size_t ent_len = strlen(ent->d_name);
            const size_t real_path_sz = path_dirs.items[i].len + ent_len + 2; // one for '/' and one for null terminator
            if (snprintf(temp_buf, sizeof(temp_buf), "%.*s/%s", STR_FMT(path_dirs.items[i]), ent->d_name) != real_path_sz - 1) {
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

int redirect_to(int from_fd, int to_fd)
{
    if (from_fd < 0 || to_fd < 0) return -1;

    int newfd;
    if ((newfd = dup2(to_fd, from_fd)) == -1) {
        return -1;
    }

    return newfd;
}

int redirect_where(TokenList cmd, String *where, const char* *mode)
{
    // i + 1, because there has to be something after > or >> or whatever
    for (size_t i = 0; i + 1 < cmd.count; i++) {
        Token arg = cmd.items[i];
        switch (arg.type) {
        case TOK_WRITE_OUT:
            *mode = "wb";
            *where = cmd.items[i + 1].str;
            return STDOUT_FILENO;
        case TOK_WRITE_ERR:
            *mode = "wb";
            *where = cmd.items[i + 1].str;
            return STDERR_FILENO;
        case TOK_APPEN_OUT:
            *mode = "ab";
            *where = cmd.items[i + 1].str;
            return STDOUT_FILENO;
        case TOK_APPEN_ERR:
            *mode = "ab";
            *where = cmd.items[i + 1].str;
            return STDERR_FILENO;
        default:
            continue;
        }
    }
    return -1;
}

void command_echo(StrList words)
{
    for (size_t i = 0; i < words.count; i++) {
        printf("%.*s", STR_FMT(words.items[i]));
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
        if (str_cmp(words.items[0], builtin_cmds[i])) {
            matched = i;
        }
    }
    if (matched != -1) {
        printf("%.*s is a shell builtin\n", STR_FMT(words.items[0]));
    }
    else if ((buf = search_path(path_dirs, words.items[0])) != NULL) {
        printf("%.*s is %s\n", STR_FMT(words.items[0]), buf);
    }
    else {
        printf("%.*s: not found\n", STR_FMT(words.items[0]));
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

StrList registered_completions = {0};

void command_complete(StrList args)
{
    if (args.count < 2) return;

    String flag = next_str(&args);
    if (str_equ(flag, "-C")) {
        String path_to_completer = next_str(&args);
        String trigger = next_str(&args);
        da_push(registered_completions, trigger);
        da_push(registered_completions, path_to_completer);
    }
    else if (str_equ(flag, "-p")) {
        String trigger = next_str(&args);
        for (size_t i = 0; i + 1 < registered_completions.count; i += 2) {
            if (str_cmp(trigger, registered_completions.items[i])) {
                printf("complete -C '%.*s' %.*s\n", STR_FMT(registered_completions.items[i+1]), STR_FMT(trigger));
                fflush(stdout);
                return;
            }
        }
        printf("complete: %.*s: no completion specification\n", STR_FMT(trigger));
        fflush(stdout);
    }
}

void execute_program(const char *path, TokenList args, StrList env, int from_fd, int to_fd)
{
    if (path == NULL || args.count == 0)
        return;

    StrList program_args = {0};
    for (size_t i = 0; i < args.count && args.items[i].type == TOK_WORD; i++) {
        Token arg = args.items[i];
        da_push(program_args, arg.str);
    }

    char* *arguments = (char**)malloc(sizeof(*arguments) * (program_args.count + 1));
    for (size_t i = 0; i < program_args.count; i++)
        arguments[i] = program_args.items[i].data;
    arguments[program_args.count] = NULL;
    da_free(program_args);

    char* *env_vars = (char**)malloc(sizeof(*env_vars) * (env.count + 1));
    for (size_t i = 0; i < env.count; i++) {
        env_vars[i] = env.items[i].data;
    }
    env_vars[env.count] = NULL;

    int pid = fork();
    int fd;
    if (pid == 0) {
        if (from_fd > 0 && to_fd > 0) {
            fd = redirect_to(from_fd, to_fd);
        }
        execve(path, arguments, env_vars);
    }
    else {
        wait(NULL);
        free(env_vars);
        free(arguments);
    }
}

void execute_command(BuiltIns type, TokenList cmd, StrList path_dirs)
{
    if (cmd.count == 0) return;

    int redfd = -1;
    String redirect = {0};
    const char *mode;

    StrList program_args = {0};
    // We start from 1 because 0 is for the command name
    for (size_t i = 1; i < cmd.count && cmd.items[i].type == TOK_WORD; i++) {
        Token arg = cmd.items[i];
        da_push(program_args, arg.str);
    }
    redfd = redirect_where(cmd, &redirect, &mode);

    int saved_fd = dup(redfd);
    if (redfd > 0 && redirect.len > 0) {
        FILE *f = fopen(redirect.data, mode);
        if (f) {
            int fd = fileno(f);
            redirect_to(redfd, fd);
            close(fd);
        }
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
            command_cd(STR_NULL);
        break;
    case CMD_COMPLETE:
        command_complete(program_args);
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

char** cmd_name_completion(const char *text, int start, int end)
{
    rl_completion_append_character = ' ';
    rl_attempted_completion_over = 0;
    if (start == 0) {
        return rl_completion_matches(text, cmd_name_generator);
    }
    static char *buffer = NULL;
    const size_t buffer_sz = 4096;
    if (!buffer) {
        buffer = (char*)malloc(buffer_sz);
    }
    TokenList words = extract_words(rl_line_buffer);
    for (size_t i = 0; i + 1 < registered_completions.count; i += 2) {
        String trigger = registered_completions.items[i];
        String path = registered_completions.items[i + 1];
        if (!str_cmp(trigger, words.items[0].str)) continue;
        TokenList args = {0};
        Token t = { .str = to_str(basename(path.data)), .type = TOK_WORD };
        da_push(args, t);
        t.str = trigger;
        da_push(args, t);
        for (size_t i = words.count - 1; i > 0; i--) {
            t.str = words.items[1].str;
            da_push(args, t);
        }
        StrList envs = {0};
        da_push(envs, to_str_fmt("COMP_LINE=%s", rl_line_buffer));
        da_push(envs, to_str_fmt("COMP_POINT=%d", end));
        FILE *f = fmemopen(buffer, buffer_sz, "r+");
        int fd = fileno(f);
        execute_program(path.data, args, envs, STDOUT_FILENO, fd);
        da_free(args);
        da_free(envs);
        close(fd);

        // if (sz == 0) {
        //     rl_completion_append_character = '\0';
        //     fclose(f);
        //     printf("\x07");
        //     fflush(stdout);
        //     break;
        // }
        break;
    }

    da_free(words);
    return NULL;
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
        da_push(completion_cmds, builtin_cmds[i]);
    for (size_t i = 0; i < path_execs.count; i++)
        da_push(completion_cmds, path_execs.items[i]);
    da_push(completion_cmds, STR_NULL);
    da_free(path_execs);

    while (true) {
        char *line;
        rl_attempted_completion_function = cmd_name_completion;
        line = readline("$ ");
        if (line == NULL)
            continue;
        TokenList tokens = extract_words((char*)line);
        if (tokens.count == 0) continue;
        int matched = -1;
        for (size_t i = 0; i < CMD_COUNT; i++) {
            if (str_cmp(tokens.items[0].str, builtin_cmds[i])) {
                matched = i;
                break;
            }
        }
        // da_foreach(tokens, tok) {
        //     printf("Word: %.*s\n", STR_FMT(tok->str));
        // }
        char *program = NULL;
        if (matched != -1) {
            execute_command(matched, tokens, path_dirs);
        }
        else if ((program = search_path(path_dirs, tokens.items[0].str)) != NULL) {
            String where_to = {0};
            const char *mode = NULL;
            int from_fd = redirect_where(tokens, &where_to, &mode);
            int to_fd = -1;
            if (from_fd > 0 && where_to.len > 0) {
                FILE *f = fopen(where_to.data, mode);
                if (f) to_fd = fileno(f);
            }
            execute_program(program, tokens, (StrList){0}, from_fd, to_fd);
            if (to_fd != -1)
                close(to_fd);
        }
        else {
            printf("%.*s: command not found\n", STR_FMT(tokens.items[0].str));
            fflush(stdout);
        }
        free(line);
        da_free(tokens);
    }


    return 0;
}
