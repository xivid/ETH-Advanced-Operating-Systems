//
// Created by zhiyang on 11/22/17.
//

#ifndef _INIT_TESTS_H_
#define _INIT_TESTS_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* testing declarations */
bool test_alloc_free(int count);
bool test_virtual_memory(int count, int size);
bool test_multi_spawn(int spawns, char *name);
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);
bool test_remote_spawn(void);
void test_arm_map_unmap(void);
void test_paging(bool one_thread);
int test_one_thread(void *arg);
void test_threads(void);
void perform_tests(void);


/* tests implementations */
__attribute__((unused))
bool test_alloc_free(int allocations) {
    struct capref capabilities[allocations];
    errval_t err;
    size_t size;

    for (int i = 0; i < allocations; i++) {
        size = BASE_PAGE_SIZE << (i % 9);
        err = ram_alloc_aligned(capabilities + i, size, size);
        if (err_is_fail(err)) {
            debug_printf("Failed allocating capability %i\n", i);
            return false;
        }
    }
    debug_printf("allocations test succeeded\n");
    for (int i = 0; i < allocations; i++) {
        err = aos_ram_free(capabilities[i]);
        if (err_is_fail(err)) {
            debug_printf("Failed freeing capability %i\n", i);
            return false;
        }
    }
    debug_printf("frees test succeeded\n");
    return true;
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
bool test_multi_spawn(int spawns, char *name) {
    errval_t err;

    debug_printf("test_multi_spawn(%d, %s)...\n", spawns, name);
    for (int i = 0; i < spawns; i++) {
        struct spawninfo *si = malloc(sizeof(struct spawninfo));
        err = spawn_load_by_name(name, si);
        if (err_is_fail(err)) {
            debug_printf("Failed spawning process %i\n", i);
            return false;
        }
    }
    debug_printf("Spawning test succeeded\n");
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

// spawns memeater, which remote-spawns hello (this relies on the implementation in memeater)
bool test_remote_spawn(void) {
    errval_t err;
    struct spawninfo *si = malloc(sizeof(struct spawninfo));
    err = spawn_load_by_name_with_arguments("/armv7/sbin/memeater", si, "memeater Lorem ipsum dolor sit \"amet, consectetur\" adipiscing \"elit\"    ");
    if (err_is_fail(err)) {
        debug_printf("Failed spawning process memeater\n");
        return false;
    }

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


int test_one_thread(void *arg) {
    int size = 10000;
    int *tmp = malloc(sizeof(int) * size);
    for (int i = 0 ; i < size ; ++i) {
        tmp[i] = i;
    }
    debug_printf("\t*** %d *** thread done %p\n", (int) arg, tmp);
    /* test_paging(false); */
    /* test_alloc_free(10); */
    /* test_huge_malloc(); // TODO: fix me, i'm failing */
    return 0;
}

void test_threads(void) {
    int thread_count = 100;
    debug_printf("running thread tests with %d threads\n", thread_count);
    for (int i = 0 ; i < thread_count ; ++i) {
        struct thread *t = thread_create(test_one_thread, (void *) i);
        debug_printf("thread %d -> %p\n", i, t);
    }
    debug_printf("main done launching threads\n");
}


__attribute__((unused))
void perform_tests(void) {
    // here call all the tests we want to perform

    //    test_multi_spawn(1, "/armv7/sbin/hello");  // FIXME: aos_rpc_send_handler_for_init fails when spawning multiple "hello"s
    test_multi_spawn(1, "/armv7/sbin/memeater");
    // test_alloc_free(400);  // FIXME: this still doesn't work

    /*
    test_virtual_memory(10, BASE_PAGE_SIZE);
    test_huge_malloc();
    test_dynamic_slots(3000);
    */
}
#endif //_INIT_TESTS_H_
