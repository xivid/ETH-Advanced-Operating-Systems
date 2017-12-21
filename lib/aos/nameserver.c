#include <aos/nameserver.h>

struct capref ns_endpoint;
struct aos_rpc *ns_rpc = NULL;
unsigned ns_client_id;

// TODO: add an ns_err parameter for cases when nameserver is not running, etc.
errval_t ns_init_channel(void)
{
    errval_t err = aos_rpc_get_nameserver_ep(get_init_rpc(), &ns_endpoint);
    if (err_is_fail(err)) {
        debug_printf("failed getting nameserver endpoint: %s\n");
        return err;
    }
    ns_rpc = (struct aos_rpc *) malloc(sizeof(struct aos_rpc));
    err = aos_rpc_nameserver_syn(ns_rpc, ns_endpoint, &ns_client_id);
    return err;
}

errval_t ns_register(char *name, struct capref endpoint, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel();
        if (err_is_fail(err)) {
            debug_printf("ns_register failed: error in ns_init_channel\n");
            return err;
        }
    }

    err = aos_rpc_nameserver_register(ns_rpc, ns_client_id, endpoint,
            name, ns_err);
    return err;
}

errval_t ns_lookup(char *name, struct capref *endpoint, ns_err_names_t *ns_err)
{
    errval_t err;
    if (ns_rpc == NULL) {
        err = ns_init_channel();
        if (err_is_fail(err)) {
            debug_printf("ns_lookup failed: error in ns_init_channel\n");
            return err;
        }
    }

    err = aos_rpc_nameserver_lookup(ns_rpc, ns_client_id, endpoint, name,
            ns_err);
    return err;
}

// TODO: lookup
// TODO: enum
// TODO: deregister
// TODO: deregister dead



