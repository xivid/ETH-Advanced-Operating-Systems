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

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    ns_err_names_t ns_err;
    errval_t err;
    struct capref cap;
    do {
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    debug_printf("hello got nameserver's cap\n");

    char *str = "a really-really-really-really-really-really long string, which is definitely longer than 28 bytes";
    ns_register(str, cap_initep, &ns_err);
    debug_printf("ns_register error: %d\n", ns_err);

    ns_lookup(str, &cap, &ns_err);
    debug_printf("ns_lookup error: %d\n", ns_err);
    char tmp[100];
    debug_print_cap_at_capref(tmp, 100, cap);
    debug_printf("cap obtained: %s\n", tmp);

    ns_lookup("not_registered_service", &cap, &ns_err);
    debug_printf("ns_lookup error: %d\n", ns_err);

    /* debug_printf("hello done: %d\n", ns_err); */
    debug_printf("hello exiting\n");

    return 0;
}
