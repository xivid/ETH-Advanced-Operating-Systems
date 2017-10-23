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

errval_t aos_rpc_rcv_handler_for_num (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(rpc->lmp, &lmp_msg, &cap);
    
    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(rpc->lmp, &lmp_msg, &cap);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_rcv_handler_for_num: too many failed attempts or non transient error\n");
        return err;
    }
    if (lmp_msg.buf.msglen != 1) {
        debug_printf("aos_rpc_rcv_handler_for_num: received message not es expected");
        return FLOUNDER_ERR_RPC_MISMATCH;
    }
    return SYS_ERR_OK;
    
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
    // TODO: implement functionality to send a number ofer the channel
    // given channel and wait until the ack gets returned.
    uintptr_t args[2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) &val;
    
    errval_t err = aos_rpc_send_and_receive(aos_rpc_send_handler_for_num, aos_rpc_rcv_handler_for_num, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string)
{
    // TODO: implement functionality to send a string over the given channel
    // and wait for a response.
    return SYS_ERR_OK;
}

errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t size, size_t align,
                             struct capref *retcap, size_t *ret_size)
{
    // TODO: implement functionality to request a RAM capability over the
    // given channel and wait until it is delivered.
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
    // TODO implement functionality to send a character to the
    // serial port.
    return SYS_ERR_OK;
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
    // TODO (milestone 5): implement name lookup for process given a process
    // id
    return SYS_ERR_OK;
}

errval_t aos_rpc_process_get_all_pids(struct aos_rpc *chan,
                                      domainid_t **pids, size_t *pid_count)
{
    // TODO (milestone 5): implement process id discovery
    return SYS_ERR_OK;
}

errval_t aos_rpc_get_device_cap(struct aos_rpc *rpc,
                                lpaddr_t paddr, size_t bytes,
                                struct capref *frame)
{
    return LIB_ERR_NOT_IMPLEMENTED;
}

errval_t aos_rpc_init(struct aos_rpc *rpc)
{
    // TODO: Initialize given rpc channel
    return SYS_ERR_OK;
}

/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void)
{
    return NULL;
}

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void)
{
    return NULL;
}

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void)
{
    return NULL;
}

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void)
{
    return NULL;
}