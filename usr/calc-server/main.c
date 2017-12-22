/**
 * \file
 * \brief Hello world application
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
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/nameserver.h>
#include <aos/waitset.h>
#include <aos/except.h>
#include <time.h>

struct aos_rpc *listen_rpc = NULL;

errval_t set_up_listen_channel(void);
errval_t calc_recv_handler(void *v_args);
errval_t calc_send_handler(void *v_args);
void *calc_marhsal_input(struct lmp_recv_msg *msg, struct lmp_chan *lmp);

int main(int argc, char *argv[])
{
    errval_t err;
    ns_err_names_t ns_err;
    debug_printf("calc-server started\n");
    err = set_up_listen_channel();
    if (err_is_fail(err)) {
        debug_printf("calc_server failed setting up a listening channel\n");
        return 1;
    }
    do {
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);
    /* debug_printf("calc_server inited a channel to nameserver\n"); */

    err = ns_register("calc_server", listen_rpc->lmp.local_cap, &ns_err);
    if (err_is_fail(err) || ns_err != NS_ERR_OK) {
        debug_printf("calc_server got an error while registering\n");
        return 1;
    }
    /* debug_printf("calc_server registered\n"); */

    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            debug_printf("error in event dispatch in nameserver\n");
            return 1;
        }
    }

    return 0;
}

void *calc_marhsal_input(struct lmp_recv_msg *msg, struct lmp_chan *lmp)
{
    char *str = (char *) &msg->words[1];
    // dummy calculator:
    int a = atoi(str), sign = 0, sign_pos = 0, b, c;
    for (int i = 0 ; i < strlen(str) ; ++i) {
        if (str[i] == '+' || str[i] == '*' || str[i] == '/' || str[i] == '-') {
            sign = str[i];
            sign_pos = i;
            break;
        }
    }
    b = atoi(&str[sign_pos + 1]);
    switch (sign) {
        case '+':
            c = a + b;
            break;
        case '-':
            c = a - b;
            break;
        case '*':
            c = a * b;
            break;
        case '/':
            c = a / b;
            break;
        default:
            c = 0;
    }

    debug_printf("result is: %d\n", c);
    uintptr_t *answer_args = malloc(3*sizeof(uintptr_t));
    answer_args[0] = (uintptr_t) lmp;
    answer_args[1] = AOS_RPC_ID_ACK;
    answer_args[2] = c;
    return (void *) answer_args;
}

errval_t calc_recv_handler(void *v_args)
{
    struct capref cap;
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    struct lmp_chan *lmp = &listen_rpc->lmp;
    errval_t err = lmp_chan_recv(lmp, &lmp_msg, &cap);
    int count = 0;
    while (count < AOS_RPC_ATTEMPTS &&
            lmp_err_is_transient(err) && err_is_fail(err)) {
        err = lmp_chan_recv(lmp, &lmp_msg, &cap);
        count++;
    }
    if (err_is_fail(err)) {
        debug_printf("calc_recv_handler: too many failed attempts\n");
        return err;
    }

    struct lmp_chan *client_lmp = malloc(sizeof(struct lmp_chan));
    err = lmp_chan_accept(client_lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        debug_printf("calc_recv_handler: lmp chan accept failed\n");
        return err;
    }

    // reregister
    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/calc_server/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
            MKCLOSURE((void*) calc_recv_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/calc_server/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    uintptr_t *answer_args;
    switch (lmp_msg.words[0]) {
        case AOS_RPC_ID_STR:
            answer_args = calc_marhsal_input(&lmp_msg, client_lmp);
            break;
        default:
            debug_printf("unknown message sent to calc_server: %d\n",
                    lmp_msg.words[0]);
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    if (answer_args == NULL) {
        debug_printf("calc_server: answer_args is NULL\n");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_send(client_lmp, get_default_waitset(),
            MKCLOSURE((void *) calc_send_handler, answer_args));
    if (err_is_fail(err)) {
        debug_printf("calc_recv_handler: can't register send\n");
        return err;
    }

    return SYS_ERR_OK;
}

errval_t calc_send_handler(void *v_args)
{
    uintptr_t *args = (uintptr_t *) v_args;
    struct lmp_chan *lmp = (struct lmp_chan *) args[0];

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send2(lmp, LMP_FLAG_SYNC, NULL_CAP, args[1], args[2]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        debug_printf("got an error: %s\n", err_getstring(err));
        count++;
    }
    debug_printf("calc_send_handler: too many failed attempts\n");
    return err;
}

errval_t set_up_listen_channel(void)
{
    errval_t err;
    // Set up the listening channel.
    listen_rpc = malloc(sizeof(struct aos_rpc));
    err = lmp_chan_accept(&listen_rpc->lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/calc-server/main.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/calc_server/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
            MKCLOSURE((void*) calc_recv_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/calc_server/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }
    return SYS_ERR_OK;
}
