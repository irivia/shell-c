#include <linux/limits.h>
#include <readline/chardefs.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <libgen.h>
#include <pwd.h>
#include <readline/readline.h>
#include <fcntl.h>
#include <poll.h>
#include "da.h"
#include "lexer.h"

typedef enum {
    CMD_EXIT,
    CMD_ECHO,
    CMD_TYPE,
    CMD_PWD,
    CMD_CD,
    CMD_COMPLETE,
    CMD_JOBS,
    CMD_COUNT,
} BuiltIns;

static Token builtin_cmds[CMD_COUNT] = {
    { "exit", 4, TOK_WORD },
    { "echo", 4, TOK_WORD },
    { "type", 4, TOK_WORD },
    { "pwd", 3, TOK_WORD },
    { "cd", 2, TOK_WORD },
    { "complete", 8, TOK_WORD },
    { "jobs", 4, TOK_WORD },
};

typedef struct {
    int pid;
    int idx;
    int fds[2];
    char **cmd;
    StringBuilder buffer;
} Job;

typedef struct {
    Job *items;
    size_t count;
    size_t capacity;
    size_t ptr;
} JobList;

Job jobs_dequeue(JobList *list)
{
    if (!list || !list->count) return (Job){0};

    list->ptr = list->ptr >= list->count ? 0 : list->ptr;
    Job job = list->items[list->ptr++];
    list->count -= 1;

    return job;
}

ssize_t jobs_get_first(JobList *list)
{
    if (!list || !list->count) return -1;

    ssize_t ptr = list->ptr % list->count;
    return ptr;
}

void job_free(Job *job)
{
    if (!job) return;

    da_free(job->buffer);
    free_cstrlist(&job->cmd);
}

TokenList registered_completions = {0};
JobList jobs = {0};
int jobs_idx = 0;

bool is_file_executable(const char *file)
{
    struct stat path_stat;

    return (
        file != NULL && stat(file, &path_stat) == 0 &&
        S_ISREG(path_stat.st_mode) && access(file, X_OK) == 0
    );
}

