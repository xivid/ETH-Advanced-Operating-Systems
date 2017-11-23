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

#define LMP_ARGS_SIZE (10)

#define min(a,b) ( ((a) < (b)) ? (a) : (b) )

errval_t send_handler (void *v_args);

errval_t rcv_handler_general (void *v_args);
errval_t rcv_handler_for_ram (void *v_args);
errval_t rcv_handler_for_process (void *v_args);
errval_t rcv_handler_for_get_process_name (void *v_args);
errval_t rcv_handler_for_get_pids (void *v_args);

errval_t send_and_receive (void* rcv_handler, uintptr_t* args);


errval_t send_handler (void* v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct lmp_chan *lmp = (struct lmp_chan*) args[0];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, lmp->local_cap, args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        count++;
    }
    debug_printf("send_handler: too many failed attempts\n");
    return err;
}

// checks that the message was acknowledged with an ACK
errval_t rcv_handler_general (void* v_args)
{
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(&rpc->lmp, &lmp_msg, &cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                MKCLOSURE((void*) rcv_handler_general, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("rcv_handler_general: too many failed attempts or non transient error\n");
        return err;
    }
    // check that message was received
    if (lmp_msg.buf.msglen != 1 || lmp_msg.words[0] != AOS_RPC_ID_ACK) {
        debug_printf("rcv_handler_general: received message not es expected");
        return FLOUNDER_ERR_RPC_MISMATCH;
    }
    return SYS_ERR_OK;
}

errval_t send_and_receive (void* rcv_handler, uintptr_t* args)
{
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];

    // set handlers
    errval_t err = lmp_chan_register_send(&rpc->lmp, rpc->ws, MKCLOSURE((void *)send_handler, args));
    if (err_is_fail(err)) {
        debug_printf("send_and_receive: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(&rpc->lmp, rpc->ws, MKCLOSURE(rcv_handler, args));
    if (err_is_fail(err)) {
        debug_printf("send_and_receive: lmp_chan_register_rcv failed\n");
        return err;
    }

    // wait for send and receive ready:
    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send_and_receive: first event_dispatch failed\n");
        return err;
    }

    err = event_dispatch(rpc->ws);
    if (err_is_fail(err)) {
        debug_printf("send_and_receive: second event_dispatch failed\n");
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
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_RAM;
    args[2] = (uintptr_t) size;
    args[3] = (uintptr_t) align;
    args[4] = (uintptr_t) retcap;
    args[5] = (uintptr_t) ret_size;

    assert(chan != NULL);

    // setup receiver slot
    /* thread_mutex_lock(&chan->rpc_mutex); */
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
    /* thread_mutex_unlock(&chan->rpc_mutex); */

    return SYS_ERR_OK;
}

errval_t rcv_handler_for_ram (void* v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref* cap = (struct capref*) args[3];
    size_t* ret_size = (size_t*) args[4];

    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(&rpc->lmp, &lmp_msg, cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                MKCLOSURE((void*) rcv_handler_for_ram, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("rcv_handler_for_ram: too many failed attempts or non transient error\n");
        return err;
    }
    // check that message was received
    if (lmp_msg.words[0] == AOS_RPC_ID_ACK) {
        *ret_size = lmp_msg.words[1];
    }

    return (errval_t) lmp_msg.words[2];
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
    // TODO: what if string is bigger than 28 chars?
    uintptr_t args[LMP_ARGS_SIZE];
    // order: 0-chan, 1-newpid, 2..8-the name
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_PROCESS;
    args[2] = (uintptr_t) core;
    args[3] = (uintptr_t) newpid;
    int str_size = strlen(name) + 1;
    int blocks = str_size / 4 + (str_size % 4 != 0);
    for (int j = 0; j < blocks; j++) {
        args[j+4] = ((uintptr_t *) name)[j];
    }
    errval_t err = send_and_receive(rcv_handler_for_process, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_process_spawn failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_process(void *v_args) {
    uintptr_t* args = (uintptr_t*) v_args;
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    domainid_t *new_pid = (domainid_t *) args[2];

    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(&rpc->lmp, &lmp_msg, &cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void *) rcv_handler_for_process, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("rcv_handler_for_process: too many failed attempts or non transient error\n");
        return err;
    }

    // check that message was received
    if (lmp_msg.buf.msglen == 3 && lmp_msg.words[0] == AOS_RPC_ID_ACK) {
        *new_pid = (domainid_t) lmp_msg.words[1];
        /* add_new_process(rpc, name, core, lmp_msg.words[1]); */
        /* print_process_table(rpc); */
    }

    return (errval_t) lmp_msg.words[2];
}

errval_t aos_rpc_process_get_name(struct aos_rpc *chan, domainid_t pid,
                                  char **name)
{
    uintptr_t args[LMP_ARGS_SIZE + 3];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_PNAME;
    args[2] = (uintptr_t) pid;
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
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    errval_t err = lmp_chan_recv(&rpc->lmp, &lmp_msg, &cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                MKCLOSURE((void*) rcv_handler_general, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("rcv_handler_general: too many failed attempts or non transient error\n");
        return err;
    }

    if (lmp_msg.words[0] != AOS_RPC_ID_ACK) {
        debug_printf("rcv_handler_general: received message not es expected\n");
        return FLOUNDER_ERR_RPC_MISMATCH;
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
    int n_bytes_to_copy = min((9 - string_off) * 4, args[LMP_ARGS_SIZE]);
    strncpy(copy_start, (char *)(lmp_msg.words + string_off), n_bytes_to_copy);
    copy_start[n_bytes_to_copy] = 0;
    args[LMP_ARGS_SIZE] -= n_bytes_to_copy;
    args[LMP_ARGS_SIZE + 1] += n_bytes_to_copy;

    if (args[LMP_ARGS_SIZE] == 0) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler_general, args));
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
    uintptr_t args[LMP_ARGS_SIZE];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_PIDS;
    args[2] = (uintptr_t) pid_count;
    args[3] = (uintptr_t) pids;
    args[4] = (uintptr_t) 0; // current bytes
    args[5] = (uintptr_t) 0; //

    errval_t err = send_and_receive(rcv_handler_for_get_pids, args);
    if (err_is_fail(err)) {
        debug_printf("aos_rpc_process_get_name failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t rcv_handler_for_get_pids (void *v_args)
{
    // TODO: Implement
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
    rpc->head = NULL;

    errval_t err = lmp_chan_accept(&rpc->lmp, DEFAULT_LMP_BUF_WORDS, cap_initep);
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

/* void add_new_process(struct aos_rpc *rpc, char *name, coreid_t core, domainid_t id) { */
/*     struct domaininfo *new_domain = malloc(sizeof(struct domaininfo)); */
/*     new_domain->domain_name = name; */
/*     new_domain->core_id = core; */
/*     new_domain->pid = id; */
/*     new_domain->next = rpc->head; */
/*     rpc->head = new_domain; */
/* } */

/* void print_process_table(struct aos_rpc *rpc) { */
/*     struct domaininfo *cur = rpc->head; */
/*     debug_printf("------------------------------------------\n"); */
/*     debug_printf("              process  table              \n"); */
/*     debug_printf("------------------------------------------\n"); */
/*     debug_printf("%8s\t%5s\tname\n", "core", "pid"); */
/*     debug_printf("------------------------------------------\n"); */
/*     while (cur) { */
/*         debug_printf("%8d\t%5d\t%s\n", (int)cur->core_id, cur->pid, cur->domain_name); */
/*         cur = cur->next; */
/*     } */
/*     debug_printf("------------------------------------------\n"); */
/* } */
