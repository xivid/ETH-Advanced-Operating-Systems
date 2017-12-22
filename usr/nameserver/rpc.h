#ifndef _NS_RPC_H_
#define _NS_RPC_H_

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

// [aos_rpc, capability, message_type, err/id (optional), payload (optional)]
#define        RPC_SLOT 0
#define        CAP_SLOT 1
#define       TYPE_SLOT 2
#define     ERR_ID_SLOT 3
#define    MAX_LMP_ARGS 11
// Functions to communicate with init:
errval_t ns_init_rpc(void);
errval_t ns_send_register(void);
errval_t ns_send_listen_cap(void);

// Marshal functions:
void *ns_marshal_init(struct capref cap);
void *ns_marshal_register(struct capref endpoint, struct lmp_recv_msg *msg);
void *ns_marshal_deregister(struct lmp_recv_msg *msg);
void *ns_marshal_lookup(struct lmp_recv_msg *msg);
void *ns_marshal_enum(struct lmp_recv_msg *msg);

// Handlers and primitives:
errval_t send(void *v_args, void *send_handler, void *recv_handler);
errval_t ns_send_handler(void *v_args);
errval_t ns_recv_ack_handler(void *v_args);
errval_t ns_recv_listener_handler(void *v_args);

extern struct aos_rpc *listen_rpc;


#endif
