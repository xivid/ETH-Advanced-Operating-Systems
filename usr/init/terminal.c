/**
 * \file
 * \brief Handles everything related to character input and output
 */

#include <aos/aos.h>

#include "terminal.h"


#define SERIAL_GETCHAR_IRQ 106
#define CHAR_BUFFER_LENGTH 1024

void send_response(void);
char get_next_char_from_buffer(void);

int current_read_index;
char read_char_buffer[CHAR_BUFFER_LENGTH]; // circular character buffer

int transmit_max;
char_received_fn callback;
void *callback_arg;

char get_next_char_from_buffer(void)
{
    char current = read_char_buffer[current_read_index];
    if (current != '\0')
    {
        current_read_index = (current_read_index + 1) % CHAR_BUFFER_LENGTH;
    }
    return current;
}

void send_response(void)
{
    if (transmit_max != current_read_index && callback != NULL) {
        callback(callback_arg, get_next_char_from_buffer());
        callback = NULL;
        callback_arg = NULL;
    }
}

void register_receiver_fn(char_received_fn handler, void *arg)
{
    callback = handler;
    callback_arg = arg;
    send_response();
}

static void print_handler(void *handler_arg)
{
    assert(handler_arg == NULL);
    char new_char;
    sys_getchar(&new_char);

    int insertion_index = current_read_index;
    while (read_char_buffer[insertion_index] != '\0')
    {
        insertion_index = (insertion_index + 1) % CHAR_BUFFER_LENGTH;
    }
    int next_index = (insertion_index + 1) % CHAR_BUFFER_LENGTH;

    if (new_char == '\r') {
        new_char = '\n';
        transmit_max = next_index;
    } else if (new_char == 127) {
        if (insertion_index > current_read_index) {
            int prev = (insertion_index - 1 + CHAR_BUFFER_LENGTH) % CHAR_BUFFER_LENGTH;
            read_char_buffer[prev] = '\0';
            const char* delete_back = "\b \b";
            sys_print(delete_back, 3);
        }
        return;
    }
    read_char_buffer[insertion_index] = new_char;
    read_char_buffer[next_index] = '\0';

    sys_print(&new_char, 1);
    send_response();
}

errval_t register_getchar_interrupt_handler(void)
{
    current_read_index = 0;
    read_char_buffer[current_read_index] = '\0';
    transmit_max = 0;
    callback = NULL;
    callback_arg = NULL;
    errval_t err = inthandler_setup_arm(print_handler, NULL, SERIAL_GETCHAR_IRQ);
    if (err_is_fail(err))
    {
        debug_printf("Failed to set the interrupt handler\n");
        return err;
    }
    return SYS_ERR_OK;
}
