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

#include <mm/mm.h>
#include "mem_alloc.h"
#include <spawn/spawn.h>

struct client {
    struct EndPoint end;
    struct client* prev;
    struct client* next;

    struct lmp_chan lmp;
} *client_list = NULL;
errval_t recv_handler(void* arg);
errval_t whois(struct capref cap, struct client **he_is);
void* answer_number(struct capref* cap, struct lmp_recv_msg* msg);
void* answer_init(struct capref* cap);
errval_t send_received(void* arg);
bool test_alloc_free(int count);
bool test_virtual_memory(int count, int size);
bool test_multi_spawn(int spawns);

coreid_t my_core_id;
struct bootinfo *bi;


// see also book page 98
errval_t recv_handler(void* arg)
{
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref cap;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap);
    if (err_is_fail(err)) {
        if (lmp_err_is_transient(err)) {
            err = lmp_chan_recv(lmp, &msg, &cap);
        }
        else {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan recv, non transient error");
            return err;
        }
    }

    // register again for receiving
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan alloc recv slot failed");
        return err;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, arg));
    if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan register recv failed");
            return err;
    }

    // no message content received?
    if (msg.buf.msglen <= 0)
        return err;
    debug_printf("msg buflen %zu\n", msg.buf.msglen);
    debug_printf("msg−>words[0] = 0x%lx\n", msg.words[0]);
    void* answer;
    void* answer_args;
    if (msg.words[0] == AOS_RPC_ID_NUM) {
        answer_args = answer_number(&cap, &msg);
        answer = (void*) send_received;
    }
    else if (msg.words[0] == AOS_RPC_ID_INIT) {
        answer_args = answer_init(&cap);
        answer = (void*) send_received;
    }
    else {
        answer = NULL;
        answer_args = NULL;
    }
    struct lmp_chan* ret_chan = (struct lmp_chan*) answer_args;
    err = lmp_chan_register_send(ret_chan, get_default_waitset(), MKCLOSURE(answer, answer_args));
    if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan register send failed");
            return err;
    }
    return SYS_ERR_OK;
}

errval_t whois(struct capref cap, struct client **he_is) {
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c whois: debug identify cap failed");
        return err;
    }
    while (cur != NULL) {
        if (return_cap.u.endpoint.listener == cur->end.listener
                && return_cap.u.endpoint.epoffset == cur->end.epoffset) {
            *he_is = cur;
            break;
        }
        cur = cur->next;
    }
    return err;
}
void* answer_number(struct capref* cap, struct lmp_recv_msg* msg) {
    debug_printf("server received number: %u\n", (uint32_t) msg->words[1]);

    // create answer
    struct client* he_is = NULL;
    errval_t err = whois(*cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/main.c answer number: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

// sets up the client struct for new processes
void* answer_init(struct capref* cap) {
    struct client *potential = NULL;
    errval_t err = whois(*cap, &potential);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer init: could not identify client");
        return NULL;
    }
    if (potential != NULL) {
        return (void*) &(potential->lmp);
    }
    potential = (struct client*) malloc(sizeof(struct client));

    struct capability return_cap;
    debug_cap_identify(*cap, &return_cap);
    potential->end = return_cap.u.endpoint;
    potential->prev = NULL;
    if (client_list == NULL) {
        potential->next = NULL;
    }
    else {
        potential->next = client_list;
        client_list->prev = potential;
    }
    client_list = potential;

    err = lmp_chan_accept(&potential->lmp, DEFAULT_LMP_BUF_WORDS, *cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer init: could not do lmp chan accept");
        return NULL;
    }
    return (void*) &(potential->lmp);
}

// handler to send a signal that the message was received
errval_t send_received(void* arg) {
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, 1); // send a 1 to signal that the message arrived
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c send received: could not do lmp chan send1");
        return err;
    }
    return SYS_ERR_OK;
}

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

    err = initialize_ram_alloc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "initialize_ram_alloc");
        return EXIT_FAILURE;
    }

    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPoint, 0, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: cap retype of dispatcher to selfep failed");
        return EXIT_FAILURE;
    }
    struct lmp_chan* lmp = (struct lmp_chan*) malloc(sizeof(struct lmp_chan));
    err = lmp_chan_accept(lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }
    err = cap_copy(cap_initep, lmp->local_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp cap copy of lmp->local_cap to cap_initep failed");
        return EXIT_FAILURE;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    test_multi_spawn(1);

    debug_printf("Message handler loop\n");
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

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
        size = BASE_PAGE_SIZE << (i % 9);

        err = aos_ram_free(capabilities[i], size);
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
bool test_multi_spawn(int spawns) {
    errval_t err;

    for (int i = 0; i < spawns; i++) {
        struct spawninfo *si = malloc(sizeof(struct spawninfo));
        err = spawn_load_by_name("/armv7/sbin/hello", si);
        if (err_is_fail(err)) {
            debug_printf("Failed spawning process %i\n", i);
            return false;
        }
    }
    debug_printf("Spawning test succeeded\n");
    return true;
}
