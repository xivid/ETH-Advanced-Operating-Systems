#ifndef _INIT_TERMINAL_H
#define _INIT_TERMINAL_H

#include <aos/inthandler.h>

typedef void (*char_received_fn)(void *, char);

errval_t register_getchar_interrupt_handler(void);
void register_receiver_fn(char_received_fn handler, void *arg);

#endif /* _INIT_TERMINAL_H */
