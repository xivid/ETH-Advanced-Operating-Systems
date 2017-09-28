/*
 * Reference implementation of AOS milestone 0, on the Pandaboard.
 */

/*
 * Copyright (c) 2009-2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <kernel.h>

#include <assert.h>
#include <bitmacros.h>
#include <maps/omap44xx_map.h>
#include <paging_kernel_arch.h>
#include <platform.h>
#include <serial.h>

#define MSG(format, ...) printk( LOG_NOTE, "OMAP44xx: "format, ## __VA_ARGS__ )

void blink_leds(void);

/* RAM starts at 2G (2 ** 31) on the Pandaboard */
lpaddr_t phys_memory_start= GEN_ADDR(31);

/*** Serial port ***/

unsigned serial_console_port= 2;

errval_t
serial_init(unsigned port, bool initialize_hw) {
    /* XXX - You'll need to implement this, but it's safe to ignore the
     * parameters. */

    return SYS_ERR_OK;
};

void
serial_putchar(unsigned port, char c) {
    /* XXX - You'll need to implement this, but it's safe to ignore the
     * port parameter. */
    // define physical addresses
    // address to check if we can write
    volatile char * TX_FIFO_E = (char*)0x48020014;
    // address to write characters to
    volatile char * UART_THR = (char*)0x48020000;
    
    // check if we can continue and write (at offset 5)
    while (!((*TX_FIFO_E) & (1 << 5)));
    // write char
    *UART_THR = c;
}

__attribute__((noreturn))
char
serial_getchar(unsigned port) {
    /* XXX - You only need to implement this if you're going for the extension
     * component. */

    panic("Unimplemented.\n");
}

/*** LED flashing ***/

__attribute__((noreturn))
void
blink_leds(void) {
    /* XXX - You'll need to implement this. */
    volatile int * GPIO_OE = (int*)0x4a310134;
    //wk8 has offset of 8
    int mask = 1<<8;
    // enable output on led bit by setting the according Output enable to 0
    *GPIO_OE &= ~mask;
    volatile int * Dataout = (int*)0x4a31013C;
    while(1){
        // pass time
        int a = 0;
        // upper limit is found experimentally
        for (int i = 0; i < 120000000; i++) {
            a++;
        }
        // inverse the value at the mask bit in dataout
        * Dataout ^= mask;
    }
}
