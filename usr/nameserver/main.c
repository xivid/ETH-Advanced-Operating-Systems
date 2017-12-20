#include <aos/nameserver.h>
#include <stdio.h>
#include "rpc.h"

int main(int argc, char *argv[])
{
    errval_t err;
    debug_printf("nameserver launched\n");
    err = ns_init_rpc();
    if (err_is_fail(err)) {
        debug_printf("nameserver failed init_rpc\n");
        return 1;
    }

    int *tmp = malloc(1000);
    debug_printf("malloc succeeded %p\n", tmp);

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
