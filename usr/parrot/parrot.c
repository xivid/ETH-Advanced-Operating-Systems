/**
 * \file
 * \brief UDP Echo server - parrot
 */

/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */


#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <aos/aos.h>
#include <aos/nameserver.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

void debug_print_packet(uint8_t *data, size_t size);


/** RPC server of UDP echo server
 *  Supports the following rpc message types:
 *    AOS_RPC_ID_UDP_RECV
 */

struct rpc_answer_t {
    struct lmp_chan* sender_lmp;
    net_err_names_t err;
};

struct aos_rpc *listen_rpc = NULL;
errval_t init_rpc_server(void);
errval_t lmp_recv_handler(void *v_args);
errval_t lmp_send_handler(void *v_args);
static int send_retry_count = 0;
struct rpc_answer_t *marshal_udp_deliver(struct lmp_recv_msg *msg, struct lmp_chan *lmp);

bool pending_reply = false;
#define UDP_PAYLOAD_MAX_BYTES 1006
struct echo_reply_t {
    uint32_t ip_dst;
    uint16_t port_src;
    uint16_t port_dst;
    uint8_t payload[UDP_PAYLOAD_MAX_BYTES];
    uint16_t length;
} echo_reply;

struct capref cap_ep_network;
struct aos_rpc *network_rpc;

int main(int argc, char *argv[])
{
    /*if (argc < 2) {
        debug_printf("Usage: %s [port]\n", argv[0]);
        return EXIT_FAILURE;
    }*/

    errval_t err;
    net_err_names_t net_err;

    //! step 1 - look up network manager endpoint from name server
    ns_err_names_t ns_err;
    do {
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    debug_printf("got nameserver's cap\n");

    do {
        ns_lookup("network", &cap_ep_network, &ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    debug_printf("obtained cap_ep_network\n");


    //! step 2 - set up my own rpc server
    debug_printf("Setting up a open-receiving rpc channel\n");
    err = init_rpc_server();
    if (err_is_fail(err)) {
        debug_printf("Failed setting up a listening rpc channel\n");
        return EXIT_FAILURE;
    }


    //! step 3 - establish network_rpc channel
    debug_printf("Getting rpc channel to network manager\n");
    network_rpc = malloc(sizeof(struct aos_rpc));
    err = aos_rpc_network_init(get_default_waitset(), network_rpc, cap_ep_network);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Unable to establish rpc channel to network manager\n");
        return EXIT_FAILURE;
    }

    //! step 4 - register to the network manager to bind on the specified port
    uint16_t port = 7777; //(uint16_t) strtol(argv[1], (char **)NULL, 10);
    debug_printf("Binding to port %u\n", port);
    err = aos_rpc_net_udp_bind(network_rpc, port, &net_err);
    if (err_is_fail(err)) {
        debug_printf("Unable to bind to port %u\n", port);
        return EXIT_FAILURE;
    }
    if (net_err != NET_ERR_OK) {
        debug_printf("Unable to bind to port %u, net_err = %d\n", port, net_err);
    }


    //! step 5 - wait for events
    debug_printf("parrot listening on port %d\n", port);
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }

        if (pending_reply) {
            err = aos_rpc_net_udp_forward(network_rpc, echo_reply.ip_dst,
                                          echo_reply.port_src, echo_reply.port_dst,
                                          echo_reply.payload, echo_reply.length, &net_err);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "aos_rpc_net_udp_forward failed");
            }
            if (net_err != NET_ERR_OK) {
                debug_printf("aos_rpc_net_udp_forward failed: net_err = %d\n", net_err);
            }
            pending_reply = false;
        }
    }

    return EXIT_SUCCESS;
}

//! RPC server implementations

