/**
 * \file
 * \brief Handles everything related to character input and output
 */

#ifndef _INIT_TERMINAL_H
#define _INIT_TERMINAL_H

#include <aos/inthandler.h>

#define SERIAL_GETCHAR_IRQ 106
#define CHAR_BUFFER_LENGTH 1024

int current_read_index;
char read_char_buffer[CHAR_BUFFER_LENGTH]; // circular character buffer

errval_t register_getchar_interrupt_handler(void);
char get_next_char_from_buffer(void);

char get_next_char_from_buffer(void) {
    char current = read_char_buffer[current_read_index];
    if (current != '\0') {
        current_read_index = (current_read_index + 1) % CHAR_BUFFER_LENGTH;
    }
    return current;
}

static void print_handler(void *handler_arg) {
    assert(handler_arg == NULL);
    char new_char;
    sys_getchar(&new_char);

    int insertion_index = current_read_index;
    while (read_char_buffer[insertion_index] != '\0')
    {
        insertion_index = (insertion_index + 1) % CHAR_BUFFER_LENGTH;
    }
    read_char_buffer[insertion_index] = new_char;
    read_char_buffer[(insertion_index + 1) % CHAR_BUFFER_LENGTH] = '\0';
}

errval_t register_getchar_interrupt_handler(void) {
    current_read_index = 0;
    read_char_buffer[current_read_index] = '\0';
    errval_t err = inthandler_setup_arm(print_handler, NULL, SERIAL_GETCHAR_IRQ);
    if (err_is_fail(err)) {
        debug_printf("Failed to set the interrupt handler\n");
        return err;
    }
    return SYS_ERR_OK;
}


#endif /* _INIT_TERMINAL_H */
