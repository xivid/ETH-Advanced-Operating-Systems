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
#include "terminal.h"
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
        err = register_getchar_interrupt_handler();
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "register_getchar_handler");
            return EXIT_FAILURE;
        }


        debug_printf("booting core 1\n");
        err = boot_core(1);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/init/main.c: boot_core(1) failed");
            return EXIT_FAILURE;
        }
    }

    /* perform_tests(); */

    // test remote spawn hello.0 from memeater.1
    // WARNING: in principle, we shouldn't send anything over urpc before core 1 has finished reading the resources initially described in urpc frame.
    // however, the resources seems impossible to be overwritten under any circumstances.
    // So, I put this warning here just for reference in case some related problem occurs in the future.

    if (my_core_id == 1) {
        test_remote_spawn();
    }

    debug_printf("Message handler loop\n");

    // Hang around
    struct waitset *default_ws = get_default_waitset();
    bool has_event = false;
    // volatile uint32_t *rx;
    while (true) {
        // local events
        err = event_dispatch_non_block(default_ws);
        if (err_is_ok(err)) {
            has_event = true;
        }

        // urpc events
        err = urpc_read_and_process_non_block(my_core_id);
        if (err_is_ok(err)) {
            has_event = true;
        }

        // if we did something, we try to do more again immediately. Otherwise we yield
        if (has_event) {
            has_event = false;
            continue;
        }
        else {
            thread_yield();
        }
    }

    return EXIT_SUCCESS;
}