char* search_path(const TokenList path_dirs, const Token cmd)
{
    if (path_dirs.count == 0 || cmd.data == NULL || cmd.len == 0)
        return NULL;
    DIR *dir;
    struct dirent *ent;
    char temp_buf[PATH_MAX];
    for (size_t i = 0; i < path_dirs.count; i++) {
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", TOK_FMT(path_dirs.items[i]));
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
            if (snprintf(real_path, real_path_sz, "%.*s/%s", TOK_FMT(path_dirs.items[i]), ent->d_name) != real_path_sz - 1) {
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

TokenList get_execs_from_path(TokenList path_dirs)
{
    TokenList execs = {0};
    if (path_dirs.count == 0)
        return execs;
    DIR *dir;
    struct dirent *ent;
    char temp_buf[PATH_MAX];
    for (size_t i = 0; i < path_dirs.count; i++) {
        snprintf(temp_buf, sizeof(temp_buf), "%.*s", TOK_FMT(path_dirs.items[i]));
        if ((dir = opendir(temp_buf)) == NULL)
            continue;
        while ((ent = readdir(dir)) != NULL) {
            const size_t ent_len = strlen(ent->d_name);
            const size_t real_path_sz = path_dirs.items[i].len + ent_len + 2; // one for '/' and one for null terminator
            if (snprintf(temp_buf, sizeof(temp_buf), "%.*s/%s", TOK_FMT(path_dirs.items[i]), ent->d_name) != real_path_sz - 1) {
                continue;
            }
            if (is_file_executable(temp_buf)) {
                Token s = cstr_to_tok(ent->d_name);
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

int redirect_where(TokenList cmd, Token *where, const char* *mode)
{
    // i + 1, because there has to be something after > or >> or whatever
    for (size_t i = 0; i + 1 < cmd.count; i++) {
        Token arg = cmd.items[i];
        switch (arg.type) {
        case TOK_WRITE_OUT:
            *mode = "wb";
            *where = cmd.items[i + 1];
            return STDOUT_FILENO;
        case TOK_WRITE_ERR:
            *mode = "wb";
            *where = cmd.items[i + 1];
            return STDERR_FILENO;
        case TOK_APPEN_OUT:
            *mode = "ab";
            *where = cmd.items[i + 1];
            return STDOUT_FILENO;
        case TOK_APPEN_ERR:
            *mode = "ab";
            *where = cmd.items[i + 1];
            return STDERR_FILENO;
        default:
            continue;
        }
    }
    return -1;
}

void command_echo(TokenList words)
{
    for (size_t i = 0; i < words.count; i++) {
        printf("%.*s", TOK_FMT(words.items[i]));
        if (i < words.count - 1)
            printf(" ");
    }
    printf("\n");
    fflush(stdout);
}

void command_type(TokenList path_dirs, TokenList words)
{
    if (words.count < 1) {
        printf("No command was provided.\n");
        fflush(stdout);
        return;
    }
    int matched = -1;
    const char *buf;
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (tok_cmp(words.items[0], builtin_cmds[i])) {
            matched = i;
        }
    }
    if (matched != -1) {
        printf("%.*s is a shell builtin\n", TOK_FMT(words.items[0]));
    }
    else if ((buf = search_path(path_dirs, words.items[0])) != NULL) {
        printf("%.*s is %s\n", TOK_FMT(words.items[0]), buf);
    }
    else {
        printf("%.*s: not found\n", TOK_FMT(words.items[0]));
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

void command_cd(Token path)
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

void command_complete(TokenList args)
{
    if (args.count < 2) return;

    Token flag = next_tok(&args);
    if (tok_equ(flag, "-C")) {
        Token path_to_completer = next_tok(&args);
        Token trigger = next_tok(&args);
        da_push(registered_completions, tok_dup(trigger));
        da_push(registered_completions, tok_dup(path_to_completer));
    }
    else if (tok_equ(flag, "-p")) {
        Token trigger = next_tok(&args);
        for (size_t i = 0; i + 1 < registered_completions.count; i += 2) {
            if (tok_cmp(trigger, registered_completions.items[i])) {
                printf("complete -C '%.*s' %.*s\n", TOK_FMT(registered_completions.items[i+1]), TOK_FMT(trigger));
                fflush(stdout);
                return;
            }
        }
        printf("complete: %.*s: no completion specification\n", TOK_FMT(trigger));
        fflush(stdout);
    }
    else if (tok_equ(flag, "-r")) {
        Token trigger = next_tok(&args);
        for (size_t i = 0; i + 1 < registered_completions.count; i += 2) {
            if (tok_cmp(trigger, registered_completions.items[i])) {
                registered_completions.items[i] = registered_completions.items[registered_completions.count - 2];
                registered_completions.items[i + 1] = registered_completions.items[registered_completions.count - 1];
                registered_completions.count -= 1;
                return;
            }
        }
    }
}

void command_jobs(TokenList cmd)
{
    // [1]+  Running                 sleep 10 &
    const ssize_t first = jobs_get_first(&jobs);
    if (first < 0) return;
    Job list[jobs.count - first];
    size_t list_count = sizeof(list) / sizeof(list[0]);
    for (ssize_t i = first; i < jobs.count; i++) {
        Job job = jobs.items[i];
        list[job.idx - 1] = job;
    }
    for (ssize_t i = 0; i < list_count; i++) {
        Job job = list[i];
        char marker = ' ';
        if (i == list_count - 1) marker = '+';
        else if (i == list_count - 2) marker = '-';
        printf("[%d]%c  Running                 ", job.idx, marker);
        while (*(job.cmd) != NULL) {
            printf("%s ", *(job.cmd));
            job.cmd++;
        }
        printf("&\n");
        fflush(stdout);
    }
}

void execute_program(const char *path, TokenList args, TokenList env, int from_fd, int to_fd)
{
    if (path == NULL || args.count == 0)
        return;

    size_t args_count = 0;
    Token arg = TOK_NULL;

    for (size_t i = 0; i < args.count && args.items[i].type == TOK_WORD; i++)
        args_count++;

    bool background = args.count > 0 && args.items[args.count - 1].type == TOK_JOB;
    char **arguments = toklist_to_cstrlist(args, args_count);
    char **env_vars = toklist_to_cstrlist(env, env.count);

    int job_fds[2];
    if (background) pipe(job_fds);

    int pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Couldn't fork program\n");
    }
    else if (pid == 0) {
        if (from_fd > 0 && to_fd > 0) {
            redirect_to(from_fd, to_fd);
        }
        else if (background) {
            close(job_fds[0]);
            redirect_to(STDOUT_FILENO, job_fds[1]);
            redirect_to(STDERR_FILENO, job_fds[1]);
        }
        execve(path, arguments, env_vars);
    }
    else if (!background) {
        wait(NULL);
        free_cstrlist(&arguments);
    }
    else {
        jobs_idx++;
        Job job = { .idx = jobs_idx, .pid = pid };
        close(job_fds[1]);
        job.fds[0] = job_fds[0];
        job.fds[1] = job_fds[1];
        job.cmd = arguments;
        da_push(jobs, job);
        printf("[%d] %d\n", jobs_idx, pid);
        fflush(stdout);
    }
    free_cstrlist(&env_vars);
}

bool run_if_program(TokenList tokens, TokenList path_dirs)
{
    if (tokens.count == 0) return false;

    char *program = NULL;

    if (access(tokens.items[0].data, F_OK) == 0)
        program = tokens.items[0].data;
    else
        program = search_path(path_dirs, tokens.items[0]);

    if (!program) return false;

    Token where_to = {0};
    const char *mode = NULL;
    int from_fd = redirect_where(tokens, &where_to, &mode);
    int to_fd = -1;
    if (from_fd > 0 && where_to.len > 0) {
        FILE *f = fopen(where_to.data, mode);
        if (f) to_fd = fileno(f);
    }
    execute_program(program, tokens, (TokenList){0}, from_fd, to_fd);
    if (to_fd != -1)
        close(to_fd);

    return true;
}

void execute_command(BuiltIns type, TokenList cmd, TokenList path_dirs)
{
    if (cmd.count == 0) return;

    int redfd = -1;
    Token redirect = {0};
    const char *mode;

    next_tok(&cmd); // Removing the first token (command name)
    size_t args_count = 0;
    for (size_t i = 0; i < cmd.count && cmd.items[i].type == TOK_WORD; i++) {
        args_count++;
    }
    redfd = redirect_where(cmd, &redirect, &mode);
    cmd.count = args_count;

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
        command_echo(cmd);
        break;
    case CMD_TYPE:
        command_type(path_dirs, cmd);
        break;
    case CMD_PWD:
        command_pwd();
        break;
    case CMD_CD:
        if (cmd.count > 0)
            command_cd(cmd.items[0]);
        else
            command_cd(TOK_NULL);
        break;
    case CMD_COMPLETE:
        command_complete(cmd);
        break;
    case CMD_JOBS:
        command_jobs(cmd);
        break;
    default:
        fprintf(stderr, "Invalid command: %d\n", type);
    }
    if (redfd > 0 && redirect.len > 0) {
        dup2(saved_fd, redfd);
        close(saved_fd);
    }
}

TokenList completion_cmds = {0};

char* cmd_name_generator(const char *text, int state)
{
    static int list_index, len;
    char *name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    while (list_index < completion_cmds.count && (name = completion_cmds.items[list_index++].data)) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

char* custom_cmd_generator(const char *text, int state)
{
    static int list_index, len;
    Token trigger = {0};
    Token path = {0};
    static TokenList completions = {0};
    Token name = {0};

    if (!state) {
        toklist_free(&completions);
        completions = (TokenList){0};
        list_index = 0;
        len = strlen(text);
        TokenList words = extract_words(rl_line_buffer);
        if (words.count == 0) return NULL;
        for (size_t i = 0; i + 1 < registered_completions.count; i += 2) {
            Token cmd = words.items[0];
            Token temp_trigger = registered_completions.items[i];
            path = registered_completions.items[i + 1];
            if (!tok_cmp(cmd, temp_trigger)) continue;
            trigger = temp_trigger;
            break;
        }
        if (trigger.len == 0 || path.len == 0) return NULL;
        TokenList envs = {0};
        TokenList args = {0};
        da_push(envs, to_tok_fmt("COMP_LINE=%s", rl_line_buffer));
        da_push(envs, to_tok_fmt("COMP_POINT=%d", rl_end));
        Token s = cstr_to_tok(basename(path.data));
        da_push(args, s);
        da_push(args, tok_dup(trigger));
        da_push(args, cstr_to_tok(text));
        if (words.count > 1)
            da_push(args, tok_dup(words.items[words.count - 2]));
        else
            da_push(args, cstr_to_tok(""));
        int fds[2];
        pipe(fds);
        char buffer[4096];
        const size_t buffer_sz = sizeof(buffer);
        execute_program(path.data, args, envs, STDOUT_FILENO, fds[1]);
        toklist_free(&args);
        toklist_free(&envs);
        toklist_free(&words);
        close(fds[1]);
        ssize_t bytes = read(fds[0], buffer, buffer_sz);
        close(fds[0]);
        if (bytes <= 0) {
            printf("\x07");
            fflush(stdout);
            return NULL;
        }
        if (bytes >= buffer_sz) {
            fprintf(stderr, "Buffer of size: %zu couldn't accomodate output from '%s'\n", buffer_sz, path.data);
            return NULL;
        }
        buffer[bytes] = '\0';
        completions = split_by_delim(buffer, '\n');
        qsort(completions.items, completions.count, sizeof(Token), qsort_tok_fun);
    }

    while (list_index < completions.count && (name = completions.items[list_index++]).len > 0) {
        if (len <= name.len && strncmp(name.data, text, len) == 0)
            return strndup(name.data, name.len);
    }

    return NULL;
}

char** cmd_name_completion(const char *text, int start, int end)
{
    if (start == 0) {
        return rl_completion_matches(text, cmd_name_generator);
    }
    else {
        return rl_completion_matches(text, custom_cmd_generator);
    }

    return NULL;
}

void my_display_matches(char **matches, int num_matches, int max_length)
{
    int columns = 80 / (max_length + 2);
    if (columns < 1)
        columns = 1;

    int rows = (num_matches + columns - 1) / columns;

    putchar('\n');

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < columns; col++) {
            int i = row + col * rows;
            if (i >= num_matches)
                continue;

            struct stat st;
            if (stat(matches[i + 1], &st) == 0 && S_ISDIR(st.st_mode)) {
                size_t len = strlen(matches[i + 1]);
                char *match = (char*)malloc(len + 2);
                match[len] = '/';
                match[len + 1] = '\0';
                memcpy(match, matches[i + 1], len);
                free(matches[i + 1]);
                matches[i + 1] = match;
            }
            printf("%-*s", max_length + 2, matches[i + 1]);
        }
        putchar('\n');
        rl_on_new_line();
    }
}

void poll_jobs()
{
    if (!jobs.count) return;

    struct pollfd fds[jobs.count];
    for (size_t i = 0; i < jobs.count; i++) {
        fds[i].fd = jobs.items[i].fds[0];
        fds[i].events = POLLIN;
    }
    int ret = poll(fds, jobs.count, 50);
    if (ret <= 0) return;
    for (size_t i = 0; i < jobs.count;) {
        if (fds[i].revents != POLLIN) {
            i++;
            continue;
        }
        Job job = jobs.items[i];
        char buffer[4096];
        const size_t buffer_sz = sizeof(buffer);
        ssize_t bytes;
        while ((bytes = read(job.fds[0], buffer, buffer_sz)) > 0) {
            for (ssize_t i = 0; i < bytes; i++) {
                da_push(job.buffer, buffer[i]);
            }
        }
        close(job.fds[0]);
        if (job.buffer.count) {
            da_push(job.buffer, '\0');
            if (job.buffer.items[job.buffer.count - 2] == '\n')
                printf("%s", job.buffer.items);
            else
                printf("%s\n", job.buffer.items);
            fflush(stdout);
        }
        job_free(&job);
        da_remove(jobs, i);
        jobs_idx--;
    }
}

void print_jobs()
{
    da_foreach(jobs, job) {
        printf("%d [%d] ", job->pid, job->idx);
        char **cmd = job->cmd;
        while (*cmd != NULL) {
            printf("%s ", *cmd);
            cmd++;
        }
        printf("&\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    char* path = getenv("PATH");
    TokenList path_dirs = {0};
    if (path != NULL) {
        path_dirs = split_by_delim(path, ':');
    }
    TokenList path_execs = get_execs_from_path(path_dirs);
    for (size_t i = 0; i < CMD_COUNT; i++)
        da_push(completion_cmds, builtin_cmds[i]);
    for (size_t i = 0; i < path_execs.count; i++)
        da_push(completion_cmds, path_execs.items[i]);
    da_push(completion_cmds, TOK_NULL);
    da_free(path_execs);

    rl_attempted_completion_function = cmd_name_completion;
    rl_completion_display_matches_hook = my_display_matches;
    rl_completion_append_character = ' ';
    rl_attempted_completion_over = 0;

    while (true) {
        poll_jobs();
        // print_jobs();
        char *line;
        // struct pollfd fd = {
        //     .fd = STDIN_FILENO,
        //     .events = POLLIN
        // };
        // poll(&fd, 1, 50);
        line = readline("$ ");
        if (line == NULL)
            continue;
        TokenList tokens = extract_words((char*)line);
        if (tokens.count == 0) continue;
        int matched = -1;
        for (size_t i = 0; i < CMD_COUNT; i++) {
            if (tok_cmp(tokens.items[0], builtin_cmds[i])) {
                matched = i;
                break;
            }
        }
        char *program = NULL;
        if (matched != -1) {
            execute_command(matched, tokens, path_dirs);
        }
        else if (!run_if_program(tokens, path_dirs)) {
            printf("%.*s: command not found\n", TOK_FMT(tokens.items[0]));
            fflush(stdout);
        }
        free(line);
        toklist_free(&tokens);
    }


    return 0;
}
