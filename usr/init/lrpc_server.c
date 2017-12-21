#include "lrpc_server.h"
#include "terminal.h"

errval_t recv_handler(void *arg);
struct client *whois(struct capref cap);

// Local data structs definitions for marshaling operations. Should not be
// visible outside the current file.
// Make sure that the first field of each struct is the lmp channel.
// recv_handler relies on this fact.

// Struct for marshaling the answer to the request for obtaining the nameserver
// endpoint.
struct nameserver_answer_t {
    struct lmp_chan sender_lmp;
    struct capref ns_endpoint;
};

// Struct for marshaling the answer to the request for spawning a process.
struct process_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    domainid_t domainid;
};

// Struct for marshalling the answer to the request of listing pids.
struct pids_answer_t {
    struct lmp_chan sender_lmp;
    size_t count;
    domainid_t *pids;
};

// Struct for marshalling the answer to the request of returning a process name.
struct pname_answer_t {
    struct lmp_chan sender_lmp;
    size_t length;
    char *name;
};

struct char_answer_t {
    struct lmp_chan sender_lmp;
    char retchar;
};

// Struct for marshalling the answer to the request of providing RAM.
struct ram_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    struct capref ram_cap;
    size_t allocated_size;
};

// Struct for marshalling the answer to the request of providing device capability.
struct device_cap_answer_t {
    struct lmp_chan sender_lmp;
    errval_t err;
    struct capref device_cap;
};

/* RPC implementations */
/* Message format:
 * There are at most 9 uintptr_t-length words in msg, namely msg[0] ... msg[8]
 *
 * msg[0] is used to indicate the type of message (see enum_rpc_msgtype)
 * msg[1] ... msg[8] is the actual message
 */
errval_t init_rpc(void)
{
    errval_t err;
    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPoint, 0, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: cap retype of dispatcher to selfep failed");
        return EXIT_FAILURE;
    }
    // TODO: make lmp a global variable, give it a more meaningful name (lmp_serv/open_lmp?)
    struct lmp_chan *lmp = (struct lmp_chan *) malloc(sizeof(struct lmp_chan));
    // by setting NULL_CAP as endpoint, we make this lmp an "open-receive" channel
    err = lmp_chan_accept(lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }
    err = cap_copy(cap_initep, lmp->local_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp cap copy of lmp->local_cap to cap_initep failed");
        return EXIT_FAILURE;
    }
    // the "reply-wait" operation
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }
    return SYS_ERR_OK;
}

