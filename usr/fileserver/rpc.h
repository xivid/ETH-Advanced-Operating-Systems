#ifndef _FS_RPC_H_
#define _FS_RPC_H_

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

// [aos_rpc, capability, message_type, id (optional)]
#define MAX_LMP_ARGS 4
#define    RPC_SLOT 0
#define    CAP_SLOT 1
#define   TYPE_SLOT 2
#define ERR_ID_SLOT 3

// Functions to communicate with init:
errval_t fs_init_rpc(void);
errval_t fs_send_register(void);
errval_t fs_send_listen_cap(void);

// Marshal functions:
void *fs_marshal_unmount(struct capref cap);
void *fs_marshal_mount(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_open(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_close(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_read(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_seek(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_opendir(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_closedir(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_dir_read_next(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_stat(struct capref endpoint, struct lmp_recv_msg *msg);
void *fs_marshal_init(struct capref cap);

// Handlers and primitives:
errval_t send(void *v_args, void *send_handler, void *recv_handler);
errval_t fs_recv_ack_handler(void *v_args);
errval_t fs_recv_listener_handler(void *v_args);

errval_t fs_send_error(void *args);
errval_t fs_send_seek(void *args);
errval_t fs_send_handle(void *args);
errval_t fs_send_read(void *args);
errval_t fs_send_next_dir_name(void *args);
errval_t fs_send_stat(void *args);
errval_t fs_send_handler(void *v_args);

struct client {
    // TODO: store the capref directly? (can avoid ugly comparison in whois())
    struct EndPoint end;
    struct client *prev;
    struct client *next;
    struct lmp_chan lmp;
};


struct client *whois(struct capref cap);
extern struct client *client_list;
//extern struct aos_rpc *listen_rpc;

typedef errval_t (*send_func_t)(void *);

// used by mount close, closedir 
struct error_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
};
// for open, opendir
struct handle_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    void* handle;
};

// for read
struct read_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    void* buffer;
    size_t bytes_read;
};

// for dir_read_next, as the name is limited to 13 chars we don't need to return the length
struct next_dir_name_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    char *name;
};
// for stat
struct stat_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    struct fs_fileinfo* info;
};
// for seek
struct seek_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    size_t offset;
};

struct arg_collector {
    char* arg1;
    char* arg2;
    size_t len1;
    size_t stat1;
    size_t len2;
    size_t stat2;
};

#endif
