//
// Created by Zhifei Yang on 2017/12/20.
//

#include "rpc_services.h"

// #define DEBUG_PACKET
// #define TRACE_PACKET
// #define ENABLE_UDP_IPV4_CHKSUM

void slip_receive(uint8_t *buf, size_t len)
{
    // debug_printf("slip_receive(%s, %lu)\n", buf, len);

    uint8_t new_char;
    for (int i = 0; i < len; ++i) {
        new_char = buf[i];
        if (slip_decoded_packet.size < SLIP_DECODED_PACKET_MAX_BYTES) {
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
                    slip_packet_recv_handler();
                    slip_decoded_packet = EMPTY_SLIP_DECODED_PACKET;
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
            slip_decoded_packet.data[slip_decoded_packet.size++] = new_char;
        }
        else {
            debug_printf("buffer full, dropped SLIP byte: [%c] (0x%2x)\n", new_char, new_char);
            if (new_char == SLIP_END) {
                debug_printf("buffer has been full without a SLIP_END, but SLIP_END now comes -> clear the buffer\n");
                slip_decoded_packet = EMPTY_SLIP_DECODED_PACKET;
                slip_escaping = false;
            }
        }
    }

}

void debug_print_packet(uint8_t *data, size_t size) {
#define PRINTABLE(c) (isprint(c) ? (c) : '.')
    int i;
    for (i = 0; i < size / 8; ++i) {
        debug_printf(
                "0x%06x: %02x %02x %02x %02x %02x %02x %02x %02x  %c%c%c%c%c%c%c%c\n",
                i * 8,
                data[i*8+0], data[i*8+1], data[i*8+2], data[i*8+3],
                data[i*8+4], data[i*8+5], data[i*8+6], data[i*8+7],
                PRINTABLE(data[i*8+0]), PRINTABLE(data[i*8+1]),
                PRINTABLE(data[i*8+2]), PRINTABLE(data[i*8+3]),
                PRINTABLE(data[i*8+4]), PRINTABLE(data[i*8+5]),
                PRINTABLE(data[i*8+6]), PRINTABLE(data[i*8+7])
        );
    }

    if (size % 8 != 0) {
        char buf[60] = {0};
        sprintf(buf, "0x%06x:", i * 8);
        int j;
        for (j = 0; j < size % 8; ++j) {
            sprintf(buf + strlen(buf), " %02x", data[i * 8 + j]);
        }
        for (; j < 8; ++j) {
            sprintf(buf + strlen(buf), "   ");
        }
        sprintf(buf + strlen(buf), "  ");
        for (j = 0; j < size % 8; ++j) {
            sprintf(buf + strlen(buf), "%c", PRINTABLE(data[i * 8 + j]));
        }
        debug_printf("%s\n", buf);
    }

    debug_printf("\n");
}

void slip_packet_recv_handler(void) {
#ifdef TRACE_PACKET
    debug_printf("SLIP Packet received and decoded, size = %u:\n", slip_decoded_packet.size);
#endif
#ifdef DEBUG_PACKET
    debug_print_packet(slip_decoded_packet.data, slip_decoded_packet.size);
#endif

    if (slip_decoded_packet.size < 20
        || (slip_decoded_packet.data[0] & 0xf0) != 0x40 || (slip_decoded_packet.data[0] & 0xf) < 5
        || inet_checksum(slip_decoded_packet.data, (uint16_t)(slip_decoded_packet.data[0] & 0xf) << 2) != 0) {
        debug_printf("Not a valid IPv4 packet, or not an IPv4 packet. Dropped.\n");
        return;
    }

    ipv4_packet_recv_handler((struct ip_t *) &slip_decoded_packet.data[0], slip_decoded_packet.size);
    
    /*void *p = &slip_decoded_packet.data[0];
    struct ip_t ip_header = *(struct ip_t *) p;*/
}

void slip_send(uint8_t *buf, size_t size) {
#ifdef TRACE_PACKET
    debug_printf("SLIP sending packet with size = %u, before encoding:\n", size);
#endif
#ifdef DEBUG_PACKET
    debug_print_packet(buf, size);
#endif

    for (size_t i = 0; i < size; ++i) {
        switch(*buf) {
            case 0:
                serial_putchar(SLIP_ESC);
                serial_putchar(SLIP_ESC_NUL);
                break;
            case SLIP_END:
                serial_putchar(SLIP_ESC);
                serial_putchar(SLIP_ESC_END);
                break;
            case SLIP_ESC:
                serial_putchar(SLIP_ESC);
                serial_putchar(SLIP_ESC_ESC);
                break;
            default:
                serial_putchar(*buf);
        }
        ++buf;
    }

    serial_putchar(SLIP_END);
}

