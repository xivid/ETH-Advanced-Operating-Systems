/**
 * \file
 * \brief RPC server in the init process
 */

/*
 * Created by zhiyang on 11/22/17.
 */

#ifndef _INIT_RPC_SERVER_H_
#define _INIT_RPC_SERVER_H_

#include <aos/aos_rpc.h>
#include <spawn/spawn.h>
#include <spawn/process_manager.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

// macros copied from kernel/include/arch/arm/startup_arm.h
#define INIT_BOOTINFO_VBASE     ((lvaddr_t) 0x200000)
#define INIT_ARGS_VBASE         (INIT_BOOTINFO_VBASE + BOOTINFO_SIZE)
#define INIT_DISPATCHER_VBASE   (INIT_ARGS_VBASE + ARGS_SIZE)
#define MON_URPC_VBASE          (INIT_DISPATCHER_VBASE + DISPATCHER_SIZE)

#define N_LINES                 (MON_URPC_SIZE / 2 / 64)
#define LINE_WORDS              (16)
#define URPC_PAYLOAD_LEN        (10)

/* rpc declarations */
struct client {
    struct EndPoint end;  // TODO: store the capref directly? (can avoid ugly comparison in whois())
    struct client* prev;
    struct client* next;

    struct lmp_chan lmp;
} *client_list = NULL;

extern struct process_manager pm;

errval_t init_rpc(void);
errval_t recv_handler(void* arg);
errval_t whois(struct capref cap, struct client **he_is);
void* answer_number(struct capref cap, struct lmp_recv_msg *msg);
void* answer_char(struct capref cap, struct lmp_recv_msg* msg);
void* answer_str(struct capref cap, struct lmp_recv_msg* msg);
void* answer_process(struct capref cap, struct lmp_recv_msg* msg);
void* answer_pids(struct capref cap, struct lmp_recv_msg* msg);
void* answer_pname(struct capref cap, struct lmp_recv_msg* msg);
void* answer_init(struct capref cap);
void* answer_ram(struct capref cap, struct lmp_recv_msg* msg);
errval_t send_received(void* arg);
errval_t send_ram(void* args);
errval_t send_process(void* args);
errval_t send_pids(void* args);
errval_t send_pname(void* args);

