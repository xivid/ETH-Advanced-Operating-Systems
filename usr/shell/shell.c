/**
 * \file
 * \brief Shell process
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("Started Shell: Welcome!\n");
    setbuf(stdout, NULL);
    char new_char;
    while (1) {
        scanf("%c", &new_char);
        if (new_char != 0) {
            printf("%c", new_char);
            if (new_char == '\r') {
                printf("\n");
            }
        }
    }
    return 0;
}
