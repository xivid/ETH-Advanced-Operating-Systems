//
// Created by Zhifei Yang on 2017/12/20.
//

#ifndef BF_AOS_SLIP_H
#define BF_AOS_SLIP_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <netutil/user_serial.h>


#define SLIP_DATAGRAM_MAX_BYTES 1006
struct slip_datagram_t {
    uint8_t data[SLIP_DATAGRAM_MAX_BYTES];
    uint16_t size;
};
#define EMPTY_SLIP_DATAGRAM (struct slip_datagram_t) { \
                                .data = {}, \
                                .size = 0, \
                            }

void slip_input(uint8_t *buf, size_t len);
void debug_print_slip_datagram(struct slip_datagram_t *datagram);
void slip_datagram_recv_handler(void);
void slip_datagram_send(struct slip_datagram_t *datagram);

#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335
#define SLIP_ESC_NUL 0336

struct slip_datagram_t slip_datagram;
bool slip_escaping;

#endif //BF_AOS_SLIP_H
