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
#include <bitmacros.h>

errval_t send_handler (void *v_args);

errval_t receive_and_match_ack(uintptr_t *args, struct lmp_recv_msg *lmp_msg, struct capref *cap, void *rcv_handler);
errval_t rcv_handler_general (void *v_args);
errval_t rcv_handler_network_register(void *v_args);
errval_t rcv_handler_for_ram (void *v_args);
errval_t rcv_handler_for_process (void *v_args);
errval_t rcv_handler_for_get_process_name (void *v_args);
errval_t rcv_handler_for_get_pids (void *v_args);
errval_t rcv_handler_for_ns(void *v_args);
errval_t rcv_handler_for_char(void *v_args);
errval_t rcv_handler_for_device_cap(void *v_args);
errval_t rcv_handler_for_ns_syn(void *v_args);
errval_t rcv_handler_for_ns_register(void *v_args);
errval_t rcv_handler_for_ns_lookup(void *v_args);
errval_t rcv_handler_for_net(void *v_args);
errval_t rcv_handler_for_ns_enum(void *v_args);

errval_t send_and_receive (void* rcv_handler, uintptr_t* args);

errval_t send_handler_with_cap(void  *v_args);
errval_t send_and_receive_with_cap(void *rcv_handler, uintptr_t *args);

static int send_retry_count = 0;
errval_t send_handler (void* v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc*) args[0];
    struct lmp_chan *lmp = &rpc->lmp;
    errval_t err;

    /*char buf[256];
    debug_print_capref(buf, 256, lmp->local_cap);
    debug_printf("send_handler: from %s\n", buf)*/

    err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, lmp->local_cap, args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    if (err_is_ok(err)) {
        return err;
    } else {
        DEBUG_ERR(err, "send_handler first send fails");
    }

    if (send_retry_count++ < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_register_send(&rpc->lmp, rpc->ws, MKCLOSURE((void *)send_handler, args));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "send_handler: lmp_chan_register_send failed\n");
            return err;
        }
        err = event_dispatch(rpc->ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "send_handler: first event_dispatch failed\n");
            return err;
        }
    } else {
        DEBUG_ERR(err, "send_handler: too many failed attempts\n");
        return err;
    }

    return err;
}

errval_t send_handler_with_cap(void  *v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc*) args[0];
    struct lmp_chan *lmp = &rpc->lmp;
    struct capref cap = *((struct capref *) args[LMP_ARGS_SIZE]);
    errval_t err;
    err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, cap, args[1], args[2],
                         args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    if (err_is_ok(err)) {
        return err;
    }

    if (send_retry_count++ < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, cap, args[1], args[2],
                             args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "send_handler_with_cap: lmp_chan_register_send failed\n");
            return err;
        }
        err = event_dispatch(rpc->ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "send_handler_with_cap: first event_dispatch failed\n");
            return err;
        }
    } else {
        DEBUG_ERR(err, "send_handler_with_cap: too many failed attempts\n");
        return err;
    }

    return err;
}

// checks that the message was acknowledged with an ACK
errval_t rcv_handler_general (void* v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    return receive_and_match_ack(args, &lmp_msg, &cap, (void *) rcv_handler_general);
}

// checks that the message was acknowledged with an ACK, and reregister
errval_t rcv_handler_network_register(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err;

    err = receive_and_match_ack(args, &lmp_msg, &cap, (void *) rcv_handler_network_register);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "receive_and_match_ack fails");
    }

    struct aos_rpc* rpc = (struct aos_rpc*) args[0];

    // register again for receiving
    err = lmp_chan_alloc_recv_slot(&rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "rcv_handler_network_register: lmp chan alloc recv slot failed");
        return err;
    }
    /*err = lmp_chan_register_recv(&rpc->lmp, get_default_waitset(), MKCLOSURE((void*)rcv_handler_network_register, args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "rcv_handler_network_register: lmp chan register recv failed");
        return err;
    }*/

    return err;
}


errval_t receive_and_match_ack(uintptr_t *args, struct lmp_recv_msg *lmp_msg, struct capref *cap, void *rcv_handler)
{
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    errval_t err = lmp_chan_recv(&rpc->lmp, lmp_msg, cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(&rpc->lmp, lmp_msg, cap);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("registering receive handler: too many failed attempts\n");
        return err;
    }

    if (lmp_msg->words[0] != AOS_RPC_ID_ACK) {
        debug_printf("Expected answer to be of type ACK, got %i\n", lmp_msg->words[0]);
        return FLOUNDER_ERR_RPC_MISMATCH;
    }

    return SYS_ERR_OK;
}

errval_t send_and_receive (void* rcv_handler, uintptr_t* args)
{
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    send_retry_count = 0;

    // set handlers
    errval_t err = lmp_chan_register_send(&rpc->lmp, rpc->ws, MKCLOSURE((void *)send_handler, args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "send_and_receive: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(&rpc->lmp, rpc->ws, MKCLOSURE(rcv_handler, args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "send_and_receive: lmp_chan_register_rcv failed\n");
        return err;
    }

    // debug_printf("send and receive: both registered\n");

    // wait for send and receive ready:
    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "send_and_receive: first event_dispatch failed\n");
        return err;
    }
    // debug_printf("send and receive: send done\n");

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send_and_receive: second event_dispatch failed\n");
        return err;
    }
    // debug_printf("send and receive: receive done\n");

    return SYS_ERR_OK;
}

