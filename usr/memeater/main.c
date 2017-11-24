/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/paging.h>

static struct aos_rpc init_rpc;

const char *str = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                  "sed do eiusmod tempor incididunt ut labore et dolore magna "
                  "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
                  "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
                  "Duis aute irure dolor in reprehenderit in voluptate velit "
                  "esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
                  "occaecat cupidatat non proident, sunt in culpa qui officia "
                  "deserunt mollit anim id est laborum.";

__attribute__((unused))
static errval_t request_and_map_memory(void)
{
    errval_t err;

    size_t bytes;
    struct frame_identity id;
    debug_printf("testing memory server...\n");

    struct paging_state *pstate = get_current_paging_state();

    debug_printf("obtaining cap of %" PRIu32 " bytes...\n", BASE_PAGE_SIZE);

    struct capref cap1;
    err = aos_rpc_get_ram_cap(&init_rpc, BASE_PAGE_SIZE, BASE_PAGE_SIZE, &cap1, &bytes);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not get BASE_PAGE_SIZE cap\n");
        return err;
    }

    struct capref cap1_frame;
    err = slot_alloc(&cap1_frame);
    assert(err_is_ok(err));

    err = cap_retype(cap1_frame, cap1, 0, ObjType_Frame, BASE_PAGE_SIZE, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not retype RAM cap to frame cap\n");
        return err;
    }

    err = invoke_frame_identify(cap1_frame, &id);
    assert(err_is_ok(err));

    void *buf1;
    err = paging_map_frame(pstate, &buf1, BASE_PAGE_SIZE, cap1_frame, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not get BASE_PAGE_SIZE cap\n");
        return err;
    }

    debug_printf("got frame: 0x%" PRIxGENPADDR " mapped at %p\n", id.base, buf1);

    debug_printf("performing memset.\n");
    memset(buf1, 0x00, BASE_PAGE_SIZE);



    debug_printf("obtaining cap of %" PRIu32 " bytes using frame alloc...\n",
                 LARGE_PAGE_SIZE);

    struct capref cap2;
    err = frame_alloc(&cap2, LARGE_PAGE_SIZE, &bytes);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not get BASE_PAGE_SIZE cap\n");
        return err;
    }

    err = invoke_frame_identify(cap2, &id);
    assert(err_is_ok(err));

    void *buf2;
    err = paging_map_frame(pstate, &buf2, LARGE_PAGE_SIZE, cap2, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not get BASE_PAGE_SIZE cap\n");
        return err;
    }

    debug_printf("got frame: 0x%" PRIxGENPADDR " mapped at %p\n", id.base, buf1);

    debug_printf("performing memset.\n");
    memset(buf2, 0x00, LARGE_PAGE_SIZE);
    debug_printf("RPC: testing memory. SUCCESS.\n");

    return SYS_ERR_OK;

}

__attribute__((unused))
static errval_t test_basic_rpc(void)
{
    errval_t err;

    debug_printf("RPC: testing basic RPCs...\n");

    debug_printf("RPC: sending number...\n");
    err =  aos_rpc_send_number(&init_rpc, 42);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not send a string\n");
        return err;
    }

    debug_printf("RPC: sending small string...\n");
    err =  aos_rpc_send_string(&init_rpc, "Hello init!\n");
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not send a string\n");
        return err;
    }

    debug_printf("RPC: sending large string...\n");
    err = aos_rpc_send_string(&init_rpc, str);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not send a string\n");
        return err;
    }

    debug_printf("RPC: testing basic RPCs. SUCCESS\n");

    return SYS_ERR_OK;
}

__attribute__((unused))
static errval_t test_spawn_process(void)
{
    errval_t err;
    debug_printf("RPC: testing process spawn\n");
    domainid_t new_pid;
    err = aos_rpc_process_spawn(&init_rpc, "/armv7/sbin/hello", 0, &new_pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not spawn a process using RPC\n");
        return err;
    }
    debug_printf("new pid=%d\n", new_pid);

    debug_printf("RPC: testing process spawn. SUCCESS\n");
    return SYS_ERR_OK;
}

__attribute__((unused))
static errval_t test_remote_spawn_process(void)
{
    errval_t err;
    debug_printf("RPC: testing remote process spawn\n");
    domainid_t new_pid;
    err = aos_rpc_process_spawn(&init_rpc, "/armv7/sbin/hello", 1 - disp_get_core_id(), &new_pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "could not spawn a remote process using RPC\n");
        return err;
    }

    debug_printf("RPC: testing remote process spawn, spawned on core %d PID 0x%lx. SUCCESS\n", new_pid >> 24, new_pid & 0xFFFFFF);
    return SYS_ERR_OK;
}

__attribute__((unused))
static errval_t test_print_processes(struct aos_rpc *channel) {
    errval_t err = SYS_ERR_OK;
    domainid_t *process_ids;
    size_t n_processes;
    err = aos_rpc_process_get_all_pids(channel, &process_ids, &n_processes);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_process_get_all_pids fails\n");
        return err;
    }

    coreid_t other_coreid = (1 - disp_get_core_id()) << 24;
    domainid_t *process_ids_remote;
    size_t remote_processes;
    err = aos_rpc_process_get_all_pids_on_core(channel, &process_ids_remote,
                                               &remote_processes, other_coreid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_process_get_all_pids_on_core fails\n");
        return err;
    }

    debug_printf("------------------------------------------\n");
    debug_printf("              process  table              \n");
    debug_printf("------------------------------------------\n");
    debug_printf("%8s\t%5s\tname\n", "core", "pid");
    debug_printf("------------------------------------------\n");

    for (int i = 0; i < n_processes; i++) {
        char *pname;
        err = aos_rpc_process_get_name(channel, process_ids[i], &pname);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "aos_rpc_process_get_name fails\n");
            return err;
        }
        debug_printf("%8d\t%5d\t%s\n", process_ids[i] >> 24, process_ids[i] & 0xFFFFFF, pname);
    }

    for (int i = 0; i < remote_processes; i++) {
        char *pname;
        err = aos_rpc_process_get_name_on_core(channel, process_ids_remote[i], &pname, other_coreid);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "aos_rpc_process_get_name_on_core fails\n");
            return err;
        }
        debug_printf("%8d\t%5d\t%s\n", process_ids_remote[i] >> 24, process_ids_remote[i] & 0xFFFFFF, pname);
    }

    debug_printf("------------------------------------------\n");
    return err;
}

int main(int argc, char *argv[])
{
    errval_t err;

    debug_printf("memeater started....\n");

    init_rpc = *get_init_rpc();
    err = test_print_processes(&init_rpc);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failure in testing print processes\n");
    }

    err = test_basic_rpc();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failure in testing basic RPC\n");
    }

    err = request_and_map_memory();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not request and map memory\n");
    }

    /*err = test_spawn_process();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not spawn a process\n");
    }*/

    err = test_remote_spawn_process();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not spawn a remote process\n");
    }

    /* test printf functionality */
    /*debug_printf("testing terminal printf function...\n");
    printf("Hello world using terminal service\n");
    debug_printf("memeater terminated....\n");*/

    return EXIT_SUCCESS;
}
