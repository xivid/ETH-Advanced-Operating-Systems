/**
 * \file
 * \brief Networking process
 */

#include <stdio.h>
#include <stdlib.h>
#include "rpc_services.h"

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/nameserver.h>
#include <aos/waitset.h>
#include <aos/paging.h>
#include <netutil/user_serial.h>
#include <maps/omap44xx_map.h>

struct aos_rpc *listen_rpc = NULL;
struct client *client_list = NULL;
int network_rpc_send_retry_count = 0;
struct udp_server *udp_server_list = NULL;


int main(int argc, char *argv[])
{
    debug_printf("Networking process started!\n");
    struct aos_rpc *init_rpc = aos_rpc_get_init_channel();
    errval_t err;

    //! step 1
    debug_printf("Get uart4 device frame\n");
    struct capref cap_uart4;
    err = aos_rpc_get_device_cap(init_rpc, OMAP44XX_MAP_L4_PER_UART4, OMAP44XX_MAP_L4_PER_UART4_SIZE, &cap_uart4);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cannot get device cap uart4");
    }

    //! step 2
    debug_printf("Mapping device frame\n");
    void *uart4_vbase;
    err = paging_map_frame_attr(get_current_paging_state(), &uart4_vbase, OMAP44XX_MAP_L4_PER_UART4_SIZE, cap_uart4, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cannot map mem_uart4 to my own space");
        return EXIT_FAILURE;
    }

    //! step 3
    debug_printf("Initing serial\n");
    serial_init((lvaddr_t) uart4_vbase, UART4_IRQ);

    //! step 4
    debug_printf("Testing greeting to host by serial\n");
    slip_send((uint8_t *)"hello host!\r\n", 13);

    //! step 5 - set up my own rpc server
    debug_printf("Setting up a open-receiving rpc channel\n");
    err = init_rpc_server();
    if (err_is_fail(err)) {
        debug_printf("Failed setting up a listening rpc channel\n");
        return EXIT_FAILURE;
    }

    //! step 6 - connect to nameserver
    debug_printf("Setting up rpc channel to nameserver\n");
    ns_err_names_t ns_err;
    do {
        err = ns_init_channel(&ns_err);
    } while (err != SYS_ERR_OK || ns_err != NS_ERR_OK);

    //! step 7 - register a service to nameserver
    debug_printf("Registering networking service to name server\n");
    err = ns_register("network", listen_rpc->lmp.local_cap, &ns_err);
    if (err_is_fail(err) || ns_err != NS_ERR_OK) {
        DEBUG_ERR(err, "cannot register myself to name server as \"networking\"");
        return EXIT_FAILURE;
    }

    //! step 8 - listen for events
    debug_printf("Networking enters message handler loop\n");
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

void serial_input(uint8_t *buf, size_t len)
{
    slip_receive(buf, len);
}