// see also book page 98
errval_t recv_handler(void *arg)
{
    struct lmp_chan *lmp = (struct lmp_chan *) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref cap_endpoint;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
    if (err_is_fail(err)) {
        if (lmp_err_is_transient(err)) {
            err = lmp_chan_recv(lmp, &msg, &cap_endpoint);
        }
        if (err_is_fail(err)) {  // non-transient error, or transient but still fail after retry
            DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan recv, non transient error");
            return err;
        }
    }

    // register again for receiving
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan alloc recv slot failed");
        return err;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, arg));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan register recv failed");
        return err;
    }

    // no message content received?
    if (msg.buf.msglen <= 0)
        return err;
    send_func_t send_handler;
    void *answer_args;
    // TODO: why not just passing in the lmp_chan*, instead of the cap_endpoint? It seems we don't really need the whois().
    switch (msg.words[0]) {
        case AOS_RPC_ID_INIT:
            answer_args = marshal_init(cap_endpoint);
            send_handler = send_received;
            break;
        case AOS_RPC_ID_RAM:
            answer_args = marshal_ram(cap_endpoint, &msg);
            send_handler = send_ram;
            break;
        case AOS_RPC_ID_NUM:
            answer_args = marshal_number(cap_endpoint, &msg);
            send_handler = send_received;
            break;
        case AOS_RPC_ID_CHAR:
            answer_args = marshal_char(cap_endpoint, &msg);
            send_handler = send_received;
            break;
        case AOS_RPC_ID_STR:
            answer_args = marshal_str(cap_endpoint, &msg);
            send_handler = send_received;
            break;
        case AOS_RPC_ID_PROCESS:
            answer_args = marshal_process(cap_endpoint, &msg);
            send_handler = send_process;
            break;
        case AOS_RPC_ID_GET_PIDS:
            answer_args = marshal_pids(cap_endpoint, &msg);
            send_handler = send_pids;
            break;
        case AOS_RPC_ID_GET_PNAME:
            answer_args = marshal_pname(cap_endpoint, &msg);
            send_handler = send_pname;
            break;
        case AOS_RPC_ID_GET_NAMESERVER_EP:
            answer_args = marshal_nameserver_ep(cap_endpoint, &msg);
            send_handler = send_nameserver_ep;
            break;
        case AOS_RPC_ID_GET_CHAR:
            answer_args = marshal_retchar(cap_endpoint, &msg);
            send_handler = send_char;
            break;
        case AOS_RPC_ID_GET_DEVICE_CAP:
            answer_args = marshal_device_cap(cap_endpoint, &msg);
            send_handler = send_device_cap;
            break;
        default:
            debug_printf("RPC MSG Type %lu not supported!\n", msg.words[0]);
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    struct lmp_chan *ret_chan = (struct lmp_chan *) answer_args;
    err = lmp_chan_register_send(ret_chan, get_default_waitset(),
            MKCLOSURE((void*) send_handler, answer_args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan register send failed");
        return err;
    }
    return SYS_ERR_OK;
}

struct client *whois(struct capref cap)
{
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c whois: debug identify cap failed");
        return NULL;
    }
    // TODO: rewrite this with cap_compare()
    while (cur != NULL) {
        if (return_cap.u.endpoint.listener == cur->end.listener
            && return_cap.u.endpoint.epoffset == cur->end.epoffset) {
            break;
        }
        cur = cur->next;
    }
    return cur;
}


void *marshal_number(struct capref cap, struct lmp_recv_msg *msg) {
    debug_printf("server received number: %u\n", (uint32_t) msg->words[1]);

    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    return (void*) &(sender->lmp);
}

void *marshal_char(struct capref cap, struct lmp_recv_msg *msg)
{
    errval_t err = sys_print((const char *)&msg->words[1], 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not sys_print");
        return NULL;
    }

    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    return (void*) &(sender->lmp);
}

void *marshal_str(struct capref cap, struct lmp_recv_msg *msg)
{
    int len = strnlen((const char*) &msg->words[1], 32);
    //debug_printf("Got packet len(%i) %lx!\n", len, &msg->words[3]);
    errval_t err = sys_print((const char *)&msg->words[1], len);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not sys_print");
        return NULL;
    }

    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    return (void*) &(sender->lmp);
}

void *marshal_process(struct capref cap, struct lmp_recv_msg *msg)
{
    errval_t spawn_err = SYS_ERR_OK;
    domainid_t domainid;

    coreid_t target_core_id = *(coreid_t *) &msg->words[1];

    if (disp_get_core_id() != target_core_id) {
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 0;
        message[1] = AOS_RPC_ID_PROCESS;
        strncpy((char *)(message + 2), (const char*)&msg->words[3], (URPC_PAYLOAD_LEN - 2)* 4 - 1);
        ((char *)(message + 2))[(URPC_PAYLOAD_LEN - 2)* 4] = 0;

        urpc_write(message, target_core_id);

        urpc_read_until_ack(message, disp_get_core_id());

        if (err_is_fail((errval_t)message[1])) {
            debug_printf("Remote spawning failed\n");
            return NULL;
        }
        domainid = message[0];
    } else {
        struct spawninfo *si = malloc(sizeof(struct spawninfo));
        spawn_err = spawn_load_by_name(&msg->words[3], si);
        if (err_is_fail(spawn_err)) {
            DEBUG_ERR(spawn_err, "usr/init/main.c could not start a process\n");
            return NULL;
        }
        domainid = si->domain_id;
    }

    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    struct process_answer_t *args = malloc(sizeof(struct process_answer_t));
    args->sender_lmp = sender->lmp;
    args->err = spawn_err;
    args->domainid = domainid;

    return (void*) args;
}

