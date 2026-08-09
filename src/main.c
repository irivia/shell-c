#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define BUFFER_SZ 2048
char BUFFER[BUFFER_SZ];

typedef struct {
    char *data;
    size_t len;
} String;

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    enum commands {
        CMD_EXIT,
    };
    String builtins[] = {
        { "exit", 4 },
    };
    const size_t builtins_len = sizeof(builtins) / sizeof(builtins[0]);

    while (true) {
        printf("$ ");
        if (fgets(BUFFER, BUFFER_SZ, stdin) != NULL) {
            size_t len = strlen(BUFFER);
            if (len >= 1 && BUFFER[len-1] == '\n') { // Removing the newline
                len -= 1;
            }
            if (len == 0) continue;
            int matched = -1;
            for (size_t i = 0; i < builtins_len; i++) {
                if (len == builtins[i].len && memcmp(BUFFER, builtins[i].data, len) == 0) {
                    matched = i;
                }
            }
            if (matched == -1) {
                printf("%.*s: command not found\n", (int)len, BUFFER);
                fflush(stdout);
                continue;
            }
            switch (matched) {
            case CMD_EXIT:
                exit(0);
            default:
                break; // UNREACHABLE
            }
        }
    }


    return 0;
}
