/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/morecore.h>
#include <aos/paging.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/coreboot.h>
#include <multiboot.h>

#include <mm/mm.h>
#include "mem_alloc.h"
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include "rpc_server.h"
#include "core_boot.h"
#include "tests.h"


/* init declarations */
coreid_t my_core_id;
struct bootinfo *bi;
struct process_manager pm;

/* main */
int main(int argc, char *argv[])
{
    errval_t err;
    client_list = NULL;
    /* Set the core id in the disp_priv struct */
    err = invoke_kernel_get_core_id(cap_kernel, &my_core_id);
    assert(err_is_ok(err));
    disp_set_core_id(my_core_id);

    debug_printf("init: on core %" PRIuCOREID " invoked as:", my_core_id);
    for (int i = 0; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");

    /* First argument contains the bootinfo location, if it's not set */
    bi = (struct bootinfo*)strtol(argv[1], NULL, 10);
    if (!bi) {
        assert(my_core_id > 0);
    }

    if (my_core_id == 0) {
        err = initialize_ram_alloc();
        if(err_is_fail(err)){
            DEBUG_ERR(err, "initialize_ram_alloc");
            return EXIT_FAILURE;
        }

        err = core_boot_dump_resources();
        if(err_is_fail(err)){
            DEBUG_ERR(err, "core_boot_dump_resources");
            return EXIT_FAILURE;
        }
    } else {
        err = core_boot_load_resources(my_core_id);  // initialize_ram_alloc is done inside
        if(err_is_fail(err)){
            DEBUG_ERR(err, "core_boot_load_resources(%d)", (int) my_core_id);
            return EXIT_FAILURE;
        }
    }

    debug_printf("init rpc\n");
    err = init_rpc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "init_rpc");
        return EXIT_FAILURE;
    }

    debug_printf("init process manager\n");
    err = process_manager_init(&pm);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "process_manager_init");
        return EXIT_FAILURE;
    }

    // WARNING: boot_core() must only be called AFTER core_boot_dump_resources()!
    if (my_core_id == 0) {
        debug_printf("booting core 1\n");
        err = boot_core(1);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/init/main.c: boot_core(1) failed");
            return EXIT_FAILURE;
        }
    }

    perform_tests();

    // test remote spawn hello.1 from memeater.0
    // WARNING: in principle, we shouldn't send anything over urpc before core 1 has finished reading the resources initially described in urpc frame.
    // however, the resources seems impossible to be overwritten under any circumstances.
    // So, I put this warning here just for reference in case some related problem occurs in the future.

    if (my_core_id == 0) {
        test_remote_spawn();
    }

    debug_printf("Message handler loop\n");

    // Both cores listen for urpc requests
    /*if (my_core_id == 1) {
        urpc_read_until_ack(NULL, my_core_id);  // TODO: should be some similar read() function which however does not stop on ack
        }*/

    // Hang around
    struct waitset *default_ws = get_default_waitset();
    bool did_something = false;
    volatile uint32_t *rx;
    while (true) {
        // local events
        err = check_for_event(default_ws);
        if (err_is_ok(err)) {
            struct event_closure closure;
            err = get_next_event(default_ws, &closure);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "usr/init/main.c: event handling loop failure");
                return EXIT_FAILURE;
            }
            assert(closure.handler != NULL);
            closure.handler(closure.arg);
            did_something = true;
        }
        
        // urpc events
        rx = (uint32_t *)urpc_shared_base + my_core_id * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
        if (*(rx + LINE_WORDS - 1)) {
            // there's something new
            
            // TODO: call handler for urpc
            // process_urpc();
            
            
            did_something = true;
        }
        
        // if we did something, we try to do more again immediately. Otherwise we yield
        if (did_something) {
            did_something = false;
            continue;
        }
        else
            thread_yield();
    }

    return EXIT_SUCCESS;
}
