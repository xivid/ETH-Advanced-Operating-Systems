/**
 * \file
 * \brief Implementation of AOS rpc-like messaging
 */

/*
 * Copyright (c) 2013 - 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos_rpc.h>

errval_t aos_rpc_send_handler_for_init (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send1(rpc->lmp, LMP_FLAG_SYNC, rpc->lmp->local_cap, AOS_RPC_ID_INIT);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("aos_rpc_send_handler_for_num: too many failed attempts\n");
    return err;
}

errval_t aos_rpc_send_handler_for_num (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    uintptr_t* num = (uintptr_t*) args[1];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send2(rpc->lmp, LMP_FLAG_SYNC, rpc->lmp->local_cap, AOS_RPC_ID_NUM, *num);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("aos_rpc_send_handler_for_num: too many failed attempts\n");
    return err;
}

errval_t aos_rpc_send_handler_for_char (void* v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    char character = (char) args[1];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send2(rpc->lmp, LMP_FLAG_SYNC, rpc->lmp->local_cap, AOS_RPC_ID_CHAR, character);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("aos_rpc_send_handler_for_num: too many failed attempts\n");
    return err;
}

errval_t aos_rpc_send_handler_for_string (void* v_args)
{
    errval_t err;
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];

    int len = strnlen((const char*) &args[1], 32) / 4;

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send(rpc->lmp, LMP_FLAG_SYNC, rpc->lmp->local_cap, len + 1, AOS_RPC_ID_STR, args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("aos_rpc_send_handler_for_num: too many failed attempts\n");
    return err;
}

// checks that the message was acknowledged with a "1" sent as answer
errval_t aos_rpc_rcv_handler_general (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(rpc->lmp, &lmp_msg, &cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(rpc->lmp, rpc->ws,
                MKCLOSURE((void*) aos_rpc_rcv_handler_general, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_rcv_handler_general: too many failed attempts or non transient error\n");
        return err;
    }
    // check that message was received
    if (lmp_msg.buf.msglen != 1 || !lmp_msg.words[0]) {
        debug_printf("aos_rpc_rcv_handler_general: received message not es expected");
        return FLOUNDER_ERR_RPC_MISMATCH;
    }
    return SYS_ERR_OK;

}

errval_t aos_rpc_send_handler_for_ram (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    size_t* size = (size_t*) args[1];
    size_t* align = (size_t*) args[2];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send3(rpc->lmp, LMP_FLAG_SYNC, rpc->lmp->local_cap, AOS_RPC_ID_RAM, *size, *align);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("aos_rpc_send_handler_for_ram: too many failed attempts\n");
    return err;
}

errval_t aos_rpc_rcv_handler_for_ram (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref* cap = (struct capref*) args[3];
    size_t* ret_size = (size_t*) args[4];

    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(rpc->lmp, &lmp_msg, cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(rpc->lmp, rpc->ws,
                MKCLOSURE((void*) aos_rpc_rcv_handler_for_ram, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_rcv_handler_for_ram: too many failed attempts or non transient error\n");
        return err;
    }
    // check that message was received
    if (lmp_msg.buf.msglen == 3 && lmp_msg.words[0]) {
        *ret_size = lmp_msg.words[1];
    }

    return (errval_t) lmp_msg.words[2];
}

errval_t aos_rpc_send_and_receive (void* send_handler, void* rcv_handler, uintptr_t* args) {

    struct aos_rpc* rpc = (struct aos_rpc*) args[0];

    // set handlers
    errval_t err = lmp_chan_register_send(rpc->lmp, rpc->ws, MKCLOSURE(send_handler, args));
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_and_receive: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(rpc->lmp, rpc->ws, MKCLOSURE(rcv_handler, args));
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_and_receive: lmp_chan_register_rcv failed\n");
        return err;
    }

    // wait for send and receive ready:
    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_and_receive: first event_dispatch failed\n");
        return err;
    }

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_and_receive: second event_dispatch failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_send_number(struct aos_rpc *chan, uintptr_t val)
{
    // implement functionality to send a number ofer the channel
    // given channel and wait until the ack gets returned.
    uintptr_t args[2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) &val;

    errval_t err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_num, aos_rpc_rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string)
{
    uintptr_t args[9];
    args[0] = (uintptr_t) chan;

    int str_size = strlen(string) + 1;

    for (int i = 0; i < str_size / 32; i++) {
        for (int j = 0; j < 8; j++) {
            args[j+1] = ((uintptr_t *) string)[9*i + j];
        }
        errval_t err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_string, aos_rpc_rcv_handler_general, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_send_string failed\n");
            return err;
        }
    }

    // send the remaining partial packet
    for (int j = 0; j < (str_size % (32))/ 4; j++) {
        args[j+1] = ((uintptr_t *) string)[9*(str_size/32 + 1) + j];
    }
    errval_t err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_string, aos_rpc_rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_string last failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t size, size_t align,
                             struct capref *retcap, size_t *ret_size)
{
    // arguments: [aos_rpc, size, align, retcap, ret_size]
    uintptr_t args[5];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) &size;
    args[2] = (uintptr_t) &align;
    args[3] = (uintptr_t) retcap;
    args[4] = (uintptr_t) ret_size;

    // setup receiver slot
    errval_t err = lmp_chan_alloc_recv_slot(chan->lmp);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: lmp chan alloc recv slot failed\n");
        return err;
    }
    err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_ram, aos_rpc_rcv_handler_for_ram, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: send and receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc)
{
    // TODO implement functionality to request a character from
    // the serial driver.
    return SYS_ERR_OK;
}


errval_t aos_rpc_serial_putchar(struct aos_rpc *chan, char c)
{
    errval_t err;
    uintptr_t args[2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) c;

    err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_char, aos_rpc_rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_char failed\n");
        return err;
    }    return SYS_ERR_OK;
}

errval_t aos_rpc_process_spawn(struct aos_rpc *chan, char *name,
                               coreid_t core, domainid_t *newpid)
{
    // TODO (milestone 5): implement spawn new process rpc
    return SYS_ERR_OK;
}

errval_t aos_rpc_process_get_name(struct aos_rpc *chan, domainid_t pid,
                                  char **name)
{
    struct domaininfo *current = chan->head;
    while (current != NULL) {
        if (current->pid == pid) {
            *name = current->domain_name;
            return SYS_ERR_OK;
        }
        current = current->next;
    }
    debug_printf("Error: Didn't find process!\n");
    return SYS_ERR_OK;
}

errval_t aos_rpc_process_get_all_pids(struct aos_rpc *chan,
                                      domainid_t **pids, size_t *pid_count)
{
    int count = 0;
    struct domaininfo *current = chan->head;
    while (current != NULL) {
        count++;
        current = current->next;
    }

    *pid_count = count;
    *pids = malloc(count * sizeof(domainid_t));
    current = chan->head;
    int i = 0;
    while (current != NULL) {
        *pids[i++] = current->pid;
        current = current->next;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_get_device_cap(struct aos_rpc *rpc,
                                lpaddr_t paddr, size_t bytes,
                                struct capref *frame)
{
    return LIB_ERR_NOT_IMPLEMENTED;
}

errval_t aos_rpc_init(struct waitset* ws, struct aos_rpc *rpc)
{
    rpc->ws = ws;

    // channel to the init endpoint
    rpc->lmp = (struct lmp_chan*) malloc(sizeof(struct lmp_chan));

    errval_t err = lmp_chan_accept(rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap_initep);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_init: lmp_chan_accept failed\n");
        return err;
    }

    uintptr_t args = (uintptr_t) rpc;
    err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_init, aos_rpc_rcv_handler_general, &args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_init: aos_rpc_send_and_receive failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void)
{
    return get_init_rpc();
}

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void)
{
    return get_init_rpc();
}

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void)
{
    return get_init_rpc();
}

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void)
{
    return get_init_rpc();
}
