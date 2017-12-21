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
        printf("%c", new_char);
    }
    return 0;
}
