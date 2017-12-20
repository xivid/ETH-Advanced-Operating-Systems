#ifndef _NS_RPC_H_
#define _NS_RPC_H_

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

// [aos_rpc, capability, message_type, id (optional)]
#define MAX_LMP_ARGS 4

// Functions to communicate with init:
errval_t ns_init_rpc(void);
errval_t ns_send_register(void);
errval_t ns_send_listen_cap(void);

// Marshal functions:
void *ns_marshal_init(struct capref cap);

// Handlers and primitives:
errval_t send(void *v_args, void *send_handler, void *recv_handler);
errval_t ns_send_handler(void *v_args);
errval_t ns_recv_ack_handler(void *v_args);
errval_t ns_recv_listener_handler(void *v_args);

extern struct aos_rpc *listen_rpc;


#endif
