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
errval_t rcv_handler_for_ram (void *v_args);
errval_t rcv_handler_for_process (void *v_args);
errval_t rcv_handler_for_get_process_name (void *v_args);
errval_t rcv_handler_for_get_pids (void *v_args);
errval_t rcv_handler_for_ns(void *v_args);
errval_t rcv_handler_for_char(void *v_args);
errval_t rcv_handler_for_device_cap(void *v_args);
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
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    return receive_and_match_ack(args, &lmp_msg, &cap, (void *) rcv_handler_general);
}

errval_t receive_and_match_ack(uintptr_t *args, struct lmp_recv_msg *lmp_msg, struct capref *cap, void *rcv_handler)
{
    struct aos_rpc* rpc = (struct aos_rpc*) args[0];
    errval_t err = lmp_chan_recv(&rpc->lmp, lmp_msg, cap);

    int count = 0;
    while (count < AOS_RPC_ATTEMPTS && lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
                                     MKCLOSURE((void*) rcv_handler, args));
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("registering receive handler: too many failed attempts\n");
        return err;
    }

    if (lmp_msg->words[0] != AOS_RPC_ID_ACK) {
        debug_printf("Expected answer to be of type ACK\n");
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
        struct capref *retcap)
{
    // arguments: [aos_rpc, msg_id, retcap]
    uintptr_t args[3];
    args[0] = (uintptr_t) init_chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_NAMESERVER_EP;
    args[2] = (uintptr_t) retcap;

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
    uintptr_t* args = (uintptr_t*) v_args;
    struct capref* retcap = (struct capref*) args[2];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    return receive_and_match_ack(args, &lmp_msg, retcap, (void*) rcv_handler_for_ns);
}

errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc)
{
    errval_t err;
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) chan;
    args[1] = (uintptr_t) AOS_RPC_ID_GET_CHAR;
    args[2] = (uintptr_t) 0; // The core dest

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
    args[1] = (uintptr_t) AOS_RPC_ID_PROCESS;  // msg->words[0] on server side
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
    domainid_t *new_pid = (domainid_t *) args[3];
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;

    errval_t err = receive_and_match_ack(args, &lmp_msg, &cap, (void*) rcv_handler_for_process);
    if (err_is_fail(err)) {
        *new_pid = 0;
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

// mount mount fat32 drive is mounted to path, given also the uri of the mount command
errval_t aos_rpc_fat_mount(struct aos_rpc* chan, const char* path, const char* uri) {
    
    return LIB_ERR_NOT_IMPLEMENTED;
}
// unmounts the fat32 drive (use with care)
errval_t aos_rpc_fat_unmount(struct aos_rpc* chan){
    
    return LIB_ERR_NOT_IMPLEMENTED;
}
// open file at path and return via handle
errval_t aos_rpc_fat_open(struct aos_rpc* chan, char* path, void** handle) {
    
    return LIB_ERR_NOT_IMPLEMENTED;
}
// close the file given with handle
errval_t aos_rpc_fat_close(struct aos_rpc* chan, void* handle) {
    
    return LIB_ERR_NOT_IMPLEMENTED;
}
// read the file given in handle to buffer, read maximum of bytes, returns bytes_read with the actual read number
errval_t aos_rpc_fat_read(struct aos_rpc* chan, void* handle, void* buffer, size_t bytes, size_t* bytes_read) {
    
    return LIB_ERR_NOT_IMPLEMENTED;
}
// move read pointer of open file at handle, move by offset, new offset is given in handle->current_offset
errval_t aos_rpc_fat_seek(struct aos_rpc* chan, void* handle, enum fs_seekpos whence, off_t offset) {
    
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