#include "rpc.h"

errval_t send(void *v_args, void *send_handler, void *recv_handler)
{
    uintptr_t *args = (uintptr_t *) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[0];
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

errval_t fs_recv_listener_handler(void *v_args)
{
    struct capref cap;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct lmp_chan *lmp = (struct lmp_chan*) v_args;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap);
    int count = 0;
    while (count < AOS_RPC_ATTEMPTS &&
            lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(lmp, &msg, &cap);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("fs_recv_ack_handler: too many failed attempts\n");
        return err;
    }


    void *answer_args;
    send_func_t send_handler;
    switch (msg.words[0]) {
        case AOS_RPC_INIT:
            answer_args = fs_marshal_init(cap);
            send_handler = fs_send_received;
        case AOS_RPC_ID_FS_MOUNT:
            answer_args = fs_marshal_mount(cap, &msg);
            send_handler = fs_send_error;
            break;
        case AOS_RPC_ID_FS_UNMOUNT:
            answer_args = fs_marshal_unmount(cap);
            send_handler = fs_send_error;
            break;
        case AOS_RPC_ID_FS_OPEN:
            answer_args = fs_marshal_open(cap, &msg);
            send_handler = fs_send_handle;
            break;
        case AOS_RPC_ID_FS_CLOSE:
            answer_args = fs_marshal_close(cap, &msg);
            send_handler = fs_send_error;
            break;
        case AOS_RPC_ID_FS_READ:
            answer_args = fs_marshal_read(cap, &msg);
            send_handler = fs_send_read;
            break;
        case AOS_RPC_ID_FS_SEEK:
            answer_args = fs_marshal_seek(cap, &msg);
            send_handler = fs_send_seek;
            break;
        case AOS_RPC_ID_FS_OPENDIR:
            answer_args = fs_marshal_opendir(cap, &msg);
            send_handler = fs_send_handle;
            break;
        case AOS_RPC_ID_FS_CLOSEDIR:
            answer_args = fs_marshal_closedir(cap, &msg);
            send_handler = fs_send_error;
            break;
        case AOS_RPC_ID_FS_DIR_READ_NEXT:
            answer_args = fs_marshal_next_dir(cap, &msg);
            send_handler = fs_send_next_dir_name;
            break;
        case AOS_RPC_ID_FS_STAT:
            answer_args = fs_marshal_stat(cap, &msg);
            send_handler = fs_send_stat;
            break;
        default:
            debug_printf("unknown message sent to nameserver: %d\n",
                    lmp_msg.words[0]);
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    if (answer_args == NULL) {
        debug_printf(fs_recv_listener_handler: answer_args is NULL\n");
        return EXIT_FAILURE;
    }
    else if(answer_args == -1) {
        answer_args = (void*)&whois(cap)->lmp;
        send_handler = fs_send_received;
    }

    struct lmp_chan *ret_chan = (struct lmp_chan *) answer_args;
    if (!ret_chan) {
        return SYS_ERR_LMP_NO_TARGET;
    }
    err = lmp_chan_register_send(ret_chan, get_default_waitset(),
            MKCLOSURE((void*) send_handler, answer_args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan register send failed");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t fs_send_received(void *arg) {
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, 1); // send a 1 to signal that the message arrived
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c send received: could not do lmp chan send1");
        return err;
    }
    return SYS_ERR_OK;
}

// used by mount, close, closedir 
errval_t fs_send_error(void* args) {
 
    // get channel
    struct error_answer_t* error_args = (struct error_answer_t*) args;

    struct lmp_chan* lmp = &error_args->sender_lmp;
    errval_t *err = &error_args->err;

    errval_t err_send = lmp_chan_send2(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send error: could not do lmp chan send2");
        return err_send;
    }

    return SYS_ERR_OK;
}

// for seek
errval_t fs_send_seek(void* args) {
 
    // get channel
    struct seek_answer_t* seek_args = (struct seek_answer_t*) args;

    struct lmp_chan* lmp = &seek_args->sender_lmp;
    errval_t *err = &seek_args->err;
    size_t offset = seek_args->offset;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err, offset);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send seek: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}
// for open, opendir
errval_t fs_send_handle(void* args) {
    
    struct handle_answer_t* handle_args = (struct handle_answer_t*) args;

    struct lmp_chan* lmp = &handle_args->sender_lmp;
    errval_t *err = &handle_args->err;
    struct fat32_handle* handle = &handle_args->handle;
    size_t* length = malloc(sizeof(size_t));
    void* buffer = package_handle(handle, length);
    char* buf = (char*) buffer;
    uintptr_t msg[9] = {0};
    msg[0] = (size_t)(err_is_fail(*err)? 0:1);
    msg[1] = (uintptr_t) *err;
    msg[2] = (uintptr_t) *length;
    size_t current = MIN(24, *length);
    memcpy(&msg[3], (void*)buf, current);
    
    errval_t err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send handle: could not do lmp chan send9");
        return err_send;
    }
    // send 32 bytes until there's no full 
    while (current < *length) {
        memcpy(&msg[1], (void*)buf[current], MIN(32, *length-current);
        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send handle: could not do lmp chan send5");
            return err_send;
        }
        current+=32;
    }
    handle_close(handle);
    return SYS_ERR_OK;
}

