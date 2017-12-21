/**
 * \file
 * \brief Shell process
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_LINE_SIZE 1024

struct parseline {
    char line[MAX_LINE_SIZE];
    int matched;

};

struct token {
    int start;
    int length;
};

void parse(struct parseline *line);
void error(struct parseline *line, struct token t);
void echo(struct parseline *line);
struct token nextToken(struct parseline *line);

struct token nextToken(struct parseline *line) {
    bool instring = false;
    bool leading_ws = true;
    int current = line->matched;

    struct token result;
    result.start = current;
    result.length = 0;
    while (true) {
        char currchar = line->line[current];

        if (currchar == '\0') {
            break;
        }

        if (leading_ws) {
            //printf("leading %i %i\n", result.start, result.length);
            if (isspace(currchar)) {
                result.start = current + 1;
            } else if (currchar == '"') {
                result.start = current + 1;
                instring = true;
            } else {
                result.start = current;
                result.length = 1;
                leading_ws = false;
            }
        } else if (instring) {
            //printf("string %i %i\n", result.start, result.length);
            if (currchar == '"') {
                break;
            } else {
                result.length++;
            }
        } else {
            //printf("normal %i %i\n", result.start, result.length);
            if (isspace(currchar)) {
                break;
            } else if (currchar == '"') {
                break;
            } else {
                result.length++;
            }
        }

        current += 1;
    }
    line->matched = current;
    return result;
}

void parse(struct parseline *line) {
    struct token t = nextToken(line);

    if (strncmp(line->line + t.start, "echo", t.length) == 0) {
        echo(line);
    } else {
        error(line, t);
    }
}

void error(struct parseline *line, struct token t) {
    printf("Offending token: %.*s\n", t.length, line->line + t.start);
}

void echo(struct parseline *line) {
    struct token t = nextToken(line);
    while (t.length != 0) {
        printf("%.*s ", t.length, line->line + t.start);
        t = nextToken(line);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("Started Shell: Welcome!\n");
    struct parseline pline;
    while (1) {
        printf("$ ");
        fflush(stdout);
        fgets(pline.line, MAX_LINE_SIZE, stdin);
        pline.matched = 0;
        parse(&pline);
    }
    return 0;
}
