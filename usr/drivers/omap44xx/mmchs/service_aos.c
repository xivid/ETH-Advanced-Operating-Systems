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

#include "mmchs.h"
#define        RPC_SLOT 0
#define       TYPE_SLOT 1
#define     ERR_ID_SLOT 2
#define    MAX_LMP_ARGS 11

errval_t ns_recv_listener_handler(void *v_args);
errval_t ns_send_handler(void *v_args);
errval_t ns_init_rpc(void);
errval_t send(void *v_args, void *send_handler, void *recv_handler);
errval_t ns_recv_ack_handler(void *v_args);

struct aos_rpc *listen_rpc = NULL;
errval_t ns_init_rpc(void)
{
    errval_t err;
    // Set up the listening channel.
    listen_rpc = malloc(sizeof(struct aos_rpc));
    err = lmp_chan_accept(&listen_rpc->lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
            MKCLOSURE((void*) ns_recv_listener_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    /*
    // Send a registration message to init.
    err = ns_send_register();
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return 1;
    }

    // Send the endpoint to the listening channel to init.
    err = ns_send_listen_cap();
    if (err_is_fail(err)) {
        debug_printf("send nameserver endpoint failed\n");
        return 1;
    }
    */

    debug_printf("nameserver done setting rpc\n");
    return SYS_ERR_OK;
}

errval_t ns_recv_listener_handler(void *v_args)
{
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    struct lmp_chan *lmp = &listen_rpc->lmp;
    errval_t err = lmp_chan_recv(lmp, &lmp_msg, &cap);
    int count = 0;
    while (count < AOS_RPC_ATTEMPTS &&
            lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(lmp, &lmp_msg, &cap);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("ns_recv_ack_handler: too many failed attempts\n");
        return err;
    }


    if (lmp_msg.words[0] == AOS_RPC_ID_MMCHS) {
        uintptr_t* buf = malloc(512);
        size_t block_nr = lmp_msg.words[1];
        uintptr_t piece[MAX_LMP_ARGS+1];
        struct aos_rpc *reply_rpc = listen_rpc;
        size_t counter = 0;
        while (counter < (512/28)) {
            piece[0] = (uintptr_t) reply_rpc;
            piece[1] = (uintptr_t) AOS_RPC_ID_MMCHS;
            piece[2] = (uintptr_t) mmchs_read_block(block_nr, (void*)&buf[0]);
            piece[3] = (uintptr_t)&buf[counter];
            err = send(piece, ns_send_handler, ns_recv_ack_handler);
            if (err_is_fail(err)) {
                debug_printf("ns_recv_listener_handler: can't register send\n");
                return err;
            }
            counter+=7;
        }
        free(buf);
        free(piece);
    }
    else {
        debug_printf("unknown message sent to nameserver: %d\n",
                lmp_msg.words[0]);
        return LIB_ERR_NOT_IMPLEMENTED;
    }

    // reregister receiver
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        debug_printf("ns_recv_listener_handler: alloc slot failed");
        return err;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(),
            MKCLOSURE((void*) ns_recv_listener_handler, v_args));
    if (err_is_fail(err)) {
        debug_printf("ns_recv_listener_handler: reregister recv failed");
        return err;
    }

    return SYS_ERR_OK;
}
errval_t send(void *v_args, void *send_handler, void *recv_handler)
{
    uintptr_t *args = (uintptr_t *) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[RPC_SLOT];
    errval_t err = lmp_chan_register_send(&rpc->lmp, rpc->ws,
            MKCLOSURE(send_handler, v_args));
    if (err_is_fail(err)) {
        debug_printf("send: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
            MKCLOSURE(recv_handler, v_args));
    if (err_is_fail(err)) {
        debug_printf("send: lmp_chan_register_rcv failed\n");
        return err;
    }

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send: first event_dispatch failed\n");
        return err;
    }

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send: second event_dispatch failed\n");
        return err;
    }

    return SYS_ERR_OK;
}
errval_t ns_recv_ack_handler(void *v_args)
{
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[RPC_SLOT];
    struct lmp_chan *lmp = &rpc->lmp;
    errval_t err = lmp_chan_recv(lmp, &lmp_msg, NULL);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS &&
            lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(lmp, &lmp_msg, NULL);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("ns_recv_ack_handler: too many failed attempts\n");
        return err;
    }

    if (lmp_msg.words[0] != AOS_RPC_ID_ACK) {
        debug_printf("ns_recv_ack_handler: expected answer to be ACK\n");
        return FLOUNDER_ERR_RPC_MISMATCH;
    }
    return SYS_ERR_OK;
}


errval_t ns_send_handler(void *v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[RPC_SLOT];
    struct lmp_chan *lmp = &rpc->lmp;

    errval_t err;
    size_t count = 0;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, args[1], args[2], ((uintptr_t*)args[2])[0], ((uintptr_t*)args[2])[1], ((uintptr_t*)args[2])[2], ((uintptr_t*)args[2])[3], ((uintptr_t*)args[2])[4], ((uintptr_t*)args[2])[5], ((uintptr_t*)args[2])[6]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        debug_printf("got an error: %s\n", err_getstring(err));
        count++;
    }
    debug_printf("ns_send_handler: too many failed attempts\n");
    return err;
}



void init_service(void)
{

    errval_t err;
    debug_printf("mmchs launched\n");
    err = ns_init_rpc();
    if (err_is_fail(err)) {
        debug_printf("mmchs failed init_rpc\n");
        return;
    }

    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            debug_printf("error in event dispatch in nameserver\n");
            return;
        }
    }
}
