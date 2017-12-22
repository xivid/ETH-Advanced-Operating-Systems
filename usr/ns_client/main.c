/**
 * \file
 * \brief Hello world application
 */

/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */


#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/nameserver.h>
#include <aos/waitset.h>
#include <aos/except.h>
#include <time.h>

void print_table(char *str);

void print_table(char *str)
{
    printf("Services registered with the nameserver:\n");
    int count = 1;
    char *prev = str, *cur;
    while (true) {
        cur = strchr(prev, ';');
        if (cur == NULL) {
            printf("%d. %s\n", count, prev);
            return;
        }
        *cur = '\0';
        printf("%d. %s\n", count, prev);
        *cur = ';';
        ++count;
        prev = cur + 1;
    }
}

#define MAX_CONNECT_ATTEMPTS 10
int main(int argc, char *argv[])
{
    errval_t err;
    ns_err_names_t ns_err;
    int count = 0;

    do {
        ++count;
        if (count == MAX_CONNECT_ATTEMPTS) {
            printf("ns_client could not connect to nameserver\n");
            return 1;
        }
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);

    char *results = NULL;
    err = ns_enum(&results, &ns_err);
    if (err_is_fail(err) || ns_err != NS_ERR_OK) {
        printf("ns_enum failed: err=%d ns_err=%d\n", err, ns_err);
    } else {
        print_table(results);
    }

    return 0;
}

