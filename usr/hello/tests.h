//
// Created by zhiyang on 11/22/17.
//

#ifndef _INIT_TESTS_H_
#define _INIT_TESTS_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* testing declarations */
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);
void test_paging(bool one_thread);
int test_one_thread(void *arg);
void test_threads(void);


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

// Tests the paging system. Bombards it with requests of random sizes.
// // Afterwards unmaps the paging regions in random order and checks that
// // the defragmentation was done correctly.
// // Randomness is needed to model a real system.
void test_paging(bool one_thread) {
    const unsigned RUNS = 100;
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
    if (one_thread) {
        void *new_buf;
        err = paging_alloc(st, &new_buf, BASE_PAGE_SIZE);
        if (err_is_fail(err)) {
            debug_printf("error at alloc at second phase: err=%s\n",
                    err_getstring(err));
            assert(1 == 2);
        }
        if (new_buf != first_buf) {
            debug_printf("error at pointers comparison\n");
            paging_state_dump(st);
            assert(1 == 2);
        }

        err = paging_unmap(st, new_buf);
        if (err_is_fail(err)) {
            debug_printf("error at unmap at second phase: err=%s\n",
                    err_getstring(err));
            assert(1 == 2);
        }
    }
    debug_printf("testing paging done\n");
}

int test_one_thread(void *arg) {
    int size = 10000;
    int *tmp = malloc(sizeof(int) * size);
    for (int i = 0 ; i < size ; ++i) {
        tmp[i] = i;
    }
    debug_printf("\t*** %d *** thread done %p\n", (int) arg, tmp);
    /* test_paging(false); */
    /* test_huge_malloc(); // TODO: fix me, i'm failing */
    return 0;
}

void test_threads(void) {
    int thread_count = 20;
    debug_printf("running thread tests with %d threads\n", thread_count);
    for (int i = 0 ; i < thread_count ; ++i) {
        struct thread *t = thread_create(test_one_thread, (void *) i);
        debug_printf("thread %d -> %p\n", i, t);
    }
    debug_printf("main done launching threads\n");
}

#endif //_INIT_TESTS_H_