/* urpc declarations */
void urpc_write(const uint32_t message[URPC_PAYLOAD_LEN], coreid_t core);
void urpc_read_and_process(coreid_t core);
void urpc_read_until_ack(uint32_t *ack_response, coreid_t core);
static inline void dmb(void) { __asm volatile ("dmb"); }

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
    struct lmp_chan* lmp = (struct lmp_chan*) malloc(sizeof(struct lmp_chan));
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
errval_t recv_handler(void* arg)
{
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
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
    void* answer;
    void* answer_args;
    // TODO: why not just passing in the lmp_chan*, instead of the cap_endpoint? It seems we don't really need the whois().
    switch (msg.words[0]) {
        case AOS_RPC_ID_NUM:
            answer_args = answer_number(cap_endpoint, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_INIT:
            answer_args = answer_init(cap_endpoint);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_RAM:
            answer_args = answer_ram(cap_endpoint, &msg);
            answer = (void*) send_ram;
            break;
        case AOS_RPC_ID_CHAR:
            answer_args = answer_char(cap_endpoint, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_STR:
            answer_args = answer_str(cap_endpoint, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_PROCESS:
            answer_args = answer_process(cap_endpoint, &msg);
            answer = (void*) send_process;
            break;
        case AOS_RPC_ID_GET_PIDS:
            answer_args = answer_pids(cap_endpoint, &msg);
            answer = (void*) send_pids;
            break;
        case AOS_RPC_ID_GET_PNAME:
            answer_args = answer_pname(cap_endpoint, &msg);
            answer = (void*) send_pname;
            break;
        default:
            debug_printf("RPC MSG Type %lu not supported!\n", msg.words[0]);
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    struct lmp_chan* ret_chan = (struct lmp_chan*) answer_args;
    err = lmp_chan_register_send(ret_chan, get_default_waitset(), MKCLOSURE(answer, answer_args));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c recv_handler: lmp chan register send failed");
        return err;
    }
    return SYS_ERR_OK;
}

// TODO: why do we need a WHOIS?
errval_t whois(struct capref cap, struct client **he_is) {
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c whois: debug identify cap failed");
        return err;
    }
    // TODO: rewrite this with cap_compare()
    while (cur != NULL) {
        if (return_cap.u.endpoint.listener == cur->end.listener
            && return_cap.u.endpoint.epoffset == cur->end.epoffset) {
            *he_is = cur;
            break;
        }
        cur = cur->next;
    }
    return err;
}


void* answer_number(struct capref cap, struct lmp_recv_msg *msg) {
    debug_printf("server received number: %u\n", (uint32_t) msg->words[1]);

    // create answer
    struct client* he_is = NULL;
    errval_t err = whois(cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer number: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_char(struct capref cap, struct lmp_recv_msg* msg)
{
    errval_t err = sys_print((const char *)&msg->words[1], 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not sys_print");
        return NULL;
    }

    // create answer
    struct client* he_is = NULL;
    err = whois(cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_str(struct capref cap, struct lmp_recv_msg* msg)
{
    int len = strnlen((const char*) &msg->words[1], 32);
    //debug_printf("Got packet len(%i) %lx!\n", len, &msg->words[3]);
    errval_t err = sys_print((const char *)&msg->words[1], len);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not sys_print");
        return NULL;
    }

    // create answer
    struct client* he_is = NULL;
    err = whois(cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_process(struct capref cap, struct lmp_recv_msg* msg)
{
    errval_t err, spawn_err = SYS_ERR_OK;
    domainid_t domainid;

    coreid_t target_core_id = *(coreid_t *) &msg->words[1];

    if (disp_get_core_id() != target_core_id) {
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 1;
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

    // create answer
    struct client* he_is = NULL;
    err = whois(cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }


    size_t size_of_args = sizeof(domainid_t) +
                          ROUND_UP(sizeof(struct lmp_chan),4) +
                          ROUND_UP(sizeof(errval_t),4);
    void* args_ptr = malloc(size_of_args);
    void* args = args_ptr;


    // add channel to args
    *((struct lmp_chan*) args) = he_is->lmp;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add error to args
    *((errval_t*) args) = spawn_err;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    *((domainid_t*) args) = domainid;

    return (void*) args_ptr;
}

void* answer_pids(struct capref cap_endpoint, struct lmp_recv_msg* msg) {
    errval_t err;

    domainid_t comm_dest = (domainid_t) msg->words[1];
    coreid_t target_core_id = pid_get_core_id(comm_dest);
    size_t length;
    domainid_t *arr;

    if (target_core_id == disp_get_core_id()) {
        process_manager_get_all_pids(&pm, &arr, &length);
    } else {
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 1;
        message[1] = AOS_RPC_ID_GET_PIDS;

        urpc_write(message, target_core_id);

        urpc_read_until_ack(message, disp_get_core_id());
        length = message[2];
        arr = malloc(sizeof(char) * (length + 1));
        size_t content_offset = 3;

        for (int i = 0; i < length / 28; i++) {
            strncpy(arr + 28*i, (const char *)&message[content_offset], 28);
            urpc_read_until_ack(message, disp_get_core_id());
            content_offset = 2;
        }

        strncpy(arr + 28*(length / 28), (const char *)&message[content_offset], MIN(28, length % 28));
        arr[length] = 0;
    }


    // find the channel
    struct client* he_is = NULL;
    err = whois(cap_endpoint, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }

    size_t size_of_args = ROUND_UP(sizeof(struct lmp_chan),4) +
                          sizeof(domainid_t) /* the length of pids[] */ +
                          sizeof(domainid_t) * length /* the pids[] */;

    void* args_ptr = malloc(size_of_args);
    void* args = args_ptr;

    *((struct lmp_chan*) args) = he_is->lmp;
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add length
    *(size_t *)args = pm.count;
    args += sizeof(size_t);
    // copy pid to args
    for (struct domain_info *p = pm.head; p != NULL; p = p->next) {
        *(domainid_t *)args = p->pid;
        args += sizeof(domainid_t);
    }

    return args_ptr;
}

void* answer_pname(struct capref cap_endpoint, struct lmp_recv_msg* msg) {
    errval_t err;
    domainid_t domainid = (domainid_t) msg->words[1];
    domainid_t comm_dest = (domainid_t) msg->words[2];
    coreid_t target_core_id = pid_get_core_id(comm_dest);

    char *name;
    if (target_core_id == disp_get_core_id()) {
        name = process_manager_get_name(&pm, domainid);
    } else {
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 1;
        message[1] = AOS_RPC_ID_GET_PNAME;
        message[2] = domainid;

        urpc_write(message, target_core_id);

        urpc_read_until_ack(message, disp_get_core_id());
        size_t length = message[2];
        name = malloc(sizeof(char) * (length + 1));
        char *ptr = name;

        // deal with the first packet
        strncpy(ptr, (const char *)&message[3], MIN(length, 28));

        for (ptr = name + 28; ptr < name + length; ptr += 32) {
            strncpy(ptr, (const char *)&message[2], 32);
            urpc_read_until_ack(message, disp_get_core_id());
        }

        strncpy(ptr, (const char *)&message[2], (length - 28) % 32);
        name[length] = 0;
    }

    // find the channel
    struct client* he_is = NULL;
    err = whois(cap_endpoint, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }

    size_t length_of_name = name == NULL ? 0 : strlen(name) + 1;  /* +1 for the NULL terminator */
    size_t size_of_args = ROUND_UP(sizeof(struct lmp_chan),4) +
                          sizeof(size_t) +
                          length_of_name;
    void* args_ptr = malloc(size_of_args);

    void* args = args_ptr;
    // add channel to args
    *((struct lmp_chan*) args) = he_is->lmp;
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add length
    *(size_t *)args = length_of_name;  // 0 means not found
    args += sizeof(size_t);
    // copy name string to args
    if (length_of_name) {
        strcpy((char *)args, name);
    }

    return args_ptr;
}

// sets up the client struct for new processes
void* answer_init(struct capref cap) {
    struct client *potential = NULL;
    errval_t err = whois(cap, &potential);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not identify client");
        return NULL;
    }
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

    err = lmp_chan_accept(&potential->lmp, DEFAULT_LMP_BUF_WORDS, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not do lmp chan accept");
        return NULL;
    }
    return (void*) &(potential->lmp);
}

void* answer_ram(struct capref cap, struct lmp_recv_msg* msg) {
    struct client *sender = NULL;
    errval_t err = whois(cap, &sender);
    if (err_is_fail(err) || sender == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not identify client");
        return NULL;
    }
    struct capref return_cap;
    size_t alignment = ROUND_UP((size_t) msg->words[2], BASE_PAGE_SIZE);
    alignment = max(alignment, BASE_PAGE_SIZE); // in case alignment is 0
    size_t allocated_size = ROUND_UP(msg->words[1], alignment);
    err = ram_alloc_aligned(&return_cap, (size_t) msg->words[1], (size_t) msg->words[2]);


    size_t size_of_args = sizeof(size_t) + ROUND_UP(sizeof(struct lmp_chan),4) + ROUND_UP(sizeof(errval_t),4) + ROUND_UP(sizeof(struct capref),4);
    void* args_ptr = malloc(size_of_args);
    void* args = args_ptr;


    // add channel to args
    *((struct lmp_chan*) args) = sender->lmp;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add error to args
    *((errval_t*) args) = err;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    // add cap to args
    *((struct capref*) args) = return_cap;
    // cast t void* and move pointer
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct capref), 4);
    *((size_t*) args) = allocated_size;

    return (void*) args_ptr;
}

/* All send handlers */

// handler to send a signal that the message was received
errval_t send_received(void* arg) {
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, 1); // send a 1 to signal that the message arrived
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c send received: could not do lmp chan send1");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t send_ram(void* args) {
    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // get error
    errval_t* err = (errval_t*) args;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    // get cap
    struct capref* cap = (struct capref*) args;
    // cast t void* and move pointer
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct capref), 4);
    size_t* allocated_size = (size_t*) args;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, *cap, (size_t)(err_is_fail(*err)? 0:1), *allocated_size, (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_process(void *args) {
    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // get error
    errval_t* err = (errval_t*) args;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    domainid_t *domain_id = (domainid_t*) args;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), *domain_id, (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_pids(void *args) {
    errval_t err_send;
    uintptr_t msg[9] = {0};
    msg[0] = 1;  // ACK

    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);

    // get length
    size_t length = *(size_t *) args;
    msg[1] = length;
    args += sizeof(size_t);

    // get string and send in possibly multiple packets
    // put into msg[2..8], 7 words <=> 28 bytes
    memcpy((void *)&msg[2], (const void *)args, MIN(28, length * sizeof(domainid_t)));

    err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
        return err_send;
    }

    if (length <= 7)
        return SYS_ERR_OK;

    // if there are remaining characters
    length -= 28;
    args += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[1..8]
        memcpy((void *)&msg[1], (const void *)args, 32);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }

        args += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        memcpy((void *)&msg[1], (const void*) args, length * sizeof(domainid_t));

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

    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);

    // get length
    size_t length = *(size_t*) args;
    msg[1] = length;
    args += sizeof(size_t);

    // get string and send in possibly multiple packets
    // put into msg[2..8], 7 words <=> 28 bytes
    strncpy((char *)&msg[2], (const char *)args, 28);

    err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
        return err_send;
    }

    if (length <= 28)
        return SYS_ERR_OK;

    // if there are remaining characters
    length -= 28;
    args += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[1..8]
        strncpy((char *)&msg[1], (const char *)args, 32);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }

        args += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&msg[1], (const char*) args, length);

        err_send = lmp_chan_send9(lmp, LMP_FLAG_SYNC, NULL_CAP, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
        if (err_is_fail(err_send)) {
            DEBUG_ERR(err_send, "usr/init/rpc_server.h send_pname(): could not do lmp_chan_send9");
            return err_send;
        }
    }


    return SYS_ERR_OK;
}

