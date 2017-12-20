/**
 * \file
 * \brief Networking process
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/paging.h>
#include <netutil/user_serial.h>
#include <maps/omap44xx_map.h>

int main(int argc, char *argv[])
{
    debug_printf("Networking process started!\n");
    struct aos_rpc *init_rpc = aos_rpc_get_init_channel();
    errval_t err;


    debug_printf("Get uart4 device frame\n");
    struct capref cap_uart4;
    err = aos_rpc_get_device_cap(init_rpc, OMAP44XX_MAP_L4_PER_UART4, OMAP44XX_MAP_L4_PER_UART4_SIZE, &cap_uart4);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cannot get device cap uart4");
    }


    debug_printf("Mapping device frame\n");
    void *uart4_vbase;
    err = paging_map_frame_attr(get_current_paging_state(), &uart4_vbase, OMAP44XX_MAP_L4_PER_UART4_SIZE, cap_uart4, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cannot map mem_uart4 to my own space");
        return EXIT_FAILURE;
    }


    debug_printf("Initing serial\n");
    serial_init((lvaddr_t) uart4_vbase, UART4_IRQ);


    debug_printf("Writing to serial\n");
    serial_write((uint8_t*) "hello!\r\n", 8);

    return 0;
}
