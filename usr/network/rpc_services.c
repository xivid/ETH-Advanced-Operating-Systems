//
// Created by Zhifei Yang on 2017/12/21.
//

#include "rpc_services.h"

errval_t init_rpc_server(void) {
    errval_t err;
    /* // What's this?
    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPoint, 0, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cap retype of dispatcher to selfep failed");
        return EXIT_FAILURE;
    }
    */
    struct lmp_chan *lmp = (struct lmp_chan *) malloc(sizeof(struct lmp_chan));

    // by setting NULL_CAP as endpoint, we make this lmp an "open-receive" channel
    err = lmp_chan_accept(lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan accept (open-receive) failed");
        return err;
    }

    // allocate slot for receiving a cap
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan alloc recv slot failed");
        return err;
    }

    // the "reply-wait" operation
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void *) lmp_recv_handler, lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan register recv failed");
        return err;
    }
    return SYS_ERR_OK;
}

struct aos_rpc *get_udp_server_channel(port_t port) {
    for (struct udp_server *p = udp_server_list; p != NULL; p = p->next) {
        if (p->port == port) {
            return p->rpc;
        }
    }
    return NULL;
}


errval_t lmp_recv_handler(void *arg)
{
    struct lmp_chan *lmp = (struct lmp_chan *) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref cap_endpoint;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
    if (err_is_fail(err)) {
        if (lmp_err_is_transient(err)) {
            err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
        }
        if (err_is_fail(err)) {  // non-transient error, or transient but still fail after retry
            DEBUG_ERR(err, "lmp_recv_handler: lmp chan recv, non transient error");
            return err;
        }
    }

    // register again for receiving
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_recv_handler: lmp chan alloc recv slot failed");
        return err;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) lmp_recv_handler, arg));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_recv_handler: lmp chan register recv failed");
        return err;
    }

    // no message content received?
    if (msg.buf.msglen <= 0) {
        debug_printf("lmp_recv_handler: no message content received?\n");
        return err;
    }

    struct rpc_answer_t *answer;

    switch (msg.words[0]) {
        case AOS_RPC_ID_INIT:
            answer = marshal_init(cap_endpoint);
            break;
        case AOS_RPC_ID_UDP_BIND:
            answer = marshal_udp_bind(cap_endpoint, &msg);
            break;
        case AOS_RPC_ID_UDP_FORWARD:
            answer = marshal_udp_forward(cap_endpoint, &msg);
            break;
        default:
            debug_printf("RPC MSG Type %lu not supported!\n", msg.words[0]);
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    struct lmp_chan *ret_chan = answer->sender_lmp;
    if (!ret_chan) {
        return SYS_ERR_LMP_NO_TARGET;
    }

    err = lmp_chan_register_send(ret_chan, get_default_waitset(),
                                 MKCLOSURE((void*) lmp_send_handler, (void *)answer));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan register send failed");
        return err;
    }
    return SYS_ERR_OK;
}

struct client *whois(struct capref cap)
{
    struct client *cur = client_list;

    while (cur != NULL) {
        if (capcmp(cur->endpoint, cap)) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}


struct rpc_answer_t *marshal_init(struct capref cap_endpoint) {
    errval_t err;
    struct rpc_answer_t *ans = malloc(sizeof(struct rpc_answer_t));
    ans->sender_lmp = NULL;

    struct client *potential = whois(cap_endpoint);
    // already have a channel to this client
    if (potential != NULL) {
        ans->sender_lmp = &potential->rpc.lmp;
        ans->err = NET_ERR_CHANNEL_ALREADY_EXISTS;
        return ans;
    }

    // new client, allocate a channel (it's only used for sending,
    // receiving is always done by open-receiving)
    potential = (struct client*) malloc(sizeof(struct client));

    potential->endpoint = cap_endpoint;
    potential->next = client_list;
    client_list = potential;

    err = lmp_chan_accept(&potential->rpc.lmp, DEFAULT_LMP_BUF_WORDS, cap_endpoint);
    potential->rpc.ws = get_default_waitset();
    thread_mutex_init(&potential->rpc.rpc_mutex);

    ans->sender_lmp = &potential->rpc.lmp;
    ans->err = NET_ERR_OK;
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "marshal_init: could not do lmp chan accept");
        ans->err = NET_ERR_SERVER_FAULT;
    }

    return ans;
}

struct rpc_answer_t *marshal_udp_bind(struct capref cap, struct lmp_recv_msg *msg) {
    errval_t err;
    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    // get requested port number
    port_t port = (port_t) msg->words[1];

    // check if port is already occupied
    if (get_udp_server_channel(port) == NULL) {
        // register this binding to the udp server list
        struct udp_server *new_server = (struct udp_server *) malloc(sizeof(struct udp_server));
        new_server->port = port;
        new_server->rpc = &sender->rpc;
        new_server->next = udp_server_list;
        udp_server_list = new_server;
        err = NET_ERR_OK;
    } else {
        err = NET_ERR_PORT_ALREADY_TAKEN;
    }

    // assemble the response
    struct rpc_answer_t *ans = malloc(sizeof(struct rpc_answer_t));
    ans->sender_lmp = &sender->rpc.lmp;
    ans->err = err;

    return ans;
}

struct rpc_answer_t *marshal_udp_forward(struct capref cap, struct lmp_recv_msg *msg) {
    // TODO: similar to udp_echo_handler
    debug_printf("forward handler not implemented!\n");
    /*if (more chars) {
        receive_and_match_ack();
    } */
    // slip_send((uint8_t *)&udp_reply, 20 + len);
    return NULL;
}


errval_t lmp_send_handler(void *arg) {
    struct rpc_answer_t *answer = (struct rpc_answer_t *)arg;
    struct lmp_chan* lmp = answer->sender_lmp;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, (uintptr_t)(answer->err));
    if (err_is_ok(err)) {
        return err;
    }

    if (network_rpc_send_retry_count++ < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_register_send(lmp, get_default_waitset(), MKCLOSURE((void *)lmp_send_handler, arg));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "lmp_send_handler: lmp_chan_register_send failed\n");
            return err;
        }
        err = event_dispatch(get_default_waitset());
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "lmp_send_handler: first event_dispatch failed\n");
            return err;
        }
    } else {
        DEBUG_ERR(err, "lmp_send_handler: too many failed attempts\n");
        return err;
    }

    return SYS_ERR_OK;
}