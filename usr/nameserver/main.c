#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

errval_t init_rpc(void);
errval_t send(void *args);
errval_t recv_handler(void *args);
errval_t snd_handler(void *args);
errval_t send_register(void);
errval_t send_ns_listen_cap(void);

struct lmp_chan *listen_lmp;

errval_t init_rpc(void)
{

    errval_t err;
    listen_lmp = (struct lmp_chan *) malloc(sizeof(struct lmp_chan));
    err = lmp_chan_accept(listen_lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_alloc_recv_slot(listen_lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }

    err = lmp_chan_register_recv(listen_lmp, get_default_waitset(),
            MKCLOSURE((void*) recv_handler, listen_lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }

    debug_printf("done setting rpc\n");
    return SYS_ERR_OK;
}

errval_t recv_handler(void *args)
{
    debug_printf("nameserver recv_handler entered\n");
    return SYS_ERR_OK;
}

errval_t snd_handler(void *v_args)
{
    uintptr_t *args = (uintptr_t*) v_args;
    struct lmp_chan *lmp = (struct lmp_chan*) args[0];
    struct capref cap = *((struct capref *) args[1]);

    int count = 0;
    errval_t err;
    while (count < AOS_RPC_ATTEMPTS) {
        err = lmp_chan_send9(lmp, LMP_FLAG_SYNC, cap, args[2], args[3],
                args[4], args[5], args[6], args[7], args[8], args[9], args[10]);
        if (!err_is_fail(err))
            return SYS_ERR_OK;
        debug_printf("error: %s\n", err_getstring(err));
        count++;
    }
    debug_printf("snd_handler: too many failed attempts\n");
    return err;
}

errval_t send(void *v_args)
{
    uintptr_t *args = (uintptr_t *) v_args;
    struct aos_rpc *rpc = (struct aos_rpc *) args[0];
    errval_t err = lmp_chan_register_send(&rpc->lmp, rpc->ws,
            MKCLOSURE((void *)snd_handler, args));
    if (err_is_fail(err)) {
        debug_printf("send: lmp_chan_register_send failed\n");
        return err;
    }

    err = lmp_chan_register_recv(&rpc->lmp, rpc->ws,
            MKCLOSURE((void *) recv_handler, args));
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

errval_t send_register(void) {
    struct aos_rpc *rpc = get_init_rpc();
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &rpc->lmp.local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_REGISTER_NAMESERVER;

    errval_t err = send(args);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t send_ns_listen_cap(void) {
    struct aos_rpc *rpc = get_init_rpc();
    uintptr_t args[LMP_ARGS_SIZE + 1];
    args[0] = (uintptr_t) rpc;
    args[1] = (uintptr_t) &listen_lmp->local_cap;
    args[2] = (uintptr_t) AOS_RPC_ID_SET_NAMESERVER_EP;

    errval_t err = send(args);
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return err;
    }
    return SYS_ERR_OK;
}

int main(int argc, char *argv[])
{
    errval_t err;
    /* char str[1000]; */
    /* debug_print_capref(str, 1000, cap_selfep); */
    /* debug_printf("cap selfep in ns main: %s\n", str); */
    debug_printf("nameserver launched\n");
    err = init_rpc();
    if (err_is_fail(err)) {
        debug_printf("nameserver failed init_rpc\n");
        return 1;
    }

    err = send_register();
    if (err_is_fail(err)) {
        debug_printf("send register failed\n");
        return 1;
    }

    err = send_ns_listen_cap();
    if (err_is_fail(err)) {
        debug_printf("send nameserver endpoint failed\n");
        return 1;
    }

    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        debug_printf("event dispatched\n");
        if (err_is_fail(err)) {
            debug_printf("error in event dispatch in nameserver\n");
            return 1;
        }
    }
    return 0;
}
