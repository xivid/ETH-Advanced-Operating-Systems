//
// Created by Zhifei Yang on 2017/12/20.
//

#include "slip.h"

void slip_input(uint8_t *buf, size_t len)
{
    // debug_printf("slip_input(%s, %lu)\n", buf, len);

    uint8_t new_char;
    for (int i = 0; i < len; ++i) {
        new_char = buf[i];
        if (slip_datagram.size < SLIP_DATAGRAM_MAX_BYTES) {
            switch (new_char) {
                case SLIP_ESC:
                    if (slip_escaping) {
                        debug_printf("Received SLIP_ESC while already in escaping mode! Dropped this byte.\n");
                        continue;
                    }
                    slip_escaping = true;
                    continue;
                case SLIP_END:
                    // received a packet, drop the END
                    slip_datagram_recv_handler();
                    slip_datagram = EMPTY_SLIP_DATAGRAM;
                    slip_escaping = false;
                    continue;
                case SLIP_ESC_END:
                    if (slip_escaping) {
                        new_char = SLIP_END;
                        slip_escaping = false;
                    }
                    break;
                case SLIP_ESC_ESC:
                    if (slip_escaping) {
                        new_char = SLIP_ESC;
                        slip_escaping = false;
                    }
                    break;
                case SLIP_ESC_NUL:
                    if (slip_escaping) {
                        new_char = 0;
                        slip_escaping = false;
                    }
                    break;
                case 0:
                    debug_printf("Received non-escaped NULL byte! Dropped this byte.\n");
                    continue;
                default:
                    if (slip_escaping) {
                        debug_printf("Received non-escaping byte while in escaping mode! Dropped this byte.\n");
                        continue;
                    }
            }
            slip_datagram.data[slip_datagram.size++] = new_char;
        }
        else {
            debug_printf("buffer full, dropped SLIP byte: [%c] (0x%2x)\n", new_char, new_char);
        }
    }

}

void debug_print_slip_datagram(struct slip_datagram_t *datagram) {
#define PRINTABLE(c) (isprint(c) ? (c) : '.')
    int i;
    for (i = 0; i < datagram->size / 8; ++i) {
        debug_printf(
                "0x%06x: %02x %02x %02x %02x %02x %02x %02x %02x  %c%c%c%c%c%c%c%c\n",
                i * 8,
                datagram->data[i*8+0], datagram->data[i*8+1], datagram->data[i*8+2], datagram->data[i*8+3],
                datagram->data[i*8+4], datagram->data[i*8+5], datagram->data[i*8+6], datagram->data[i*8+7],
                PRINTABLE(datagram->data[i*8+0]), PRINTABLE(datagram->data[i*8+1]),
                PRINTABLE(datagram->data[i*8+2]), PRINTABLE(datagram->data[i*8+3]),
                PRINTABLE(datagram->data[i*8+4]), PRINTABLE(datagram->data[i*8+5]),
                PRINTABLE(datagram->data[i*8+6]), PRINTABLE(datagram->data[i*8+7])
        );
    }

    if (datagram->size % 8 != 0) {
        char buf[60] = {0};
        sprintf(buf, "0x%06x:", i * 8);
        int j;
        for (j = 0; j < datagram->size % 8; ++j) {
            sprintf(buf + strlen(buf), " %02x", datagram->data[i * 8 + j]);
        }
        for (; j < 8; ++j) {
            sprintf(buf + strlen(buf), "   ");
        }
        sprintf(buf + strlen(buf), "  ");
        for (j = 0; j < datagram->size % 8; ++j) {
            sprintf(buf + strlen(buf), "%c", PRINTABLE(datagram->data[i * 8 + j]));
        }
        debug_printf("%s\n", buf);
    }
}

void slip_datagram_recv_handler(void) {
    debug_printf("SLIP Packet received and decoded, size = %u:\n", slip_datagram.size);
    debug_print_slip_datagram(&slip_datagram);

    // TODO: process datagram by type;

}

void slip_datagram_send(struct slip_datagram_t *datagram) {
    debug_printf("Sending SLIP encoded packet, size = %u:\n", datagram->size);
    debug_print_slip_datagram(datagram);

    serial_write(datagram->data, datagram->size);
}
