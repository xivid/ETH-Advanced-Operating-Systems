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

int main(int argc, char *argv[])
{
    printf("Hello, world! from userspace\n");
    printf("Arguments:\n");
    for (int i = 0; i < argc; ++i) {
        printf("%i is: %s\n", i, argv[i]);
    }

    printf("convert 420 to uintptr_t:\n");
    uint32_t* num = malloc(sizeof(uint32_t));
    *num = 420;
    printf("halfway\n");
    uintptr_t num_ptr = (uintptr_t)(*num);
    printf("send now %u\n", (uint32_t) num_ptr);
    errval_t err = aos_rpc_send_number(get_init_rpc(), num_ptr);
    if (err_is_fail(err)) {
        debug_printf("hello.c: aos rpc send number failed\n");
    }
    return 0;
}