errval_t send_and_receive_with_cap(void *rcv_handler, uintptr_t *args)
{
    struct aos_rpc *rpc = (struct aos_rpc*) args[0];
    send_retry_count = 0;

    errval_t err = lmp_chan_register_send(&rpc->lmp, rpc->ws,
            MKCLOSURE((void *)send_handler_with_cap, args));
    if (err_is_fail(err)) {
        debug_printf("send_and_receive_with_cap: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
            MKCLOSURE(rcv_handler, args));
    if (err_is_fail(err)) {
        debug_printf("send_and_receive_with_cap: lmp_chan_register_rcv failed\n");
        return err;
    }

    // wait for send and receive ready:
    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send_and_receive_with_cap: first event_dispatch failed\n");
        return err;
    }

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send_and_receive_with_cap: second event_dispatch failed\n");
        return err;
    }

    return SYS_ERR_OK;
}


/* RPC calls visible to users */

errval_t aos_rpc_send_number(struct aos_rpc *chan, uintptr_t val)
{
    // implement functionality to send a number over the channel
    // given channel and wait until the ack gets returned.
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_NUM;
    args[2] = (uintptr_t) &val;

    errval_t err = send_and_receive(rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string)
{
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_STR;

    int str_size = strlen(string) + 1;

    for (int i = 0; i < str_size / 32; i++) {
        for (int j = 0; j < 8; j++) {
            args[j+2] = ((uintptr_t *) string)[8*i + j];
        }
        errval_t err = send_and_receive(rcv_handler_general, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_send_string failed\n");
            return err;
        }
    }

    // send the remaining partial packet
    if (str_size % 32 != 0) {
        for (int j = 0; j < (str_size % 32)/ 4 + ((str_size % 32) % 4 != 0); j++) {
            args[j+2] = ((uintptr_t *) string)[8*(str_size/32) + j];
        }
        errval_t err = send_and_receive(rcv_handler_general, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_send_string last failed\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t size, size_t align,
                             struct capref *retcap, size_t *ret_size)
{
    // arguments: [aos_rpc, size, align, retcap, ret_size]
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_RAM;
    args[2] = (uintptr_t) size;
    args[3] = (uintptr_t) align;
    args[LMP_ARGS_SIZE] = (uintptr_t) retcap;
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) ret_size;

    assert(chan != NULL);

    // setup receiver slot
    errval_t err = lmp_chan_alloc_recv_slot(&chan->lmp);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: lmp chan alloc recv slot failed\n");
        return err;
    }
    err = send_and_receive(rcv_handler_for_ram, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: send and receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ram (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref* cap = (struct capref*) args[LMP_ARGS_SIZE];
    size_t *ret_size = (size_t*) args[LMP_ARGS_SIZE + 1];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, cap, (void *) rcv_handler_for_ram);
    if (err_is_fail(err)) {
        *ret_size = 0;
    } else {
        *ret_size = lmp_msg.words[1];
    }

    return (errval_t) lmp_msg.words[2];
}

errval_t aos_rpc_get_nameserver_ep(struct aos_rpc *init_chan,
        struct capref *retcap, ns_err_names_t *ns_err)
{
    // arguments: [aos_rpc, msg_id, retcap, ns_err]
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) init_chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_NAMESERVER_EP;
    args[LMP_ARGS_SIZE] = (uintptr_t) retcap;
    args[LMP_ARGS_SIZE + 1]= (uintptr_t) ns_err;

    assert(init_chan != NULL);

    // setup receiver slot
    errval_t err = lmp_chan_alloc_recv_slot(&init_chan->lmp);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: lmp chan alloc recv slot failed\n");
        return err;
    }
    err = send_and_receive(rcv_handler_for_ns, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_ram_cap: send and receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ns(void *v_args) {
    uintptr_t *args = (uintptr_t*) v_args;
    struct capref *retcap = (struct capref*) args[LMP_ARGS_SIZE];
    ns_err_names_t *ns_err = (ns_err_names_t *) args[LMP_ARGS_SIZE + 1];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err =  receive_and_match_ack(args, &lmp_msg, retcap,
            (void*) rcv_handler_for_ns);
    if (err_is_fail(err)) {
        return err;
    }

    *ns_err = lmp_msg.words[1];
    return SYS_ERR_OK;
}

errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc)
{
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_CHAR;
    args[2] = (uintptr_t) INIT_PROCESS_ID;

    args[LMP_ARGS_SIZE] = (uintptr_t) retc;

    err = send_and_receive(rcv_handler_for_char, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_rcv_char failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_char(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void *)rcv_handler_for_char);
    if (err_is_fail(err)) {
        return err;
    }

    *(char*)(args[LMP_ARGS_SIZE]) = lmp_msg.words[1];

    return SYS_ERR_OK;
}

errval_t aos_rpc_serial_putchar(struct aos_rpc *chan, char c)
{
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_CHAR;
    args[2] = (uintptr_t) c;

    err = send_and_receive(rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_char failed\n");
        return err;
    }
    return SYS_ERR_OK;
}


errval_t aos_rpc_process_spawn(struct aos_rpc *chan, char *name,
                               coreid_t core, domainid_t *newpid)
{
    return aos_rpc_process_spawn_with_arguments(chan, name, core, newpid, NULL);
}

errval_t aos_rpc_process_spawn_with_arguments(struct aos_rpc *chan,
                                              char *name,
                                              coreid_t core,
                                              domainid_t *newpid,
                                              const char *arguments)
{
    assert(name != NULL);
    // TODO: we will put the length of process name into the least significant 3 bytes of args[2]
    // assert(strlen(name) < (1 << 24));

    size_t name_len = strlen(name);
    size_t arg_len = (arguments == NULL) ? 0 : strlen(arguments);

    uintptr_t args[LMP_ARGS_SIZE + 1];
    // order: 0-chan, 1-type identifier, 2-core, 3-name_len, 4-arg_len, 5..8-the name
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_PROCESS;
    args[2] = (uintptr_t) core;
    args[3] = (uintptr_t) name_len;
    args[4] = (uintptr_t) arg_len;
    // additional information, passed to rcv_handler but not sent to server
    args[LMP_ARGS_SIZE] = (uintptr_t) newpid;

    int str_size = strlen(name) + 1;
    int blocks = str_size / 4 + (str_size % 4 != 0);
    for (int j = 0; j < blocks; j++) {
        args[j+4] = ((uintptr_t *) name)[j];
    }
    // TODO: define a struct rpc_send_msg { aos_rpc* rpc, size_t length, uint32_t* msg },
    // and pass a instance to send/rcv_handlers
    errval_t err = send_and_receive(rcv_handler_for_process, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_process_spawn failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_process(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    domainid_t* new_pid = (domainid_t*)args[LMP_ARGS_SIZE];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void*) rcv_handler_for_process);
    if (err_is_fail(err)) {
        *new_pid = -1;
    } else {
        *new_pid = (domainid_t) lmp_msg.words[1];
    }

    return (errval_t) lmp_msg.words[2];
}

errval_t aos_rpc_process_get_name(struct aos_rpc *chan, domainid_t pid,
                                  char **name)
{
    return aos_rpc_process_get_name_on_core(chan, pid, name, disp_get_core_id() << 24);
}

errval_t aos_rpc_process_get_name_on_core(struct aos_rpc *chan, domainid_t pid,
                                          char **name, domainid_t dest)
{
    uintptr_t args[LMP_ARGS_SIZE + 3];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_PNAME;
    args[2] = (uintptr_t) pid;
    args[3] = (uintptr_t) dest;

    args[LMP_ARGS_SIZE] = (uintptr_t) 0; // remaining bytes
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) 0; // received bytes
    args[LMP_ARGS_SIZE + 2] = (uintptr_t) name;

    errval_t err = send_and_receive(rcv_handler_for_get_process_name, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_process_get_name failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_get_process_name (void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void *)rcv_handler_for_get_process_name);
    if (err_is_fail(err)) {
        return err;
    }

    bool first_message = args[LMP_ARGS_SIZE + 1] == 0;
    char ** name = (char **) args[LMP_ARGS_SIZE + 2];

    if (first_message) {
        uint32_t str_size = lmp_msg.words[1];
        args[LMP_ARGS_SIZE] = str_size;
        *name = malloc((str_size + 1) *sizeof(char));
        if (*name == NULL) {
            debug_printf("Malloc failed to allocate a string of the required size\n");
            return LIB_ERR_MALLOC_FAIL;
        }
    }

    char *copy_start = *name + args[LMP_ARGS_SIZE + 1];
    int string_off = first_message ? 2 : 1;
    int n_bytes_to_copy = MIN((9 - string_off) * 4, args[LMP_ARGS_SIZE]);
    strncpy(copy_start, (char *)(lmp_msg.words + string_off), n_bytes_to_copy);
    copy_start[n_bytes_to_copy] = 0;
    args[LMP_ARGS_SIZE] -= n_bytes_to_copy;
    args[LMP_ARGS_SIZE + 1] += n_bytes_to_copy;

    if (args[LMP_ARGS_SIZE] != 0) {
        struct aos_rpc* rpc = (struct aos_rpc*) args[0];
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler_for_get_process_name, args));
        if (err_is_fail(err)) {
            debug_printf("can't set itself for the receive handler for the remaining bytes\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}


errval_t aos_rpc_process_get_all_pids(struct aos_rpc *chan,
                                           domainid_t **pids, size_t *pid_count)
{
    return aos_rpc_process_get_all_pids_on_core(chan, pids, pid_count, disp_get_core_id() << 24);
}


errval_t aos_rpc_process_get_all_pids_on_core(struct aos_rpc *chan,
                                           domainid_t **pids, size_t *pid_count,
                                           domainid_t dest)
{
    uintptr_t args[LMP_ARGS_SIZE + 4];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_PIDS;
    args[2] = (uintptr_t) dest;

    args[LMP_ARGS_SIZE] = (uintptr_t) 0; // received pids
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) 0; // remaining pids
    args[LMP_ARGS_SIZE + 2] = (uintptr_t) pid_count;
    args[LMP_ARGS_SIZE + 3] = (uintptr_t) pids;

    errval_t err = send_and_receive(rcv_handler_for_get_pids, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_process_get_name failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_get_pids (void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void *) rcv_handler_for_get_pids);
    if (err_is_fail(err)) {
        return err;
    }

    bool first_message = args[LMP_ARGS_SIZE + 1] == 0;
    domainid_t ** pids = (domainid_t **) args[LMP_ARGS_SIZE + 3];

    if (first_message) {
        uint32_t n_pids = lmp_msg.words[1];
        args[LMP_ARGS_SIZE] = n_pids;
        size_t *pid_count = (size_t *) args[LMP_ARGS_SIZE + 2];
        *pid_count = n_pids;
        *pids = malloc(n_pids *sizeof(domainid_t));
        if (*pids == NULL) {
            debug_printf("Malloc failed to allocate a string of the required size\n");
            return LIB_ERR_MALLOC_FAIL;
        }
    }

    uint32_t *copy_start = *pids + args[LMP_ARGS_SIZE + 1];
    int string_off = first_message ? 2 : 1;
    int n_pids_to_copy = MIN(9 - string_off, args[LMP_ARGS_SIZE]);
    for (int i = 0; i < n_pids_to_copy; i++) {
        copy_start[i] = lmp_msg.words[string_off + i];
    }
    args[LMP_ARGS_SIZE] -= n_pids_to_copy;
    args[LMP_ARGS_SIZE + 1] += n_pids_to_copy;

    if (args[LMP_ARGS_SIZE] != 0) {
        struct aos_rpc* rpc = (struct aos_rpc*) args[0];
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler_general, args));
        if (err_is_fail(err)) {
            debug_printf("can't set itself for the receive handler for the remaining bytes\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_get_device_cap(struct aos_rpc *rpc,
                                lpaddr_t paddr, size_t bytes,
                                struct capref *frame)
{
    // arguments: [aos_rpc, bytes, paddr, retcap]
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_DEVICE_CAP;
    args[2] = (uintptr_t) bytes;
    args[3] = (uintptr_t) paddr;
    args[LMP_ARGS_SIZE] = (uintptr_t) frame;

    assert(rpc != NULL);

    // setup receiver slot
    errval_t err = lmp_chan_alloc_recv_slot(&rpc->lmp);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_device_cap: lmp chan alloc recv slot failed\n");
        return err;
    }

    err = send_and_receive(rcv_handler_for_device_cap, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_get_device_cap: send and receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}


errval_t rcv_handler_for_device_cap (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref* cap = (struct capref*) args[LMP_ARGS_SIZE];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, cap, (void *) rcv_handler_for_device_cap);
    if (err_is_fail(err)) {
        return err;
        *cap = NULL_CAP;
    }

    return (errval_t) lmp_msg.words[1];
}

errval_t aos_rpc_init(struct waitset* ws, struct aos_rpc *rpc)
{
    rpc->ws = ws;

    errval_t err = lmp_chan_accept(&rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap_initep);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_init: lmp_chan_accept failed\n");
        return err;
    }
    thread_mutex_init(&rpc->rpc_mutex);

    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) rpc;
    args[1] = AOS_RPC_ID_INIT;
    err = send_and_receive(rcv_handler_general, args);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_init: send_and_receive failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_nameserver_syn(struct aos_rpc *rpc, struct capref cap,
        unsigned *id)
{
    rpc->ws = get_default_waitset();

    errval_t err = lmp_chan_accept(&rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_nameserver_syn: lmp_chan_accept failed\n");
        return err;
    }
    thread_mutex_init(&rpc->rpc_mutex);

    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) rpc;
    args[1] = AOS_RPC_ID_NAMESERVER_SYN;
    args[LMP_ARGS_SIZE] = (uintptr_t) id;

    err = send_and_receive(rcv_handler_for_ns_syn, args);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_nameserver_syn: send_and_receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}


