#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SZ 2048
char BUFFER[BUFFER_SZ];

int main(int argc, char *argv[])
{
    // Flush after every printf
    setbuf(stdout, NULL);

    // TODO: Uncomment the code below to pass the first stage
    printf("$ ");

    if (fgets(BUFFER, BUFFER_SZ, stdin) != NULL) {
        size_t len = strlen(BUFFER);
        printf("%.*s: command not found", (int)len-1, BUFFER);
        fflush(stdout);
    }


    return 0;
}