void *marshal_pids(struct capref cap_endpoint, struct lmp_recv_msg *msg) {
    domainid_t comm_dest = (domainid_t) msg->words[1];
    coreid_t target_core_id = pid_get_core_id(comm_dest);
    size_t length;
    domainid_t *arr;

    if (target_core_id == my_core_id) {
        process_manager_get_all_pids(&pm, &arr, &length);
    } else {
        // send urpc request
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 0;
        message[1] = AOS_RPC_ID_GET_PIDS;
        urpc_write(message, target_core_id);

        // wait for and read response
        urpc_read_until_ack(message, my_core_id);
        length = message[0];
        if (length) {
            arr = malloc(sizeof(domainid_t) * length);
            memcpy(arr, &message[1], sizeof(domainid_t) * 7);

            if (length > 7) {
                size_t remaining = length - 7;
                domainid_t *ptr = arr + 7;

                while (remaining >= 8) {
                    urpc_read_until_ack(message, my_core_id);
                    memcpy(ptr, (const void *)&message[0], sizeof(domainid_t) * 8);
                    remaining -= 8;
                    ptr += 8;
                }

                if (remaining) {
                    urpc_read_until_ack(message, my_core_id);
                    memcpy(ptr, (const void *)&message[0], sizeof(domainid_t) * remaining);
                }
            }
        } else {
            debug_printf("Got error response for get_pids\n");
            return NULL;
        }
    }

    // find the channel
    struct client *sender = whois(cap_endpoint);
    if (sender == NULL) {
        return NULL;
    }

    struct pids_answer_t *args = malloc(sizeof(struct pids_answer_t));
    args->sender_lmp = sender->lmp;
    args->count = length;

    if (length) {
        args->pids = malloc(length * sizeof(domainid_t));
        memcpy(args->pids, arr, sizeof(domainid_t) * length);
    } else {
        args->pids = NULL;
    }

    return args;
}

void *marshal_pname(struct capref cap_endpoint, struct lmp_recv_msg *msg) {
    domainid_t pid = (domainid_t) msg->words[1];
    domainid_t comm_dest = (domainid_t) msg->words[2];
    coreid_t target_core_id = pid_get_core_id(comm_dest);

    const char *name;
    if (target_core_id == my_core_id) {
        name = process_manager_get_name(&pm, pid);
    } else {
        // send urpc request
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 0;
        message[1] = AOS_RPC_ID_GET_PNAME;
        message[2] = pid;
        urpc_write(message, target_core_id);

        // wait for and read response
        urpc_read_until_ack(message, my_core_id);

        size_t length = message[0];
        if (length) {
            char *nameptr = malloc(sizeof(char) * (length + 1));
            char *ptr = nameptr;

            // deal with the first packet (7 words at most)
            strncpy(ptr, (const char *)&message[1], MIN(length, 28));

            if (length > 28) {
                size_t remaining = length - 28;
                ptr += 28;

                while (remaining >= 32) {
                    urpc_read_until_ack(message, my_core_id);
                    strncpy(ptr, (const char *)&message[0], 32);
                    remaining -= 32;
                    ptr += 32;
                }

                if (remaining) {
                    urpc_read_until_ack(message, my_core_id);
                    strncpy(ptr, (const char *)&message[0], remaining);
                }
            }

            nameptr[length] = 0;
            name = nameptr;
        } else {
            debug_printf("Failed to get process name, empty string returned.\n");
            return NULL;
        }
    }

    struct client *sender = whois(cap_endpoint);
    if (sender == NULL) {
        return NULL;
    }

    /* +1 for the NULL terminator */
    size_t length_of_name = (name == NULL) ? 0 : (strlen(name) + 1);

    struct pname_answer_t *args = malloc(sizeof(struct pname_answer_t));
    args->sender_lmp = sender->lmp;
    args->length = length_of_name;

    if (length_of_name) {
        args->name = malloc(length_of_name);
        strncpy(args->name, name, length_of_name);
    } else {
        args->name = NULL;
    }

    return args;
}

