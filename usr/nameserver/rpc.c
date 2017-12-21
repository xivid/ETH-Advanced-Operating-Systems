#include "rpc.h"
#include "whois.h"
#include "names.h"

uintptr_t *ns_receive_large_string(struct lmp_recv_msg *msg, bool *last_message,
        char **name);

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
    args[RPC_SLOT] = (uintptr_t) rpc;
    args[CAP_SLOT] = (uintptr_t) &rpc->lmp.local_cap;
    args[TYPE_SLOT] = (uintptr_t) AOS_RPC_ID_REGISTER_NAMESERVER_WITH_INIT;

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
    args[RPC_SLOT] = (uintptr_t) rpc;
    args[CAP_SLOT] = (uintptr_t) &listen_rpc->lmp.local_cap;
    args[TYPE_SLOT] = (uintptr_t) AOS_RPC_ID_SET_NAMESERVER_EP;

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
    answer_args[RPC_SLOT] = (uintptr_t) client_rpc;
    answer_args[CAP_SLOT] = (uintptr_t) 0;
    answer_args[TYPE_SLOT] = AOS_RPC_ID_ACK;
    answer_args[ERR_ID_SLOT] = (uintptr_t) id;
    return answer_args;
}

uintptr_t *ns_receive_large_string(struct lmp_recv_msg *msg, bool *last_message,
        char **name)
{
    // words for first msg: msg_type, client_id, len, string
    // words for other msg: msg_type, client_id, string
    *last_message = false;
    unsigned id = (unsigned) msg->words[1];
    struct ns_client_t *client = ns_find_client(id);
    if (!client) {
        debug_printf("ns_marshal_register error: client not found\n");
        return NULL;
    }

    struct aos_rpc *client_rpc = client->rpc;
    unsigned start_slot = 2;
    unsigned bytes_to_read = 7 * sizeof(uintptr_t);
    if (!client->receiving) {
        // this is the first message
        client->receiving = true;
        client->cur_received = 0;
        client->cur_data_len = (unsigned) msg->words[2];
        client->cur_buffer = calloc(msg->words[2], sizeof(char));
        start_slot += 1;
        bytes_to_read -= sizeof(uintptr_t);
    }

    strncpy(&client->cur_buffer[client->cur_received],
            (char *) &msg->words[start_slot], bytes_to_read);

    client->cur_received += bytes_to_read;

    ns_err_names_t ns_err = NS_ERR_OK;
    if (client->cur_received >= client->cur_data_len) {
        // the entire message was received
        *last_message = true;
        *name = client->cur_buffer;
        client->cur_buffer = NULL;
        client->receiving = false;
    }

    uintptr_t *answer_args = malloc(sizeof(uintptr_t) * MAX_LMP_ARGS);
    answer_args[RPC_SLOT] = (uintptr_t) client_rpc;
    answer_args[CAP_SLOT] = (uintptr_t) 0;
    answer_args[TYPE_SLOT] = AOS_RPC_ID_ACK;
    answer_args[ERR_ID_SLOT] = (uintptr_t) ns_err;
    return answer_args;
}

void *ns_marshal_register(struct capref endpoint, struct lmp_recv_msg *msg)
{
    bool last_message = false;
    char *name = NULL;
    uintptr_t *answer_args = ns_receive_large_string(msg, &last_message, &name);
    if (last_message) {
        ns_err_names_t ns_err = ns_add_record(name, endpoint);
        answer_args[ERR_ID_SLOT] = (uintptr_t) ns_err;
    }
    return (void *) answer_args;

}

void *ns_marshal_lookup(struct lmp_recv_msg *msg)
{
    bool last_message = false;
    char *name = NULL;
    uintptr_t *answer_args = ns_receive_large_string(msg, &last_message, &name);
    if (last_message) {
        struct capref *cap = ns_find_record(name);
        if (cap == NULL) {
            answer_args[ERR_ID_SLOT] = (uintptr_t) NS_ERR_NAME_NOT_FOUND;
            answer_args[CAP_SLOT] = (uintptr_t) 0;
        } else {
            answer_args[CAP_SLOT] = (uintptr_t) cap;
        }
    }
    return (void *) answer_args;

}