errval_t rcv_handler_for_ns_syn(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    size_t *id = (size_t*) args[LMP_ARGS_SIZE];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    struct capref cap;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap,
            (void *) rcv_handler_for_ns_syn);
    if (err_is_fail(err)) {
        return err;
    }
    *id = lmp_msg.words[1];

    return SYS_ERR_OK;
}

errval_t aos_rpc_fs_init(struct aos_rpc *rpc, struct capref cap)
{
    rpc->ws = get_default_waitset();

    errval_t err = lmp_chan_accept(&rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_init: lmp_chan_accept failed\n");
        return err;
    }
    thread_mutex_init(&rpc->rpc_mutex);

    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) rpc;
    args[1] = AOS_RPC_ID_INIT;
    err = send_and_receive(rcv_handler_general, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_init: send_and_receive failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_nameserver_register(struct aos_rpc *rpc, unsigned id,
        struct capref endpoint, char *name, ns_err_names_t *ns_err)
{
    unsigned name_len = strlen(name), cur = 0;
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_REGISTER_EP_WITH_NAMESERVER;
    args[2] = (uintptr_t) id;
    args[3] = (uintptr_t) name_len;
    args[LMP_ARGS_SIZE] = (uintptr_t) &endpoint;
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) ns_err;

    bool first_msg = true;
    unsigned start_slot = 4;
    unsigned bytes_free = (LMP_ARGS_SIZE - start_slot) * sizeof(uintptr_t);

    // send block, wait for an ack, send next block, ...
    while (cur < name_len) {
        strncpy((char *) &args[start_slot], &name[cur], bytes_free);

        errval_t err = send_and_receive_with_cap(rcv_handler_for_ns_register,
                args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_nameserver_register: send_and_receive failed\n");
            return err;
        }

        cur += bytes_free;
        if (first_msg) {
            first_msg = false;
            start_slot -= 1;
            bytes_free += sizeof(uintptr_t);
        }
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ns_register(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    ns_err_names_t *ns_err = (ns_err_names_t *) args[LMP_ARGS_SIZE + 1];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    struct capref cap;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap,
            (void *) rcv_handler_for_ns_register);
    if (err_is_fail(err)) {
        return err;
    }
    *ns_err = lmp_msg.words[1];

    return SYS_ERR_OK;
}

errval_t aos_rpc_nameserver_deregister(struct aos_rpc *rpc, unsigned id,
        char *name, ns_err_names_t *ns_err)
{
    unsigned name_len = strlen(name), cur = 0;
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_DEREGISTER_EP_WITH_NAMESERVER;
    args[2] = (uintptr_t) id;
    args[3] = (uintptr_t) name_len;
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) ns_err;

    bool first_msg = true;
    unsigned start_slot = 4;
    unsigned bytes_free = (LMP_ARGS_SIZE - start_slot) * sizeof(uintptr_t);

    // send block, wait for an ack, send next block, ...
    while (cur < name_len) {
        strncpy((char *) &args[start_slot], &name[cur], bytes_free);

        errval_t err = send_and_receive(rcv_handler_for_ns_register,
                args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_nameserver_deregister: send_and_receive failed\n");
            return err;
        }

        cur += bytes_free;
        if (first_msg) {
            first_msg = false;
            start_slot -= 1;
            bytes_free += sizeof(uintptr_t);
        }
    }
    return SYS_ERR_OK;
}