errval_t init_rpc_server(void) {
    errval_t err;
    // What's this?
    /*err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPoint, 0, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cap retype of dispatcher to selfep failed");
        return EXIT_FAILURE;
    }*/

    listen_rpc = malloc(sizeof(struct aos_rpc));
    // struct lmp_chan *lmp = (struct lmp_chan *) malloc(sizeof(struct lmp_chan));

    // by setting NULL_CAP as endpoint, we make this lmp an "open-receive" channel
    err = lmp_chan_accept(&listen_rpc->lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan accept (open-receive) failed");
        return err;
    }

    // allocate slot for receiving a cap
    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan alloc recv slot failed");
        return err;
    }

    // the "reply-wait" operation
    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
                                 MKCLOSURE((void *) lmp_recv_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp chan register recv failed");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t lmp_recv_handler(void *arg)
{
    struct lmp_chan *lmp = (struct lmp_chan *) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref cap_endpoint;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
    int count = 0;
    while (count < AOS_RPC_ATTEMPTS &&
           lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("lmp_recv_handler: too many failed attempts\n");
        return err;
    }

    // TODO: client is always network, don't set up a new one every time
    struct lmp_chan *client_lmp = malloc(sizeof(struct lmp_chan));
    err = lmp_chan_accept(client_lmp, DEFAULT_LMP_BUF_WORDS, cap_endpoint);
    if (err_is_fail(err)) {
        debug_printf("lmp_recv_handler: lmp chan accept failed\n");
        return err;
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
        case AOS_RPC_ID_UDP_DELIVER:
            answer = marshal_udp_deliver(&msg, client_lmp);
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

/*
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
        case AOS_RPC_ID_UDP_DELIVER:
            answer = marshal_udp_deliver(cap_endpoint, &msg);
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
 */


struct rpc_answer_t *marshal_udp_deliver(struct lmp_recv_msg *msg, struct lmp_chan *lmp) {
    errval_t err;

    uint32_t ip_src = (uint32_t) msg->words[1];
    uint16_t port_src = (uint16_t) (msg->words[2] >> 16);
    uint16_t port_dst = (uint16_t) (msg->words[2] & 0xffff);
    uint16_t this_length = (uint16_t) (msg->words[3] >> 16);
    uint16_t remaining = (uint16_t) (msg->words[3] & 0xffff);
    uint8_t *data = (uint8_t *)&msg->words[4];
    debug_printf("Network delivery from %d.%d.%d.%d:%u to port %u length %u remaining %u:",
                 *((uint8_t*)&ip_src + 3), *((uint8_t*)&ip_src + 2), *((uint8_t*)&ip_src + 1), *(uint8_t*)&ip_src,
                 port_src, port_dst, this_length, remaining);
    debug_print_packet(data, this_length);

    err = NET_ERR_OK;

    if (remaining == 0) {
        pending_reply = true;
    }
    // assemble the response
    struct rpc_answer_t *ans = malloc(sizeof(struct rpc_answer_t));
    ans->sender_lmp = lmp;
    ans->err = err;

    return ans;
}


errval_t lmp_send_handler(void *arg) {
    struct rpc_answer_t *answer = (struct rpc_answer_t *)arg;
    struct lmp_chan* lmp = answer->sender_lmp;
    errval_t err = lmp_chan_send2(lmp, LMP_FLAG_SYNC, NULL_CAP, AOS_RPC_ID_ACK, (uintptr_t)(answer->err));
    if (err_is_ok(err)) {
        return err;
    }

    if (send_retry_count++ < AOS_RPC_ATTEMPTS) {
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


//! for debugging
void debug_print_packet(uint8_t *data, size_t size) {
#define PRINTABLE(c) (isprint(c) ? (c) : '.')
    int i;
    for (i = 0; i < size / 8; ++i) {
        debug_printf(
                "0x%06x: %02x %02x %02x %02x %02x %02x %02x %02x  %c%c%c%c%c%c%c%c\n",
                i * 8,
                data[i*8+0], data[i*8+1], data[i*8+2], data[i*8+3],
                data[i*8+4], data[i*8+5], data[i*8+6], data[i*8+7],
                PRINTABLE(data[i*8+0]), PRINTABLE(data[i*8+1]),
                PRINTABLE(data[i*8+2]), PRINTABLE(data[i*8+3]),
                PRINTABLE(data[i*8+4]), PRINTABLE(data[i*8+5]),
                PRINTABLE(data[i*8+6]), PRINTABLE(data[i*8+7])
        );
    }

    if (size % 8 != 0) {
        char buf[60] = {0};
        sprintf(buf, "0x%06x:", i * 8);
        int j;
        for (j = 0; j < size % 8; ++j) {
            sprintf(buf + strlen(buf), " %02x", data[i * 8 + j]);
        }
        for (; j < 8; ++j) {
            sprintf(buf + strlen(buf), "   ");
        }
        sprintf(buf + strlen(buf), "  ");
        for (j = 0; j < size % 8; ++j) {
            sprintf(buf + strlen(buf), "%c", PRINTABLE(data[i * 8 + j]));
        }
        debug_printf("%s\n", buf);
    }

    debug_printf("\n");
}