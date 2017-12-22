/**
 * \file
 * \brief Shell process
 */

#include <aos/aos_rpc.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_LINE_SIZE 1024

char gp_buffer[1024];

struct parseline {
    char line[MAX_LINE_SIZE];
    int matched;
};

struct token {
    int start;
    int length;
};

struct token
nextToken(struct parseline* line);

void echo(struct parseline* line);
void launchProcess(struct parseline* line, bool headless);
void parse(struct parseline* line);
void error(struct parseline* line, struct token t);

int main(int argc, char* argv[])
{
    printf("Started Shell: Welcome! (<><)\n");
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

void parse(struct parseline* line)
{
    struct token t = nextToken(line);

    if (strncmp(line->line + t.start, "echo", t.length) == 0) {
        echo(line);
    } else if (strncmp(line->line + t.start, "daemon", t.length) == 0) {
        launchProcess(line, true);
    } else if (strncmp(line->line + t.start, "run", t.length) == 0) {
        launchProcess(line, false);
    } else if (strncmp(line->line + t.start, "ls", t.length) == 0) {
        printf("Sorry, not implemented yet.\n");
    } else if (strncmp(line->line + t.start, "help", t.length) == 0) {
        printf("RTFM.\n");
    } else if (strncmp(line->line + t.start, "HELP!", t.length) == 0) {
        // TODO: print a useful help
    } else {
        error(line, t);
    }
}

void launchProcess(struct parseline* line, bool headless)
{
    struct token t = nextToken(line);

    coreid_t core = -1;
    if (t.length == 1) {
        core = line->line[t.start] - '0';
    }
    if (core != 0 && core != 1) {
        printf("Error: invalid core number given\n");
        return;
    }

    t = nextToken(line);

    strncpy(gp_buffer, line->line + t.start, t.length);
    gp_buffer[t.length] = '\0';

    struct aos_rpc* chan
        = aos_rpc_get_process_channel();
    domainid_t newpid;
    errval_t err = aos_rpc_process_spawn(chan, gp_buffer,
        core, &newpid);

    if (err_is_fail(err) || newpid == -1) {
        printf("Failed to spawn process!\n");
    } else {
        printf("  [%i] %s\n", newpid, gp_buffer);
    }

    while (!headless) {
        // wait for process to finish, which is never
    }
}

void echo(struct parseline* line)
{
    struct token t = nextToken(line);
    while (t.length != 0) {
        printf("%.*s ", t.length, line->line + t.start);
        t = nextToken(line);
    }
    printf("\n");
}

void error(struct parseline* line, struct token t)
{
    printf("Offending token: %.*s\n", t.length, line->line + t.start);
}

struct token
nextToken(struct parseline* line)
{
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
            if (currchar == '"') {
                break;
            } else {
                result.length++;
            }
        } else {
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