// sets up the client struct for new processes
void *marshal_init(struct capref cap) {
    struct client *potential = whois(cap);
    // already have a channel to this client
    if (potential != NULL) {
        return (void*) &(potential->lmp);
    }

    // new client, allocate a channel (it's only used for sending, receiving is always done by open-receiving)
    potential = (struct client*) malloc(sizeof(struct client));

    struct capability return_cap;
    debug_cap_identify(cap, &return_cap);
    potential->end = return_cap.u.endpoint;
    potential->prev = NULL;
    if (client_list == NULL) {
        potential->next = NULL;
    }
    else {
        potential->next = client_list;
        client_list->prev = potential;
    }
    client_list = potential;

    errval_t err = lmp_chan_accept(&potential->lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not do lmp chan accept");
        return NULL;
    }
    return (void*) &(potential->lmp);
}

void *marshal_retchar(struct capref cap_endpoint, struct lmp_recv_msg *msg)
{
    domainid_t comm_dest = (domainid_t)msg->words[1];
    coreid_t target_core_id = pid_get_core_id(comm_dest);
    char retchar;

    if (target_core_id == my_core_id)
    {
        retchar = get_next_char_from_buffer();
    }
    else
    {
        // send URPC request
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 0;
        message[1] = AOS_RPC_ID_GET_CHAR;
        urpc_write(message, target_core_id);

        // wait for and read response
        urpc_read_until_ack(message, my_core_id);

        retchar = message[0];
    }

    struct client *sender = whois(cap_endpoint);
    if (sender == NULL)
    {
        debug_printf("usr/init/main.c answer char: could not identify client\n");
        return NULL;
    }

    struct char_answer_t *args = malloc(sizeof(struct char_answer_t));
    args->sender_lmp = sender->lmp;
    args->retchar = retchar;

    return (void*)args;
}

void *marshal_ram(struct capref cap, struct lmp_recv_msg *msg) {
    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    struct capref return_cap;
    size_t alignment = ROUND_UP((size_t) msg->words[2], BASE_PAGE_SIZE);
    alignment = max(alignment, BASE_PAGE_SIZE); // in case alignment is 0
    size_t allocated_size = ROUND_UP(msg->words[1], alignment);
    errval_t err = ram_alloc_aligned(&return_cap, (size_t) msg->words[1], (size_t) msg->words[2]);


    struct ram_answer_t *args = malloc(sizeof(struct ram_answer_t));
    args->sender_lmp = sender->lmp;
    args->err = err;
    args->ram_cap = return_cap;
    args->allocated_size = allocated_size;

    return (void*) args;
}

void* marshal_device_cap(struct capref cap, struct lmp_recv_msg* msg) {
    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }
    struct capref return_cap;
    errval_t err = slot_alloc(&return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/rpc_server.h answer device cap: could not alloc slot");
    }
    err = frame_forge(return_cap, (lpaddr_t) msg->words[2], (size_t) msg->words[1], my_core_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/rpc_server.h answer device cap: could not forge frame");
    }
    struct device_cap_answer_t *args = malloc(sizeof(struct device_cap_answer_t));
    args->sender_lmp = sender->lmp;
    args->err = err;
    args->device_cap = return_cap;

    return (void*) args;
}

void *marshal_nameserver_ep(struct capref cap, struct lmp_recv_msg *msg) {
    struct client *sender = whois(cap);
    if (sender == NULL) {
        return NULL;
    }

    struct nameserver_answer_t *args =
        malloc(sizeof(struct nameserver_answer_t));
    args->sender_lmp = sender->lmp;
    args->ns_endpoint = ns_endpoint;

    return (void*) args;
}

/* All send handlers */

