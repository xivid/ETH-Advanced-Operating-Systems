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
#include <aos/waitset.h>
#include <aos/except.h>
#include <time.h>
#include "tests.h"

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    struct capref ns_capref;
    errval_t err = aos_rpc_get_nameserver_ep(get_init_rpc(), &ns_capref);
    if (err_is_fail(err)) {
        debug_printf("hello failed getting ns ep: %s\n", err_getstring(err));
        return -1;
    }
    char str[1000];
    debug_print_capref(str, 1000, ns_capref);
    debug_printf("hello got cap: %s\n", str);
    /* size_t retbytes; */
    /* struct capref ret; */
    /* aos_rpc_get_ram_cap(get_init_rpc(), 4096, 4096, &ret, &retbytes); */
    /* char str[1000]; */
    /* debug_print_capref(str, 1000, ret); */
    /* debug_printf("hello got cap: %s\n", str); */
    /* debug_printf("hello got size: %d\n", retbytes); */
    return 0;
}
