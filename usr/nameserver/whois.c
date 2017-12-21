#include "whois.h"

struct ns_client_t **client_array = NULL;
unsigned client_count = 0;
unsigned client_array_size = 0;

struct ns_client_t *ns_find_client(unsigned id)
{
    if (id >= client_count) {
        return NULL;
    }
    return client_array[id];
}

int ns_add_client(struct capref cap, struct aos_rpc **rpc, unsigned *id)
{
    struct ns_client_t *client = malloc(sizeof(struct ns_client_t));
    client->rpc = malloc(sizeof(struct aos_rpc));
    struct aos_rpc *client_rpc = client->rpc;
    struct lmp_chan *client_lmp = &client_rpc->lmp;

    errval_t err = lmp_chan_accept(client_lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        debug_printf("lmp chan accept failed\n");
        return 1;
    }
    err = lmp_chan_alloc_recv_slot(client_lmp);
    if (err_is_fail(err)) {
        debug_printf("can't alloc slot on lmp\n");
        return 1;
    }

    if (client_count >= client_array_size) {
        unsigned new_size = 2*client_array_size + 1;
        if (client_array == NULL) {
            client_array = malloc(new_size * sizeof(struct ns_client_t *));
        } else {
            client_array = realloc(client_array,
                    new_size * sizeof(struct ns_client_t *));
            for (unsigned i = client_array_size ; i < new_size; ++i) {
                client_array[i] = NULL;
            }
        }
        debug_printf("nameserver: extended client array from %u to %u\n",
                client_array_size, new_size);
        client_array_size = new_size;
        *id = client_count;
    } else {
        for (unsigned i = 0 ; i < client_array_size ; ++i) {
            if (client_array[i] == NULL) {
                *id = i;
                break;
            }
        }
    }

    client_array[*id] = client;
    ++client_count;
    *rpc = client_rpc;
    return 0;
}