// TODO: refactor to avoid code duplication with aos_rpc_nameserver_register
errval_t aos_rpc_nameserver_lookup(struct aos_rpc *rpc, unsigned id,
        struct capref *endpoint, char *name, ns_err_names_t *ns_err)
{
    unsigned name_len = strlen(name), cur = 0;
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_LOOKUP_EP_WITH_NAMESERVER;
    args[2] = (uintptr_t) id;
    args[3] = (uintptr_t) name_len;
    args[LMP_ARGS_SIZE] = (uintptr_t) endpoint;
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) ns_err;

    bool first_msg = true;
    unsigned start_slot = 4;
    unsigned bytes_free = (LMP_ARGS_SIZE - start_slot) * sizeof(uintptr_t);

    // send block, wait for an ack, send next block, ...
    while (cur < name_len) {
        strncpy((char *) &args[start_slot], &name[cur], bytes_free);

        errval_t err = lmp_chan_alloc_recv_slot(&rpc->lmp);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_nameserver_lookup: lmp chan alloc recv slot failed\n");
            return err;
        }
        err = send_and_receive(rcv_handler_for_ns_lookup, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_nameserver_lookup: send_and_receive failed\n");
            return err;
        }

        cur += bytes_free;
        if (first_msg) {
            first_msg = false;
            start_slot -= 1;
            bytes_free += sizeof(uintptr_t);
        }
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ns_lookup(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref *cap = (struct capref *) args[LMP_ARGS_SIZE];
    ns_err_names_t *ns_err = (ns_err_names_t *) args[LMP_ARGS_SIZE + 1];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    // On the last invocation of the this handler, the lookuped endpoint
    // will be written in the cap.
    // Note: this is only valid if the return code is NS_ERR_OK.
    errval_t err = receive_and_match_ack(args, &lmp_msg, cap,
            (void *) rcv_handler_for_ns_lookup);
    if (err_is_fail(err)) {
        return err;
    }
    *ns_err = lmp_msg.words[1];

    return SYS_ERR_OK;
}


