/**
 * \file
 * \brief Shell process
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("Started Shell: Welcome!\n");
    char new_char;
    while (1) {
        scanf("%c", &new_char);
        printf("I got the char %c\n", new_char);
    }
    return 0;
}