/* urpc implementations */
void *urpc_shared_base;
uint32_t current_write_offset = 0;
uint32_t current_read_offset = 0;

void urpc_write(const uint32_t message[URPC_PAYLOAD_LEN], coreid_t core)
{
    volatile uint32_t *tx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_write_offset*LINE_WORDS;
    while (*(tx + LINE_WORDS -1));

    for (int i = 0; i < URPC_PAYLOAD_LEN; i++) {
        tx[i] = message[i];
    }

    dmb();

    tx[LINE_WORDS - 1] = 1;

    current_write_offset++;
    current_write_offset %= N_LINES;
}

void urpc_read_and_process(coreid_t core)
{
    volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;

    if (rx[0] == 1 && rx[1] != AOS_RPC_ID_ACK) {
        // otherwise, it's a request

        debug_printf("rx[1] = %x\n", rx[1]);
        switch (rx[1]) {
            case AOS_RPC_ID_PROCESS:
                urpc_spawn_handler(core, (uint32_t *) rx + 2);
                break;
            case AOS_RPC_ID_GET_PIDS:
                debug_printf("URPC get pids not yet implemented\n");
                break;
            case AOS_RPC_ID_GET_PNAME:
                urpc_get_pname_handler(core, (domainid_t) rx + 2);
                break;
            default:
                debug_printf("Message type not supported by init\n");
                return;
        }

    } else {
        debug_printf("Forwarding not implemented!\n");
    }

    current_read_offset++;
    current_read_offset %= N_LINES;
}

