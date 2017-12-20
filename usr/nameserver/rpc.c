#include "rpc.h"
#include "whois.h"

struct aos_rpc *listen_rpc = NULL;
errval_t ns_init_rpc(void)
{
    errval_t err;
    // Set up the listening channel.
    listen_rpc = malloc(sizeof(struct aos_rpc));
    err = lmp_chan_accept(&listen_rpc->lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_alloc_recv_slot(&listen_rpc->lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(&listen_rpc->lmp, get_default_waitset(),
            MKCLOSURE((void*) ns_recv_listener_handler, &listen_rpc->lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/nameserver/rpc.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    // Send a registration message to init.
    err = ns_send_register();
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return 1;
    }

    /* // Send the endpoint to the listening channel to init. */
    err = ns_send_listen_cap();
    if (err_is_fail(err)) {
        debug_printf("send nameserver endpoint failed\n");
        return 1;
    }

    debug_printf("nameserver done setting rpc\n");
    return SYS_ERR_OK;
}

errval_t ns_send_register(void) {
    struct aos_rpc *rpc = get_init_rpc();
    // it's ok to use stack variables here because the calls are made blocking
    uintptr_t args[MAX_LMP_ARGS];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &rpc->lmp.local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_REGISTER_NAMESERVER_WITH_INIT;

    errval_t err = send(args, ns_send_handler, ns_recv_ack_handler);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t ns_send_listen_cap(void) {
    struct aos_rpc *rpc = get_init_rpc();
    uintptr_t args[MAX_LMP_ARGS];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &listen_rpc->lmp.local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_SET_NAMESERVER_EP;

    errval_t err = send(args, ns_send_handler, ns_recv_ack_handler);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

void *ns_marshal_init(struct capref cap) {
    struct aos_rpc *client_rpc;
    unsigned id;
    int res = ns_add_client(cap, &client_rpc, &id);
    if (res) {
        debug_printf("nameserver failed adding a client!\n");
        return NULL;
    }

    uintptr_t *answer_args = malloc(sizeof(uintptr_t) * MAX_LMP_ARGS);
    answer_args[0] = (uintptr_t) client_rpc;
    answer_args[1] = (uintptr_t) 0;
    answer_args[2] = AOS_RPC_ID_ACK;
    answer_args[3] = (uintptr_t) id;
    return answer_args;
}
