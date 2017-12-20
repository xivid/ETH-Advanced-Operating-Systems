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
#include <netutil/htons.h>
#include <netutil/checksum.h>


/**
 * slip definitions
 */

#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335
#define SLIP_ESC_NUL 0336

//! encoded datagram
#define SLIP_DATAGRAM_MAX_BYTES 1006
struct slip_datagram_t {
    uint8_t data[SLIP_DATAGRAM_MAX_BYTES];
    uint16_t size;
};
#define EMPTY_SLIP_DATAGRAM (struct slip_datagram_t) { \
                                .data = {}, \
                                .size = 0, \
                            }

//! decoded packet, which is always no larger than the encoded datagram.
#define SLIP_DECODED_PACKET_MAX_BYTES SLIP_DATAGRAM_MAX_BYTES
struct slip_decoded_packet_t {
    uint8_t data[SLIP_DECODED_PACKET_MAX_BYTES];
    uint16_t size;
};
#define EMPTY_SLIP_DECODED_PACKET (struct slip_decoded_packet_t) { \
                                      .data = {}, \
                                      .size = 0, \
                                  }

/* state variables for decoding */
__attribute__ ((unused))
static struct slip_decoded_packet_t slip_decoded_packet = EMPTY_SLIP_DECODED_PACKET;
__attribute__ ((unused))
static bool slip_escaping = false;

//! on incoming bytes
void slip_receive(uint8_t *buf, size_t len);

//! dump the packet content
void debug_print_packet(uint8_t *data, size_t size);

//! process a completely received packet
void slip_packet_recv_handler(void);

//! send slip encoded datagram
void slip_datagram_send(struct slip_datagram_t *datagram);

//! encode before sending
void slip_send(uint8_t *buf, size_t size);


/**
 * ip definitions
 */

//! IP address type (in binary network format).
typedef uint32_t ip_addr_t;

//! The IP packet structure.
struct ip_t
{
    //!< The IP header length.
    uint8_t ihl:4;
    //!< The IP version.
    uint8_t version:4;

    //! Type of Service.
    uint8_t tos;
    //! IP packet length (both data and header).
    uint16_t length;

    //! Identification.
    uint16_t id;
    //! Fragment offset.
    uint16_t flag_off;

    //! Time To Live.
    uint8_t ttl;
    //! The type of the upper-level protocol.
    uint8_t protocol;
    //! IP header checksum.
    uint16_t checksum;

    //! IP source address (in network format).
    uint32_t ip_src;
    //! IP destination address (in network format).
    uint32_t ip_dst;
} __attribute__ ((packed));
void ipv4_packet_recv_handler(struct ip_t *ip, size_t len);

/**
 * icmp definitions
 */

//! The ICMP packet structure
struct icmp_t
{
    //! Type
    uint8_t type;

    //! Code
    uint8_t code;

    //! Checksum
    uint16_t checksum;

    //! Header data
    uint8_t hdr_data[4];

    //! Payload
    uint8_t payload[];
} __attribute__ ((packed));
void icmp_packet_recv_handler(struct icmp_t *icmp, size_t len, ip_addr_t ip_src);
void icmp_echo_request_handler(struct icmp_t *icmp, size_t len, ip_addr_t ip_src);

#define ICMP_PAYLOAD_MAX_BYTES SLIP_DECODED_PACKET_MAX_BYTES
struct icmp_echo_reply_t {
    struct ip_t ip;
    struct icmp_t icmp;
    uint8_t payload[ICMP_PAYLOAD_MAX_BYTES];
} __attribute__ ((packed));

__attribute__ ((unused))
static struct icmp_echo_reply_t icmp_echo_reply;
#endif //BF_AOS_SLIP_H
