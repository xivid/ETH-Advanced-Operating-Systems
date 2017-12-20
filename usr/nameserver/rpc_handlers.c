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

errval_t ns_recv_ack_handler(void *v_args)
{
    struct lmp_recv_msg lmp_msg = LMP_RECV_MSG_INIT;
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[0];
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

errval_t ns_send_handler(void *v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[0];
    struct lmp_chan *lmp = &rpc->lmp;
    struct capref cap = *((struct capref *) args[1]);

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, cap, args[2]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        debug_printf("error: %s\n", err_getstring(err));
        count++;
    }
    debug_printf("ns_send_handler_with_cap: too many failed attempts\n");
    return err;
}
