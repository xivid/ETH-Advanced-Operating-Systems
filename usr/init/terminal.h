/**
 * \file
 * \brief Handles everything related to character input and output
 */

#ifndef _INIT_TERMINAL_H
#define _INIT_TERMINAL_H

#include <aos/inthandler.h>

#define SERIAL_GETCHAR_IRQ 106

// TODO: buffer recieved chars

errval_t register_getchar_interrupt_handler(void);


static void print_handler(void *handler_arg) {
    assert(handler_arg == NULL);
    debug_printf("Received interrupt for getChar:\n");
    char new_char;
    scanf("%c", &new_char); // I hope this works
    debug_printf("Received char %c", new_char);
}

errval_t register_getchar_interrupt_handler(void) {
    errval_t err = inthandler_setup_arm(print_handler, NULL, SERIAL_GETCHAR_IRQ);
    if (err_is_fail(err)) {
        debug_printf("Failed to set the interrupt handler\n");
        return err;
    }
    return SYS_ERR_OK;
}


#endif /* _INIT_TERMINAL_H */
