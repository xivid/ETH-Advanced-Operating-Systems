/**
 * \file
 * \brief UDP Echo server - parrot
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

int main(int argc, char *argv[])
{
    if (argc < 2) {
        debug_printf("Usage: %s [port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    errval_t err;

    debug_printf("parrot started on port %s\n", argv[1]);

    debug_printf("Parrot %s enters message handler loop\n", argv[1]);

    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}