void slip_datagram_send(struct slip_datagram_t *datagram) {
#ifdef TRACE_PACKET
    debug_printf("Sending SLIP encoded packet, size = %u:\n", datagram->size);
#endif
#ifdef DEBUG_PACKET
    debug_print_packet(datagram->data, datagram->size);
#endif

    serial_write(datagram->data, datagram->size);
}


void ipv4_packet_recv_handler(struct ip_t *ip, size_t len) {
    ip->length = ntohs(ip->length);
    ip->id = ntohs(ip->id);
    ip->flag_off = ntohs(ip->flag_off);
    ip->checksum = ntohs(ip->checksum);
    ip->ip_src = ntohl(ip->ip_src);
    ip->ip_dst = ntohl(ip->ip_dst);
    assert(ip->length == len);

#ifdef DEBUG_PACKET
    debug_printf("ip version: 0x%1x\n", ip->version);
    debug_printf("ip ihl: 0x%1x\n", ip->ihl);
    debug_printf("ip tos: 0x%02x\n", ip->tos);
    debug_printf("ip length: 0x%04x\n", ip->length);
    debug_printf("ip identification: 0x%04x\n", ip->id);
    debug_printf("ip flags and offset: 0x%04x\n", ip->flag_off);
    debug_printf("ip ttl: 0x%1x\n", ip->ttl);
    debug_printf("ip protocol: 0x%1x\n", ip->protocol);
    debug_printf("ip header checksum: 0x%04x\n", ip->checksum);
    debug_printf("ip source addr: 0x%08x (%d.%d.%d.%d)\n", ip->ip_src, *((uint8_t*)&ip->ip_src + 3), *((uint8_t*)&ip->ip_src + 2), *((uint8_t*)&ip->ip_src + 1), *(uint8_t*)&ip->ip_src);
    debug_printf("ip dest addr: 0x%08x (%d.%d.%d.%d)\n", ip->ip_dst, *((uint8_t*)&ip->ip_dst + 3), *((uint8_t*)&ip->ip_dst + 2), *((uint8_t*)&ip->ip_dst + 1), *(uint8_t*)&ip->ip_dst);
#endif

    if (ip->flag_off != 0x4000) {
        debug_printf("IPv4 Fragmentation not supported, dropped this packet.\n");
        return;
    }

    if (ip->ip_dst != 0x0a000201 && ip->ip_dst != 0xffffffff) {
        debug_printf("Destination neither me nor broadcast, dropped this packet.\n");
        return;
    }

    uint8_t *payload = &slip_decoded_packet.data[ip->ihl << 2];
    uint16_t payload_len = ip->length - (ip->ihl << 2);
    /* ICMP */
    if (ip->protocol == 0x01 && ip->tos ==  0) {

#ifdef TRACE_PACKET
        debug_printf("ICMP packet detected. base = %u, size = %u, checksum: 0x%04x\n",
                     ip->ihl << 2,
                     ip->length - (ip->ihl << 2),
                     inet_checksum(payload, payload_len));
#endif
        if (inet_checksum(payload, payload_len)) {
            debug_printf("checksum error, dropped this packet.\n");
            return;
        }
        icmp_packet_recv_handler((struct icmp_t *) payload, payload_len, ip->ip_src);

    } else if (ip->protocol == 0x11) {

#ifdef TRACE_PACKET
        debug_printf("UDP packet detected. src port = %u, dest port = %u, length = %u\n",
                     ntohs(*(uint16_t *)payload), ntohs(*(uint16_t *)(payload + 2)), ntohs(*(uint16_t *)(payload + 4)));
#endif
        // TODO: verify checksum
        udp_packet_recv_handler((struct udp_t *) payload, payload_len, ip);

    } else {
        debug_printf("Protocol 0x%02x not implemented yet.\n", ip->protocol);
    }
}

void icmp_packet_recv_handler(struct icmp_t *icmp, size_t len, ip_addr_t ip_src) {
#ifdef TRACE_PACKET
    debug_printf("ICMP handler: type = %u\n", icmp->type);
#endif
    // Note: we didn't convert the icmp->checksum to host byte order since we wouldn't really use it.
    switch (icmp->type) {
        case 0:
            debug_printf("echo reply support not implemented yet (which means you cannot ping from PandaBoard)\n");
            break;
        case 8:
            // ping
            icmp_echo_request_handler(icmp, len, ip_src);
            break;
        default:
            debug_printf("icmp type %u not implemented yet\n", icmp->type);
    }
}

