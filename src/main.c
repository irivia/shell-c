#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define BUFFER_SZ 2048
char BUFFER[BUFFER_SZ];

int main(int argc, char *argv[])
{
    // Flush after every printf
    setbuf(stdout, NULL);

    while (true) {
        printf("$ ");
        if (fgets(BUFFER, BUFFER_SZ, stdin) != NULL) {
            size_t len = strlen(BUFFER);
            printf("%.*s: command not found\n", (int)len-1, BUFFER);
            fflush(stdout);
        }
    }


    return 0;
}
