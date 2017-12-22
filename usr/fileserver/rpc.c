#include "rpc.h"

struct aos_rpc *listen_rpc = NULL;
struct client *client_list;

errval_t fs_init_rpc(void)
{
    errval_t err;
    // Set up the listening channel.
    listen_rpc = malloc(sizeof(struct aos_rpc));
    err = lmp_chan_accept(&listen_rpc->lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/fs/rpc.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/fs/rpc.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
            MKCLOSURE((void*) fs_recv_listener_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/fs/rpc.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    // TODO do we need this?
    // Send a registration message to init.
    err = fs_send_register();
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return 1;
    }

    /* // Send the endpoint to the listening channel to init. */
    err = fs_send_listen_cap();
    if (err_is_fail(err)) {
        debug_printf("send fs endpoint failed\n");
        return 1;
    }

    debug_printf("fs done setting rpc\n");
    return SYS_ERR_OK;
}

errval_t fs_send_register(void) {
    // TODO: adapt if this is required
    struct aos_rpc *rpc = get_init_rpc();
    // it's ok to use stack variables here because the calls are made blocking
    uintptr_t args[MAX_LMP_ARGS];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &rpc->lmp.local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_REGISTER_NAMESERVER_WITH_INIT;

    errval_t err = send(args, fs_send_handler, fs_recv_ack_handler);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t fs_send_listen_cap(void) {
    struct aos_rpc *rpc = get_init_rpc();
    uintptr_t args[MAX_LMP_ARGS];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &listen_rpc->lmp.local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_SET_NAMESERVER_EP;

    errval_t err = send(args, fs_send_handler, fs_recv_ack_handler);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t fs_recv_ack_handler(void *v_args)
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

// ugly solution as we have this code in lrpc_server.c already...
struct client *whois(struct capref cap)
{
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c whois: debug identify cap failed");
        return NULL;
    }
    // TODO: rewrite this with cap_compare()
    while (cur != NULL) {
        if (return_cap.u.endpoint.listener == cur->end.listener
            && return_cap.u.endpoint.epoffset == cur->end.epoffset) {
            break;
        }
        cur = cur->next;
    }
    return cur;
}


void *fs_marshal_unmount(struct capref cap) {
    errval_t err = fat32_unmount();
    struct error_answer_t* args = malloc(sizeof(struct error_answer_t));
    args->sender_lmp = whois(cap)->lmp;
    args->err = err;
    return (void*) args;
}

struct arg_collector* mount_args = NULL;
void *fs_marshal_mount(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = mount_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len1 = (size_t)msg->words[1];
        args->len2 = (size_t)msg->words[2];
        args->arg1 = malloc(args->len1);
        args->arg2 = malloc(args->len2);
        args->stat1 = 0;
        args->stat2 = 0;
        read=3;
    }
    while (read<9 && (args->stat1 < args->len1)) {
        strncpy(&args->arg1[args->stat1], (char*)msg->words[read], 4);
        args->stat1+=4;
        read++;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if ((args->stat1 < args->len1) || (args->stat2 < args->len2)) {
        return (void*)-1;
    }
    struct error_answer_t* answer = malloc(sizeof(struct error_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_mount(args->arg1, args->arg2);
    
    free(args->arg1);
    free(args->arg2);
    free(args);
    mount_args = NULL;
    
    return (void*) answer;
}

struct arg_collector* open_args = NULL;
void *fs_marshal_open(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = open_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len1 = (size_t)msg->words[1];
        args->arg1 = malloc(args->len1);
        args->stat1 = 0;
        read=2;
    }
    while (read<9 && (args->stat1 < args->len1)) {
        strncpy(&args->arg1[args->stat1], (char*)msg->words[read], 4);
        args->stat1+=4;
        read++;
    }
    if ((args->stat1 < args->len1)) {
        return (void*)-1;
    }
    struct handle_answer_t* answer = malloc(sizeof(struct handle_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_open(args->arg1, answer->handle);
    
    free(args->arg1);
    free(args);
    open_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* close_args = NULL;
void *fs_marshal_close(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = close_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len2 = (size_t)msg->words[1];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=2;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct error_answer_t* answer = malloc(sizeof(struct error_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_close(handle);
    
    free(args->arg2);
    free(args);
    close_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* read_args = NULL;
void *fs_marshal_read(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = read_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len1 = (size_t)msg->words[1];
        args->len2 = (size_t)msg->words[2];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=3;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct read_answer_t* answer = malloc(sizeof(struct read_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->buffer = malloc(args->len1);
    answer->err = fat32_read(handle, answer->buffer, args->len1, &answer->bytes_read);
    
    handle_close(handle);
    free(args->arg2);
    free(args);
    read_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* seek_args = NULL;
void *fs_marshal_seek(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = seek_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len2 = (size_t)msg->words[1];
        args->len1 = (size_t)msg->words[2];
        args->stat1 = (size_t)msg->words[3];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=4;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    enum fs_seekpos whence = (enum fs_seekpos)args->len1;
    off_t offset = (off_t) args->stat1;
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct seek_answer_t* answer = malloc(sizeof(struct handle_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_seek(handle, whence, offset);
    answer->offset = handle->file_pos;
    free(args->arg2);
    free(args);
    seek_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* opendir_args = NULL;
void *fs_marshal_opendir(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = opendir_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len1 = (size_t)msg->words[1];
        args->len2 = (size_t)msg->words[2];
        args->arg1 = malloc(args->len1);
        args->arg2 = malloc(args->len2);
        args->stat1 = 0;
        args->stat2 = 0;
        read=3;
    }
    while (read<9 && (args->stat1 < args->len1)) {
        strncpy(&args->arg1[args->stat1], (char*)msg->words[read], 4);
        args->stat1+=4;
        read++;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if ((args->stat1 < args->len1) || (args->stat2 < args->len2)) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct handle_answer_t* answer = malloc(sizeof(struct handle_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_opendir(args->arg1, (void**)&handle);
    answer->handle = handle;
    
    free(args->arg1);
    free(args->arg2);
    free(args);
    opendir_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* closedir_args = NULL;
void *fs_marshal_closedir(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = closedir_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len2 = (size_t)msg->words[1];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=2;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct error_answer_t* answer = malloc(sizeof(struct error_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_closedir(handle);
    
    free(args->arg2);
    free(args);
    closedir_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* read_next_args = NULL;
void *fs_marshal_dir_read_next(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = read_next_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len2 = (size_t)msg->words[1];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=2;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct next_dir_name_answer_t* answer = malloc(sizeof(struct next_dir_name_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_dir_read_next(handle, &answer->name);
    
    free(args->arg2);
    free(args);
    read_next_args = NULL;
    
    return (void*)answer;
}
struct arg_collector* stat_args = NULL;
void *fs_marshal_stat(struct capref endpoint, struct lmp_recv_msg *msg) {
    struct arg_collector* args = stat_args;
    size_t read = 1; // how many messages used up already
    if (args == NULL) {
        args = malloc(sizeof(struct arg_collector));
        args->len2 = (size_t)msg->words[1];
        args->arg2 = malloc(args->len2);
        args->stat2 = 0;
        read=2;
    }
    while (read<9 && (args->stat2 < args->len2)) {
        strncpy(&args->arg2[args->stat2], (char*)msg->words[read], 4);
        args->stat2+=4;
        read++;
    }
    if (args->stat2 < args->len2) {
        return (void*)-1;
    }
    void* buffer = (void*)args->arg2;
    struct fat32_handle* handle = unpackage_handle(buffer, args->len2);
    struct stat_answer_t* answer = malloc(sizeof(struct error_answer_t));
    answer->sender_lmp = whois(endpoint)->lmp;
    answer->err = fat32_stat(handle, answer->info);
    
    free(args->arg2);
    free(args);
    stat_args = NULL;
    
    return (void*)answer;
}

void *fs_marshal_init(struct capref cap) {
    struct client *potential = whois(cap);
    // already have a channel to this client
    if (potential != NULL) {
        return (void*) &(potential->lmp);
    }

    // new client, allocate a channel (it's only used for sending,
    // receiving is always done by open-receiving)
    potential = (struct client*) malloc(sizeof(struct client));

    struct capability return_cap;
    debug_cap_identify(cap, &return_cap);

    potential->end = return_cap.u.endpoint;
    potential->prev = NULL;
    if (client_list == NULL) {
        potential->next = NULL;
    } else {
        potential->next = client_list;
        client_list->prev = potential;
    }
    client_list = potential;

    errval_t err = lmp_chan_accept(&potential->lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not do lmp chan accept");
        return NULL;
    }
    return (void*) &(potential->lmp);
}