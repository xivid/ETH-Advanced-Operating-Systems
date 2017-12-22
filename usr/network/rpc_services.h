//
// Created by 杨志飞 on 2017/12/21.
//

#ifndef BF_AOS_RPC_SERVICES_H
#define BF_AOS_RPC_SERVICES_H


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <aos/aos_rpc.h>
#include <netutil/user_serial.h>
#include <netutil/htons.h>
#include <netutil/checksum.h>
#include "slip.h"

#define UDP_BUFFER_SIZE 1006


/** RPC server of network manager
 *  Supports the following rpc message types:
 *    AOS_RPC_ID_INIT
 *    AOS_RPC_ID_UDP_BIND,
 *    AOS_RPC_ID_UDP_SEND
 */
struct client {
    // TODO: store the capref directly? (can avoid ugly comparison in whois())
    struct capref endpoint;
    struct aos_rpc rpc;
    struct client *next;
};
extern struct client *client_list;//  = NULL;
struct client *whois(struct capref cap_endpoint);

struct rpc_answer_t {
    struct lmp_chan* sender_lmp;
    net_err_names_t err;
};

errval_t init_rpc_server(void);
errval_t lmp_recv_handler(void *v_args);
errval_t lmp_send_handler(void *v_args);
__attribute__((unused))
extern int network_rpc_send_retry_count; // = 0;

struct rpc_answer_t *marshal_init(struct capref cap_endpoint);
struct rpc_answer_t *marshal_udp_bind(struct capref cap, struct lmp_recv_msg *msg);
struct rpc_answer_t *marshal_udp_forward(struct capref cap, struct lmp_recv_msg *msg);
uint8_t udp_forward_buffer[UDP_BUFFER_SIZE];

/** UDP servers manager
 *    Keeps record of all registered udp servers
 */

struct udp_server {
    struct aos_rpc *rpc;
    port_t port;
    struct udp_server *next;
};
extern struct udp_server *udp_server_list; //  = NULL;

struct aos_rpc *get_udp_server_channel(port_t port);


#endif //BF_AOS_RPC_SERVICES_H
