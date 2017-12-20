#ifndef _INIT_TERMINAL_H
#define _INIT_TERMINAL_H

#include <aos/inthandler.h>

errval_t register_getchar_interrupt_handler(void);
char get_next_char_from_buffer(void);

#endif /* _INIT_TERMINAL_H */
