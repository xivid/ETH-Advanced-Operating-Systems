#ifndef _LRPC_SERVER_H_
#define _LRPC_SERVER_H_

#include <aos/aos_rpc.h>
#include <spawn/spawn.h>
#include <spawn/process_manager.h>
#include "urpc_server.h"


struct client {
    // TODO: store the capref directly? (can avoid ugly comparison in whois())
    struct EndPoint end;
    struct client *prev;
    struct client *next;
    struct lmp_chan lmp;
};
extern struct client *client_list;
extern struct capref ns_endpoint; /// The endpoint cap to nameserver process

errval_t init_rpc(void);

// Functions for marshaling the answers.
void *marshal_number(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_char(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_str(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_process(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_pids(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_pname(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_retchar(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_init(struct capref cap);
void *marshal_ram(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_device_cap(struct capref cap, struct lmp_recv_msg *msg);
void *marshal_get_nameserver_endpoint(struct capref cap,
        struct lmp_recv_msg *msg);
void *marshal_register_nameserver(struct capref cap);
void *marshal_set_nameserver_endpoint(struct capref cap);

// Callbacks to process the marshaled answers and send the response.
typedef errval_t (*send_func_t)(void *);
errval_t send_received(void *args);
errval_t send_ram(void *args);
errval_t send_process(void *args);
errval_t send_pids(void *args);
errval_t send_pname(void *args);
errval_t send_char(void *args);
errval_t send_nameserver_ep(void *args);
errval_t send_device_cap(void *args);

#endif /* _LRPC_SERVER_H_ */