// handler to send a signal that the message was received
errval_t send_received(void *arg) {
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, 1); // send a 1 to signal that the message arrived
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c send received: could not do lmp chan send1");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t send_ram(void *args) {
    struct ram_answer_t *ram_args = (struct ram_answer_t *) args;
    struct lmp_chan* lmp = &ram_args->sender_lmp;
    errval_t *err = &ram_args->err;
    struct capref *cap = &ram_args->ram_cap;
    size_t *allocated_size = &ram_args->allocated_size;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, *cap, (size_t)(err_is_fail(*err)? 0:1), *allocated_size, (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_process(void *args) {
    struct process_answer_t *pr_args = (struct process_answer_t *) args;
    size_t is_err = (err_is_fail(pr_args->err) ? 0 : 1);

    errval_t err_send = lmp_chan_send3(&pr_args->sender_lmp, LMP_FLAG_SYNC,
            NULL_CAP, is_err, pr_args->domainid, (uintptr_t) pr_args->err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_pids(void *args) {
    errval_t err_send;
    uintptr_t msg[9] = {0};
    msg[0] = AOS_RPC_ID_ACK;

    // get channel
    struct pids_answer_t *pid_args = (struct pids_answer_t *) args;
    struct lmp_chan* lmp = &pid_args->sender_lmp;
    size_t length = pid_args->count;
    domainid_t *pids = pid_args->pids;

    msg[1] = length;

    // get string and send in possibly multiple packets
    // put into msg[2..8], 7 words <=> 28 bytes
    memcpy((void *)&msg[2], pids, MIN(28, length * sizeof(domainid_t)));

    err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
        return err_send;
    }

    if (length <= 7)
        return SYS_ERR_OK;

    // if there are remaining characters
    length -= 28;
    pids += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[1..8]
        memcpy((void *)&msg[1], pids, 32);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }

        pids += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        memcpy((void *)&msg[1], pids, length * sizeof(domainid_t));

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }
    }


    return SYS_ERR_OK;
}

errval_t send_pname(void *args) {
    errval_t err_send;
    uintptr_t msg[9] = {0};
    msg[0] = 1;  // ACK

    struct pname_answer_t *pname_args = (struct pname_answer_t *) args;
    struct lmp_chan* lmp = &pname_args->sender_lmp;
    size_t length = pname_args->length;
    char *name = pname_args->name;


    msg[1] = length;

    // get string and send in possibly multiple packets
    // put into msg[2..8], 7 words <=> 28 bytes
    strncpy((char *)&msg[2], name, 28);

    err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
        return err_send;
    }

    if (length <= 28)
        return SYS_ERR_OK;

    // if there are remaining characters
    length -= 28;
    name += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[1..8]
        strncpy((char *)&msg[1], name, 32);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }

        name += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&msg[1], (const char*) name, length);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }
    }


    return SYS_ERR_OK;
}

errval_t send_char(void *args)
{
    // get channel
    struct lmp_chan *lmp = (struct lmp_chan *)args;
    // cast to void* and move pointer to after the lmp channel
    args = (void *)ROUND_UP((uintptr_t)args + sizeof(struct lmp_chan), 4);
    char retchar = *(char *)args;

    errval_t err_send = lmp_chan_send2(lmp, LMP_FLAG_SYNC, NULL_CAP, AOS_RPC_ID_ACK, retchar);
    if (err_is_fail(err_send))
    {
        DEBUG_ERR(err_send, "usr/init/main.c send char: could not do lmp chan send2");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_nameserver_ep(void *args) {
    struct nameserver_answer_t *ns_args = (struct nameserver_answer_t *) args;
    struct lmp_chan *lmp = &ns_args->sender_lmp;

    errval_t err_send = lmp_chan_send1(lmp, LMP_FLAG_SYNC,
            ns_args->ns_endpoint, 1);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_device_cap(void* args) {
    // get channel
    struct device_cap_answer_t* device_args = (struct device_cap_answer_t*) args;

    struct lmp_chan* lmp = &device_args->sender_lmp;
    errval_t *err = &device_args->err;
    struct capref *cap = &device_args->device_cap;

    errval_t err_send = lmp_chan_send2(lmp, LMP_FLAG_SYNC, *cap, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send device cap: could not do lmp chan send2");
        return err_send;
    }

    return SYS_ERR_OK;
}