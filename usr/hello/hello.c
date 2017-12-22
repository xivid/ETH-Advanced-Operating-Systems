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
#include "tests.h"

#define MAX_CONNECT_ATTEMPTS 10

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    ns_err_names_t ns_err;
    errval_t err;
    struct capref cap;
    int count = 0;
    do {
        ++count;
        if (count == MAX_CONNECT_ATTEMPTS) {
            printf("hello could not connect to nameserver\n");
            return 1;
        }
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    /* debug_printf("hello got nameserver's cap\n"); */

    count = 0;
    do {
        ++count;
        if (count == MAX_CONNECT_ATTEMPTS) {
            printf("hello could not lookup calc_server\n");
            return 1;
        }
        ns_lookup("calc_server", &cap, &ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    /* debug_printf("hello obtained calc_server's cap\n"); */

    struct aos_rpc *calc_rpc = malloc(sizeof(struct aos_rpc));
    calc_rpc->ws = get_default_waitset();
    err = lmp_chan_accept(&calc_rpc->lmp, DEFAULT_LMP_BUF_WORDS,
            cap);
    if (err_is_fail(err)) {
        debug_printf("hello: lmp_chan_accept failed\n");
        return err;
    }
    /* aos_rpc_send_string(calc_rpc, "2+2"); */
    /* aos_rpc_send_string(calc_rpc, "2*2"); */
    /* aos_rpc_send_string(calc_rpc, "2-2"); */
    if (argc < 2) {
        aos_rpc_send_string(calc_rpc, "2+2");
    } else {
        /* debug_printf("the string was: %s\n", argv[1]); */
        aos_rpc_send_string(calc_rpc, argv[1]);
    }

    return 0;
}
