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

bool test_virtual_memory(int count, int size);

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    int *tmp = malloc(100*1024*1024);
    *tmp = 42;
    debug_printf("*%p = %d\n", tmp, *tmp);
    return 0;
}

__attribute__((unused))
bool test_virtual_memory(int count, int size) {
    errval_t err;

    for (int i = 0 ; i < count ; ++i) {
        size_t retsize;
        struct capref frame;
        void *buf;
        frame_alloc(&frame, size, &retsize);
        err = paging_map_frame_readwrite(&buf, size, frame);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Failed mapping %i\n", i);
            return false;
        } else {
            debug_printf("Succeeded mapping %i\n", i);
            debug_printf("-----------------------------\n");
        }
        ((int *)buf)[0] = 42;
    }
    debug_printf("virtual memory test succeeded\n");
    return true;
}
