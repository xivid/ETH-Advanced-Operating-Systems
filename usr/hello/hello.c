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
#include <aos/waitset.h>
#include <aos/except.h>
#include <time.h>

void test_virtual_memory(int count, int size);
void test_huge_malloc(void);
void test_dynamic_slots(int count);
void test_paging(void);
void test_arm_map_unmap(void);

int main(int argc, char *argv[])
{
    debug_printf("hello started testing\n");
    test_huge_malloc();
    test_dynamic_slots(2000);
    test_paging();
    test_arm_map_unmap();
    return 0;
}

__attribute__((unused))
void test_virtual_memory(int count, int size) {
    errval_t err;

    for (int i = 0 ; i < count ; ++i) {
        size_t retsize;
        struct capref frame;
        void *buf;
        frame_alloc(&frame, size, &retsize);
        err = paging_map_frame_readwrite(&buf, size, frame);
        if (err_is_fail(err)) {
            debug_printf("Failed mapping %i: %s\n", i, err_getstring(err));
            assert(1 == 2);
        } else {
            debug_printf("Succeeded mapping %i\n", i);
            debug_printf("-----------------------------\n");
        }
        ((int *)buf)[0] = 42;
    }
    debug_printf("virtual memory test succeeded\n");
}

__attribute__((unused))
void test_huge_malloc(void) {
    debug_printf("testing huge malloc\n");
    int *tmp = malloc(100 * 1024 * 1024);
    tmp[10000] = 42;
    tmp[10001] = 43;
    debug_printf("tmp=%p &tmp[10000]=%p\n", tmp, &tmp[10000]);
    debug_printf("testing huge malloc done\n");
}

__attribute__((unused))
void test_dynamic_slots(int count) {
    debug_printf("testing dynamic slots\n");
    for (int i = 1 ; i <= count ; ++i) {
        struct capref slot;
        errval_t err = frame_alloc(&slot, BASE_PAGE_SIZE, NULL);
        if (err_is_fail(err)) {
            debug_printf("err at test: %d. err=%s\n", i, err_getstring(err));
            assert(1 == 2);
        }
        if (i % 100 == 0) {
            debug_printf("%d/%d done\n", i, count);
        }
    }
    debug_printf("testing dynamic slots done\n");
}

// Tests the paging system. Bombards it with requests of random sizes.
// Afterwards unmaps the paging regions in random order and checks that
// the defragmentation was done correctly.
// Randomness is needed to model a real system.
void test_paging(void) {
    const unsigned RUNS = 1000;
    debug_printf("testing paging\n");
    errval_t err;
    void *buf[RUNS], *first_buf;
    struct paging_state *st = get_current_paging_state();

    // allocations
    int seed = time(NULL);
    // If a test fails it can be easily reproduced knowing the seed.
    debug_printf("the seed is %d\n", seed);
    srand(seed);
    for (int i = 0 ; i < RUNS; ++i) {
        int multiplier = rand() % 20 + 1;
        err = paging_alloc(st, &buf[i], multiplier * BASE_PAGE_SIZE);
        if (err_is_fail(err)) {
            debug_printf("error at unmap at test: %d. err=%s\n",
                    i, err_getstring(err));
            assert(1 == 2);
        }
    }
    first_buf = buf[0];

    // Do a Knuth shuffle to randomize the unmaps order.
    for (int i = RUNS - 1 ; i >= 1; --i) {
        int j = rand() % i;
        void *tmp = buf[j];
        buf[j] = buf[i];
        buf[i] = tmp;
    }

    // unmaps
    for (int i = 0 ; i < RUNS ; ++i) {
        err = paging_unmap(st, buf[i]);
        if (err_is_fail(err)) {
            debug_printf("error at map at test: %d. err=%s\n",
                    i, err_getstring(err));
            assert(1 == 2);
        }
    }
    // Check that after allocating and unmapping the virtual memory will be in
    // the same state as before.
    void *new_buf;
    err = paging_alloc(st, &new_buf, BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        debug_printf("error at alloc at second phase: err=%s\n",
                err_getstring(err));
        assert(1 == 2);
    }
    if (new_buf != first_buf) {
        debug_printf("error at pointers comparison\n");
        assert(1 == 2);
    }

    err = paging_unmap(st, new_buf);
    if (err_is_fail(err)) {
        debug_printf("error at unmap at second phase: err=%s\n",
                err_getstring(err));
        assert(1 == 2);
    }
    debug_printf("testing paging done\n");
}

// Tries to map a frame, then unmap it and map it again at same address.
void test_arm_map_unmap(void) {
    debug_printf("testing arm map/unmap\n");
    void *buf;
    int *int_buf;
    errval_t err;
    struct paging_state *st = get_current_paging_state();

    err = paging_alloc(st, &buf, BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        debug_printf("error at alloc: err=%s\n", err_getstring(err));
        assert(1 == 2);
    }
    debug_printf("first alloc returned: %p\n", buf);

    int_buf = (int *) buf;
    *int_buf = 42;

    err = paging_unmap(st, buf);
    if (err_is_fail(err)) {
        debug_printf("error at ummap: err=%s\n", err_getstring(err));
        assert(1 == 2);
    }

    err = paging_alloc(st, &buf, BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        debug_printf("error at alloc: err=%s\n", err_getstring(err));
        assert(1 == 2);
    }
    debug_printf("second alloc returned: %p\n", buf);
    int_buf = (int *) buf;
    *int_buf = 43;

    debug_printf("testing arm map/unmap done\n");
}
