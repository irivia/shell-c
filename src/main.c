#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SZ 2048
char BUFFER[BUFFER_SZ];

int main(int argc, char *argv[])
{
    // Flush after every printf
    setbuf(stdout, NULL);

    // TODO: Uncomment the code below to pass the first stage
    printf("$ ");

    if (fgets(BUFFER, BUFFER_SZ, stdin) != NULL) {
        printf("{%s}: command not found", BUFFER);
        fflush(stdout);
    }


    return 0;
}