errval_t aos_rpc_nameserver_enum(struct aos_rpc *rpc, unsigned id,
        char **str, ns_err_names_t *ns_err)
{
    uintptr_t args[LMP_ARGS_SIZE + 4];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_NAMESERVER_ENUM;
    args[2] = (uintptr_t) id;
    args[LMP_ARGS_SIZE] = (uintptr_t) str;
    args[LMP_ARGS_SIZE + 1] = (uintptr_t) ns_err;
    args[LMP_ARGS_SIZE + 2] = 0; // number of bytes written so far
    args[LMP_ARGS_SIZE + 3] = 1; // length of incoming string
    // assigned one for convenience for the while loop below

    uintptr_t *bytes_written = &args[LMP_ARGS_SIZE + 2];
    uintptr_t *bytes_total = &args[LMP_ARGS_SIZE + 3];

    while (*bytes_written < *bytes_total) {
        errval_t err = send_and_receive(rcv_handler_for_ns_enum, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_nameserver_lookup: send_and_receive failed\n");
            return err;
        }
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ns_enum(void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    char **str = (char **) args[LMP_ARGS_SIZE];
    ns_err_names_t *ns_err = (ns_err_names_t *) args[LMP_ARGS_SIZE + 1];
    uintptr_t *bytes_written = &args[LMP_ARGS_SIZE + 2];
    uintptr_t *bytes_total = &args[LMP_ARGS_SIZE + 3];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    char *cur_chunk = NULL, *buffer;
    int bytes_to_read;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap,
            (void *) rcv_handler_for_ns_lookup);
    if (err_is_fail(err)) {
        return err;
    }
    if (*bytes_written == 0) {
        // this is the first message
        *bytes_total = lmp_msg.words[2];
        *str = malloc(*bytes_total);
        cur_chunk = (char *) &lmp_msg.words[3];
        bytes_to_read = 6 * sizeof(uintptr_t);
    } else {
        cur_chunk = (char *) &lmp_msg.words[2];
        bytes_to_read = 7 * sizeof(uintptr_t);
    }
    buffer = *str;

    strncpy(&buffer[*bytes_written], cur_chunk, bytes_to_read);
    *bytes_written += bytes_to_read;
    *ns_err = lmp_msg.words[1];

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


// mount mount fat32 drive is mounted to path, given also the uri of the mount command
errval_t aos_rpc_fat_mount(struct aos_rpc* chan, const char* path, const char* uri) {
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_FS_MOUNT;
    size_t l1 = strlen(path);
    size_t l2 = strlen(uri);
    args[2] = (uintptr_t) l1;
    args[3] = (uintptr_t) l2;
    size_t p1 = 0;
    size_t p2 = 0;
    size_t sent = 4;
    while(sent < 9 && p1 < l1) {
        strncpy((char*)&args[sent], &path[p1], 4);
        p1+=4;
        sent++;
        if (sent==9) {
            sent = 0;
            err = send_and_receive(rcv_handler_for_ns_register, args);
            if (err_is_fail(err)) {
                debug_printf("aos_rpc_send_number failed\n");
                return err;
            }
        }
    }
    while(sent < 9 && p2 < l2) {
        strncpy((char*)&args[sent], &path[p2], 4);
        p2+=4;
        sent++;
        if (sent==9) {
            sent = 0;
            err = send_and_receive(rcv_handler_for_ns_register, args);
            if (err_is_fail(err)) {
                debug_printf("aos_rpc_send_number failed\n");
                return err;
            }
        }
    }
    if (sent!=0) {
        err = send_and_receive(rcv_handler_for_ns_register, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_send_number failed\n");
            return err;
        }
    }
    return (errval_t)args[LMP_ARGS_SIZE + 1];
}
// unmounts the fat32 drive (use with care)
errval_t aos_rpc_fat_unmount(struct aos_rpc* chan){
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_FS_UNMOUNT;

    errval_t err = send_and_receive(rcv_handler_for_ns_register, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    return (errval_t)args[LMP_ARGS_SIZE + 1];
}



errval_t rcv_handler_for_handle (void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void *)rcv_handler_for_handle);
    if (err_is_fail(err)) {
        return err;
    }

    bool first_message = args[LMP_ARGS_SIZE + 1] == 0;
    char ** name = (char **) args[LMP_ARGS_SIZE + 2];

    if (first_message) {
        uint32_t str_size = lmp_msg.words[2];
        args[LMP_ARGS_SIZE+3] = lmp_msg.words[1]; // store error
        args[LMP_ARGS_SIZE] = str_size;
        *name = malloc((str_size + 1) *sizeof(char));
        if (*name == NULL) {
            debug_printf("Malloc failed to allocate a string of the required size\n");
            return LIB_ERR_MALLOC_FAIL;
        }
    }

    char *copy_start = *name + args[LMP_ARGS_SIZE + 1];
    int string_off = first_message ? 2 : 1;
    int n_bytes_to_copy = MIN((9 - string_off) * 4, args[LMP_ARGS_SIZE]);
    strncpy(copy_start, (char *)(lmp_msg.words + string_off), n_bytes_to_copy);
    copy_start[n_bytes_to_copy] = 0;
    args[LMP_ARGS_SIZE] -= n_bytes_to_copy;
    args[LMP_ARGS_SIZE + 1] += n_bytes_to_copy;

    if (args[LMP_ARGS_SIZE] != 0) {
        struct aos_rpc* rpc = (struct aos_rpc*) args[0];
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler_for_handle, args));
        if (err_is_fail(err)) {
            debug_printf("can't set itself for the receive handler for the remaining bytes\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}

// open file at path and return via handle
errval_t aos_rpc_fat_open(struct aos_rpc* chan, char* path, void** handle) {
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 4];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_FS_OPEN;
    size_t l1 = strlen(path);
    args[2] = (uintptr_t) l1;
    size_t p1 = 0;
    size_t sent = 3;
    while(sent < 9 && p1 < l1) {
        strncpy((char*)&args[sent], &path[p1], 4);
        p1+=4;
        sent++;
        if (sent==9 && p1 < l1) {
            sent = 0;
            err = send_and_receive(rcv_handler_general, args);
            if (err_is_fail(err)) {
                debug_printf("aos_rpc_send_number failed\n");
                return err;
            }
        }
    }
    err = send_and_receive(rcv_handler_for_handle, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    //*handle = (void*) unpackage_handle(*(void**)args[LMP_ARGS_SIZE+2], (size_t)args[LMP_ARGS_SIZE + 1]); TODO: commented out due to trouble
    return (errval_t)args[LMP_ARGS_SIZE + 3];
}
// close the file given with handle
errval_t aos_rpc_fat_close(struct aos_rpc* chan, void* handle) {
    errval_t err;
    size_t* length = malloc(sizeof(size_t));
    //char* path = (char*)package_handle((struct fat32_handle*) handle, length);TODO: commented out due to trouble
    char* path = "";//TODO: remove this when commenting in line above
    uintptr_t args[LMP_ARGS_SIZE + 2];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_FS_CLOSE;
    size_t l1 = *length;
    args[2] = (uintptr_t) l1;
    size_t p1 = 0;
    size_t sent = 3;
    while(sent < 9 && p1 < l1) {
        strncpy((char*)&args[sent], &path[p1], 4);
        p1+=4;
        sent++;
        if (sent==9 && p1 < l1) {
            sent = 0;
            err = send_and_receive(rcv_handler_general, args);
            if (err_is_fail(err)) {
                debug_printf("aos_rpc_send_number failed\n");
                return err;
            }
        }
    }
    err = send_and_receive(rcv_handler_for_ns_register, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    return (errval_t)args[LMP_ARGS_SIZE + 1];
}
// read the file given in handle to buffer, read maximum of bytes, returns bytes_read with the actual read number
errval_t aos_rpc_fat_read(struct aos_rpc* chan, void* handle, void* buffer, size_t bytes, size_t* bytes_read) {

    return LIB_ERR_NOT_IMPLEMENTED;
}
// move read pointer of open file at handle, move by offset, new offset is given in handle->current_offset
errval_t aos_rpc_fat_seek(struct aos_rpc* chan, void* handle, enum fs_seekpos whence, off_t offset, off_t* result) {

    return LIB_ERR_NOT_IMPLEMENTED;
}
// open directory "path", return opened directory via handle
errval_t aos_rpc_fat_opendir(struct aos_rpc* chan, const char *path, void** handle) {

    return LIB_ERR_NOT_IMPLEMENTED;
}
// close the directory given by handle
errval_t aos_rpc_fat_closedir(struct aos_rpc* chan, void* handle) {
    return LIB_ERR_NOT_IMPLEMENTED;
}
// read next entry in the directory given by handle, return the name
errval_t aos_rpc_fat_dir_read_next(struct aos_rpc* chan, void* handle, char** name) {

    return LIB_ERR_NOT_IMPLEMENTED;
}
// returns the stat fs_fileinfo of the open file/dir given by handle
errval_t aos_rpc_fat_stat(struct aos_rpc* chan, void* handle, struct fs_fileinfo* info) {

    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * Networking rpc calls
 */


errval_t aos_rpc_network_init(struct waitset* ws, struct aos_rpc *network_rpc, struct capref cap_ep_network)
{
    errval_t err;

    network_rpc->ws = ws;
    err = lmp_chan_accept(&network_rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap_ep_network);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_accept network_rpc->lmp failed\n");
        return err;
    }
    thread_mutex_init(&network_rpc->rpc_mutex);

    err = lmp_chan_alloc_recv_slot(&network_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_network_init: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    // initial handshake with network manager
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) network_rpc;
    args[1] = AOS_RPC_ID_INIT;
    err = send_and_receive(rcv_handler_network_register, args);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_get_network_channel: send_and_receive failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

// bind a port, so that when a udp message arrives at this port, network will send the client a lmp message
errval_t aos_rpc_net_udp_bind(struct aos_rpc* rpc, uint16_t port, net_err_names_t *net_err)
{
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[LMP_ARGS_SIZE] = (uintptr_t) net_err;

    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_UDP_BIND;
    args[2] = (uintptr_t) port;  // msg->words[1] on receiver side

    errval_t err = send_and_receive(rcv_handler_for_net, args);
    // debug_printf("aos_rpc_net_udp_bind send and receive done\n");

    if (err_is_fail(err)) {
        debug_printf("aos_rpc_net_udp_bind failed\n");
        return err;
    }

    // debug_printf("aos_rpc_net_udp_bind returning\n");
    return SYS_ERR_OK;
}

// when a new udp datagram arrives, notify the binding client with this call
errval_t aos_rpc_net_udp_deliver(struct aos_rpc *rpc, uint32_t ip_src,
                                 uint16_t port_src, uint16_t port_dst,
                                 uint8_t *payload, uint16_t length, net_err_names_t *net_err)
{
    // debug_printf("aos_rpc_net_udp_deliver: length = %u\n", length);
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[LMP_ARGS_SIZE] = (uintptr_t) net_err;

    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_UDP_DELIVER;  // msg->words[0] on receiver
    args[2] = (uintptr_t) ip_src;
    args[3] = (uintptr_t) ((port_src << 16) | port_dst);

    const size_t header_length = 5;
    const uint16_t available_longs = (LMP_ARGS_SIZE - header_length);
    // send full packets
    const size_t num_packets = (size_t)length / (available_longs * 4);
    for (int i = 0; i < num_packets; i++) {
        // [ this length (16b) | remaining (16b) ]
        args[header_length - 1] = ((uint32_t)(available_longs * 4) << 16) | (uint16_t)(length - available_longs * (i + 1) * 4);
        // chars
        for (int j = 0; j < available_longs; j++) {
            args[j + header_length] = ((uintptr_t *) payload)[available_longs * i + j];
        }
        err = send_and_receive(rcv_handler_for_net, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_net_udp_deliver failed\n");
            return err;
        }
    }
    //debug_printf("first packet send_and_receive one, av = %u, remaining_bytes = %lu\n",
    //             available_longs, (size_t)length % (available_longs * 4));

    // send the remaining partial packet
    size_t remaining_bytes = (size_t)length % (available_longs * 4);
    if (remaining_bytes != 0) {
        // debug_printf("has remaining bytes\n");
        args[header_length - 1] = (remaining_bytes << 16);

        for (int j = 0; j < remaining_bytes; ++j) {
            ((uint8_t *)(&args[header_length]))[j] = payload[available_longs * num_packets * 4 + j];
        }
        // debug_printf("copied\n");
        err = send_and_receive(rcv_handler_for_net, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_net_udp_deliver last failed\n");
            return err;
        }
        // debug_printf("remaining bytes sent and acked\n");
    }

    return SYS_ERR_OK;
}

// send a udp message to a given ip:port via network
errval_t aos_rpc_net_udp_forward(struct aos_rpc *rpc, uint32_t ip_dst,
                                 uint16_t port_src, uint16_t port_dst,
                                 uint8_t *payload, uint16_t length, net_err_names_t *net_err)
{
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[LMP_ARGS_SIZE] = (uintptr_t) net_err;

    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) AOS_RPC_ID_UDP_FORWARD;
    args[2] = (uintptr_t) ip_dst;
    args[3] = (uintptr_t) ((port_src << 16) | port_dst);

    const size_t header_length = 5;
    const uint16_t available_longs = (LMP_ARGS_SIZE - header_length);
    // send full packets
    const size_t num_packets = (size_t)length / (available_longs * 4);
    for (int i = 0; i < num_packets; i++) {
        // [ this length (16b) | remaining (16b) ]
        args[header_length - 1] = ((uint32_t)(available_longs * 4) << 16) | (uint16_t)(length - available_longs * (i + 1) * 4);
        // chars
        for (int j = 0; j < available_longs; j++) {
            args[j + header_length] = ((uintptr_t *) payload)[available_longs * i + j];
        }
        err = send_and_receive(rcv_handler_for_net, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_net_udp_forward failed\n");
            return err;
        }
    }

    // send the remaining partial packet
    size_t remaining_bytes = (size_t)length % (available_longs * 4);
    if (remaining_bytes != 0) {
        args[header_length - 1] = (remaining_bytes << 16);

        for (int j = 0; j < remaining_bytes; ++j) {
            ((uint8_t *)(&args[header_length]))[j] = payload[available_longs * num_packets * 4 + j];
        }

        err = send_and_receive(rcv_handler_for_net, args);
        if (err_is_fail(err)) {
            debug_printf("aos_rpc_net_udp_forward last failed\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}

errval_t rcv_handler_for_net(void *v_args) {
    // debug_printf("in rcv_handler_for_net\n");
    uintptr_t *args = (uintptr_t *) v_args;
    net_err_names_t *net_err = (net_err_names_t *) args[LMP_ARGS_SIZE];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    struct capref cap;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap,
                                         (void *) rcv_handler_for_net);
    if (err_is_fail(err)) {
        debug_printf("in rcv_handler_for_net: fail\n");
        return err;
    }

    // debug_printf("in rcv_handler_for_net: net_err = %d\n", (net_err_names_t) lmp_msg.words[1]);
    *net_err = (net_err_names_t) lmp_msg.words[1];


    // debug_printf("in rcv_handler_for_net: returning\n");
    return SYS_ERR_OK;
}

errval_t aos_rpc_mmchs(struct aos_rpc* chan, void* buf, size_t block_nr) {
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 4];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_MMCHS;
    args[2] = (uintptr_t) block_nr;
    args[LMP_ARGS_SIZE] = (uintptr_t) buf;
    
    err = send_and_receive(rcv_handler_for_mmchs, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_send_number failed\n");
        return err;
    }
    buf = *(void**)args[LMP_ARGS_SIZE+2];
    return (errval_t)args[LMP_ARGS_SIZE + 3];
}

errval_t rcv_handler_for_mmchs (void *v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void *)rcv_handler_for_mmchs);
    if (err_is_fail(err)) {
        return err;
    }

    bool first_message = args[LMP_ARGS_SIZE + 1] == 0;
    char ** name = (char **) args[LMP_ARGS_SIZE + 2];

    if (first_message) {
        args[LMP_ARGS_SIZE+3] = lmp_msg.words[1]; // store error
        *name = malloc((512) *sizeof(char));
        if (*name == NULL) {
            debug_printf("Malloc failed to allocate a string of the required size\n");
            return LIB_ERR_MALLOC_FAIL;
        }
    }

    char *copy_start = *name + args[LMP_ARGS_SIZE + 1];
    int string_off = 2;//first_message ? 2 : 1;
    int n_bytes_to_copy = MIN((9 - string_off) * 4, 512 - args[LMP_ARGS_SIZE+1]);
    strncpy(copy_start, (char *)(lmp_msg.words + string_off), n_bytes_to_copy);
    copy_start[n_bytes_to_copy] = 0;
    args[LMP_ARGS_SIZE + 1] += n_bytes_to_copy;

    if (512 - args[LMP_ARGS_SIZE + 1] != 0) {
        struct aos_rpc* rpc = (struct aos_rpc*) args[0];
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler_for_mmchs, args));
        if (err_is_fail(err)) {
            debug_printf("can't set itself for the receive handler for the remaining bytes\n");
            return err;
        }
    }

    return SYS_ERR_OK;
}