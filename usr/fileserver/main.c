//#include <aos/>
#include <stdio.h>
#include "rpc.h"

int main(int argc, char *argv[])
{
    errval_t err = SYS_ERR_OK;
    debug_printf("fileserver launched\n");
    //err = ns_init_rpc();
    if (err_is_fail(err)) {
        debug_printf("fileserver failed init_rpc\n");
        return 1;
    }

    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            debug_printf("error in event dispatch in fileserver\n");
            return 1;
        }
    }
}
