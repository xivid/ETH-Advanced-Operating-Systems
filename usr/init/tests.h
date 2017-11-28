//
// Created by zhiyang on 11/22/17.
//

#ifndef _INIT_TESTS_H_
#define _INIT_TESTS_H


/* testing declarations */
bool test_alloc_free(int count);
bool test_virtual_memory(int count, int size);
bool test_multi_spawn(int spawns, char *name);
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);
bool test_remote_spawn(void);
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