void urpc_spawn_handler(coreid_t core, void *name) {
    struct spawninfo *si;
    si = malloc(sizeof(struct spawninfo));

    errval_t err = spawn_load_by_name(name, si);
    uint32_t process_ack[URPC_PAYLOAD_LEN];
    process_ack[0] = 1;
    process_ack[1] = AOS_RPC_ID_ACK;
    process_ack[2] = si->domain_id;
    process_ack[3] = err;
    urpc_write(process_ack, 1 - core);
}

void urpc_get_pname_handler(coreid_t core, domainid_t pid) {
    const char *name = process_manager_get_name(&pm, pid);
    size_t length = strlen(name);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 1;
    pname_ack[1] = AOS_RPC_ID_ACK;
    pname_ack[2] = length;

    // put into msg[3..9], 7 words <=> 28 bytes
    strncpy((char *)&pname_ack[3], name, 28);
    urpc_write(pname_ack, 1 - core);

    if (length <= 28)
        return;

    // if there are remaining characters
    length -= 28;
    name += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[2..9]
        strncpy((char *)&pname_ack[2], (const char *)name, 32);

        urpc_write(pname_ack, 1 - core);

        name += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&pname_ack[2], (const char*) name, length);

        urpc_write(pname_ack, 1 - core);
    }
}

void urpc_get_pname_handler(coreid_t core, domainid_t pid) {
    const char *name = process_manager_get_name(&pm, pid);
    size_t length = strlen(name);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 1;
    pname_ack[1] = AOS_RPC_ID_ACK;
    pname_ack[2] = length;

    // put into msg[3..9], 7 words <=> 28 bytes
    strncpy((char *)&pname_ack[3], name, 28);
    urpc_write(pname_ack, 1 - core);

    if (length <= 28)
        return;

    // if there are remaining characters
    length -= 28;
    name += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[2..9]
        strncpy((char *)&pname_ack[2], (const char *)name, 32);

        urpc_write(pname_ack, 1 - core);

        name += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&pname_ack[2], (const char*) name, length);

        urpc_write(pname_ack, 1 - core);
    }
}


void urpc_read_until_ack(uint32_t *ack_response, coreid_t core)
{
    while (true) {
        volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
        while (!*(rx + LINE_WORDS - 1));

        dmb();

        if (rx[0] == 1 && rx[1] == AOS_RPC_ID_ACK) {  // ack, it's a response
            for (int i = 0; i < URPC_PAYLOAD_LEN - 1; i++) {
                ack_response[i] = rx[i+2];
            }

            current_read_offset++;
            current_read_offset %= N_LINES;
            return;
        }

        urpc_read_and_process(core);

    }
}

#endif /* _INIT_RPC_SERVER_H_ */