void icmp_echo_request_handler(struct icmp_t *icmp, size_t len, ip_addr_t ip_src) {
#ifdef TRACE_PACKET
    uint16_t* identifier = (uint16_t*)&icmp->hdr_data[0];
    uint16_t* seq_num = (uint16_t*)&icmp->hdr_data[2];
    debug_printf("ICMP Echo request: identifier = %u, seq_num = %u, payload length = %u\n", ntohs(*identifier), ntohs(*seq_num), len - 8);
#endif

    size_t payload_length = len - 8;

#ifdef DEBUG_PACKET
    debug_printf("hdr_data: %02x %02x %02x %02x\n", icmp->hdr_data[0], icmp->hdr_data[1], icmp->hdr_data[2], icmp->hdr_data[3]);
#endif

    icmp_echo_reply.ip = (struct ip_t) {
        .version = 4,
        .ihl = 5,
        .tos = 0x00,
        .length = htons((uint16_t)(20 + 8 + payload_length)),
        .id = htons(0xBEEF), /* some random identification */
        .flag_off = htons(0x4000),
        .ttl = 0x40, /* ttl = 64, hope this works */
        .protocol = 0x01,
        .checksum = 0x0000,
        .ip_src = htonl(0x0a000201), /* hard-coded my ip: 10.0.2.1 */
        .ip_dst = htonl(ip_src)
    };
    icmp_echo_reply.icmp = (struct icmp_t) {
        .type = 0,
        .code = 0,
        .checksum = 0,
        .hdr_data = { icmp->hdr_data[0], icmp->hdr_data[1], icmp->hdr_data[2], icmp->hdr_data[3] },
    };
    memcpy(icmp_echo_reply.payload, icmp->payload, sizeof(uint8_t) * payload_length);

    uint16_t checksum;
    // fill icmp checksum
    checksum = inet_checksum(&icmp_echo_reply.icmp, (uint16_t)(8 + payload_length));
    icmp_echo_reply.icmp.checksum = checksum;

    // fill ip checksum
    checksum = inet_checksum(&icmp_echo_reply.ip, 20);
    icmp_echo_reply.ip.checksum = checksum;

    slip_send((uint8_t *)&icmp_echo_reply, 20 + 8 + payload_length);
}

void udp_packet_recv_handler(struct udp_t *udp, size_t len, struct ip_t *ip) {
    /*udp->port_src = ntohs(udp->port_src);
    udp->port_dst = ntohs(udp->port_dst);
    udp->length = ntohs(udp->length);
    udp->checksum = ntohs(udp->checksum);*/
    errval_t err;
    port_t port = ntohs(udp->port_dst);

    if (port == 8888) {
        // echo server
        udp_echo_handler(udp, len, ip);
    } else {
        struct aos_rpc *udp_server = get_udp_server_channel(port);
        net_err_names_t net_err;
        if (udp_server != NULL) {
            debug_printf("delivering to udp server on %u\n", port);
            err = aos_rpc_net_udp_deliver(udp_server, ip->ip_src,
                                          ntohs(udp->port_src), ntohs(udp->port_dst),
                                          udp->payload, ntohs(udp->length) - (uint16_t)8, &net_err);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "aos_rpc_net_udp_deliver failed");
            }
            if (net_err != NET_ERR_OK) {
                debug_printf("deliver failed, net_err = %u\n", net_err);
            }
        } else {
            debug_printf("No service on UDP Port %u, dropped this packet.\n", port);
        }
    }
}

void udp_echo_handler(struct udp_t *udp, size_t len, struct ip_t *ip) {

    udp_reply.ip = (struct ip_t) {
        .version = 4,
        .ihl = 5,
        .tos = 0x00, /* hope this doesn't matter */
        .length = htons((uint16_t)(20 + len)),
        .id = htons(0xBEEF), /* some random identification */
        .flag_off = htons(0x4000),
        .ttl = 0x40, /* ttl = 64, hope this works */
        .protocol = 0x11,
        .checksum = 0x0000,
        .ip_src = htonl(0x0a000201), /* hard-coded my ip: 10.0.2.1 */
        .ip_dst = htonl(ip->ip_src)
    };

    udp_reply.udp = (struct udp_t) {
        .port_src = udp->port_dst,
        .port_dst = udp->port_src,
        .length = udp->length,
        .checksum = 0x0000
    };

    memcpy(udp_reply.payload, udp->payload, sizeof(uint8_t) * (len - 8));

    uint16_t checksum;

    // fill udp checksum (checksum for udp on ipv4 is optional, disabled by default to improve performance)
#ifdef ENABLE_UDP_IPV4_CHKSUM
    struct udp_ipv4_pseudo_header_t header = {
            .ip_src = htonl(0x0a000201),
            .ip_dst = htonl(ip->ip_src),
            .zeros = 0x00,
            .protocol = 0x11,
            .udp_length = htons((uint16_t)len),
            .udp = udp_reply.udp
    };
    memcpy(header.payload, udp->payload, sizeof(uint8_t) * (len - 8));
    checksum = inet_checksum(&header, (uint16_t)(len + 12));
    udp_reply.udp.checksum = checksum;
#else
    udp_reply.udp.checksum = 0x0000;
#endif
    // fill ip checksum
    checksum = inet_checksum(&udp_reply.ip, 20);
    udp_reply.ip.checksum = checksum;

    slip_send((uint8_t *)&udp_reply, 20 + len);
}