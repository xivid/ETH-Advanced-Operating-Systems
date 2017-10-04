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

static volatile char     *UART_THR_ADDRESS = (char*)     0x48020000;
static volatile uint32_t *UART_LSR_ADDRESS = (uint32_t*) 0x48020014;

bool buffer_is_empty(void);
void write_one_char(char c);


/* RAM starts at 2G (2 ** 31) on the Pandaboard */
lpaddr_t phys_memory_start= GEN_ADDR(31);

/*** Serial port ***/

unsigned serial_console_port= 2;

bool
buffer_is_empty(void) {
    return (bool) ((1<<5) & (*UART_LSR_ADDRESS));
};

void
write_one_char(char c) {
    *UART_THR_ADDRESS = c;
};

errval_t
serial_init(unsigned port, bool initialize_hw) {
    /* XXX - You'll need to implement this, but it's safe to ignore the
     * parameters. */

    return SYS_ERR_OK;
};

void
serial_putchar(unsigned port, char c) {
    while (!buffer_is_empty()) {}; /* Wait until the buffer is empty.*/
    write_one_char(c);
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
    while(1);
}
