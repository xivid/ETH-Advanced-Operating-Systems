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
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    test_huge_malloc();
    test_dynamic_slots(3000);
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
__attribute__((unused))
bool test_huge_malloc(void) {
    debug_printf("testing huge malloc\n");
    int *tmp = malloc(100 * 1024 * 1024);
    tmp[10000] = 42;
    tmp[10001] = 43;
    debug_printf("tmp=%p &tmp[10000]=%p\n", tmp, &tmp[10000]);
    debug_printf("testing huge malloc done\n");
    return true;
}

__attribute__((unused))
bool test_dynamic_slots(int count) {
    debug_printf("testing dynamic slots\n");
    for (int i = 0 ; i < count ; ++i) {
        struct capref slot;
        /* errval_t err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL); */
        errval_t err = slot_alloc(&slot);
        if (err_is_fail(err)) {
            debug_printf("err at test: %d. err=%s\n", i, err_getstring(err));
            return false;
        }
    }
    debug_printf("testing dynamic slots done\n");
    return true;
}


