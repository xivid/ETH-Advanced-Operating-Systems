#include <aos/nameserver.h>

struct capref ns_endpoint;
struct aos_rpc *ns_rpc = NULL;
unsigned ns_client_id;

errval_t ns_init_channel(ns_err_names_t *ns_err)
{
    errval_t err = aos_rpc_get_nameserver_ep(get_init_rpc(), &ns_endpoint,
            ns_err);
    /* debug_printf("got nameserver's endpoint\n"); */
    if (err_is_fail(err)) {
        debug_printf("failed getting nameserver endpoint: %s\n");
        return err;
    }
    if (*ns_err != NS_ERR_OK) {
        return SYS_ERR_OK;
    }
    ns_rpc = (struct aos_rpc *) malloc(sizeof(struct aos_rpc));
    err = aos_rpc_nameserver_syn(ns_rpc, ns_endpoint, &ns_client_id);
    return err;
}

errval_t ns_register(char *name, struct capref endpoint, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel(ns_err);
        if (err_is_fail(err) || *ns_err != NS_ERR_OK) {
            debug_printf("ns_register failed: error in ns_init_channel\n");
            return err;
        }
    }

    err = aos_rpc_nameserver_register(ns_rpc, ns_client_id, endpoint,
            name, ns_err);
    return err;
}

errval_t ns_deregister(char *name, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel(ns_err);
        if (err_is_fail(err) || *ns_err != NS_ERR_OK) {
            debug_printf("ns_register failed: error in ns_init_channel\n");
            return err;
        }
    }

    err = aos_rpc_nameserver_deregister(ns_rpc, ns_client_id, name, ns_err);
    return err;
}

errval_t ns_lookup(char *name, struct capref *endpoint, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel(ns_err);
        if (err_is_fail(err) || *ns_err != NS_ERR_OK) {
            debug_printf("ns_lookup failed: error in ns_init_channel\n");
            return err;
        }
    }

    err = aos_rpc_nameserver_lookup(ns_rpc, ns_client_id, endpoint, name,
            ns_err);
    return err;
}

errval_t ns_enum(char **result, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel(ns_err);
        if (err_is_fail(err) || *ns_err != NS_ERR_OK) {
            debug_printf("ns_enum failed: error in ns_init_channel\n");
            return err;
        }
    }
    err = aos_rpc_nameserver_enum(ns_rpc, ns_client_id, result, ns_err);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}
// TODO: deregister
// TODO: deregister dead