// for read
errval_t fs_send_read(void* args) {
    //handle buffer similar to how we handle a char* (i.e. send_pname from lrpc_server.c)
    struct read_answer_t* read_args = (struct read_answer_t*) args;

    struct lmp_chan* lmp = &read_args->sender_lmp;
    errval_t *err = &read_args->err;
    char* buffer = (char*)read_args->buffer;
    size_t length = read_args->bytes_read;

    uintptr_t msg[9] = {0};
    msg[0] = (size_t)(err_is_fail(*err)? 0:1)
    msg[1] = (uintptr_t) *err;
    msg[2] = length;

    // get string and send in possibly multiple packets
    // put into msg[3..8], 6 words <=> 24 bytes
    strncpy((char *)&msg[3], buffer, 24);

    err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c fs send read: could not do lmp_chan_send9");
        return err_send;
    }

    if (length <= 24)
        return SYS_ERR_OK;

    // if there are remaining characters
    length -= 24;
    buffer += 24;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[1..8]
        strncpy((char *)&msg[1], buffer, 32);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c fs send read: could not do lmp_chan_send9");
            return err_send;
        }

        buffer += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&msg[1], (const char*) buffer, length);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c fs send read: could not do lmp_chan_send9");
            return err_send;
        }
    }
    free(read_args->buffer);

    return SYS_ERR_OK;
}

// for dir_read_next, as the name is limited to 13 chars we don't need to return the length
errval_t fs_send_next_dir_name(void* args) {
    struct next_dir_name_answer_t* read_dir_args = (struct next_dir_name_answer_t*) args;

    struct lmp_chan* lmp = &read_dir_args->sender_lmp;
    errval_t *err = &read_dir_args->err;
    char* name = read_dir_args->name;
    uintptr_t msg[4] = {0};
    strncpy((char*)&msg[0], name, 13);

    errval_t err_send = lmp_chan_send6(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err, msg[0], msg[1], msg[2], msg[3]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send next dir name: could not do lmp chan send6");
        return err_send;
    }
    free(name);

    return SYS_ERR_OK;
}

errval_t fs_send_stat(void* args) {
    
    struct stat_answer_t* stat_args = (struct stat_answer_t*) args;

    struct lmp_chan* lmp = &stat_args->sender_lmp;
    errval_t *err = &stat_args->err;
    fs_fileinfo* info = &stat_args->info;
    size_t size = info->size;
    uintptr_t dir = (uintptr_t) info->type;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err, size, dir);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/fs/fs_rpc_server.c send handle: could not do lmp chan send3");
        return err_send;
    }
    free(info);

    return SYS_ERR_OK;
}