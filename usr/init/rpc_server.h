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

#define INIT_PROCESS_ID         (0)

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

struct nameserver_answer_t {
    struct lmp_chan sender_lmp;
};

extern struct process_manager pm;
extern coreid_t my_core_id;
extern struct capref ns_endpoint; /// The endpoint cap to nameserver process

errval_t init_rpc(void);
errval_t recv_handler(void* arg);
errval_t whois(struct capref cap, struct client **he_is);
void* answer_number(struct capref cap, struct lmp_recv_msg *msg);
void* answer_char(struct capref cap, struct lmp_recv_msg* msg);
void* answer_get_char(struct capref cap, struct lmp_recv_msg* msg);
void* answer_str(struct capref cap, struct lmp_recv_msg* msg);
void* answer_process(struct capref cap, struct lmp_recv_msg* msg);
void* answer_pids(struct capref cap, struct lmp_recv_msg* msg);
void* answer_pname(struct capref cap, struct lmp_recv_msg* msg);
void* answer_init(struct capref cap);
void* answer_ram(struct capref cap, struct lmp_recv_msg* msg);
void *answer_nameserver_ep(struct capref cap, struct lmp_recv_msg *msg);
void* answer_device_cap(struct capref cap, struct lmp_recv_msg* msg);

errval_t send_received(void* arg);
errval_t send_ram(void* args);
errval_t send_process(void* args);
errval_t send_pids(void* args);
errval_t send_pname(void* args);
errval_t send_nameserver_ep(void *args);
errval_t send_char(void* args);
errval_t send_device_cap(void* args);

/* urpc declarations */
void urpc_write(const uint32_t message[URPC_PAYLOAD_LEN], coreid_t core);
errval_t urpc_read_and_process_non_block(coreid_t core);
void urpc_read_and_process(uint32_t *rx, coreid_t core);
void urpc_read_until_ack(uint32_t *ack_response, coreid_t core);
static inline void dmb(void) { __asm volatile ("dmb"); }
void urpc_spawn_handler(coreid_t core, void *name);
void urpc_get_pids_handler(coreid_t core, domainid_t pid);
void urpc_get_pname_handler(coreid_t core, domainid_t pid);
void urpc_get_char_handler(coreid_t core);

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
    case AOS_RPC_ID_GET_CHAR:
        answer_args = answer_get_char(cap_endpoint, &msg);
        answer = (void*) send_char;
        break;
    case AOS_RPC_ID_GET_NAMESERVER_EP:
        answer_args = answer_nameserver_ep(cap_endpoint, &msg);
        answer = (void*) send_nameserver_ep;
        break;
    case AOS_RPC_ID_GET_DEVICE_CAP:
        answer_args = answer_device_cap(cap_endpoint, &msg);
        answer = (void*) send_device_cap;
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
    *(size_t *)args = length;
    args += sizeof(size_t);
    // copy pid to args
    if (length) {
        memcpy(args, arr, sizeof(domainid_t) * length);
    }

    return args_ptr;
}

void* answer_pname(struct capref cap_endpoint, struct lmp_recv_msg* msg) {
    errval_t err;
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

void* answer_get_char(struct capref cap_endpoint, struct lmp_recv_msg* msg)
{
    domainid_t comm_dest = (domainid_t) msg->words[1];
    coreid_t target_core_id = pid_get_core_id(comm_dest);
    char retchar;

    if (target_core_id == my_core_id) {
        retchar = get_next_char_from_buffer();
    } else {
        // send URPC request
        uint32_t message[URPC_PAYLOAD_LEN];
        message[0] = 0;
        message[1] = AOS_RPC_ID_GET_CHAR;
        urpc_write(message, target_core_id);

        // wait for and read response
        urpc_read_until_ack(message, my_core_id);

        retchar = message[0];
    }

    struct client* he_is = NULL;
    errval_t err = whois(cap_endpoint, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer char: could not identify client");
        return NULL;
    }

    size_t size_of_args = ROUND_UP(sizeof(struct lmp_chan),4) + sizeof(char);
    void* args_ptr = malloc(size_of_args);

    void* args = args_ptr;
    // add channel to args
    *((struct lmp_chan*) args) = he_is->lmp;
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    *(char *)args = retchar;

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

void* answer_nameserver_ep(struct capref cap, struct lmp_recv_msg* msg) {
    struct client *sender = NULL;
    errval_t err = whois(cap, &sender);
    if (err_is_fail(err) || sender == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not identify client");
        return NULL;
    }

    struct nameserver_answer_t *args =
        malloc(sizeof(struct nameserver_answer_t));
    args->sender_lmp = sender->lmp;

    return (void*) args;
}

void* answer_device_cap(struct capref cap, struct lmp_recv_msg* msg) {
    struct client *sender = NULL;
    errval_t err = whois(cap, &sender);
    if (err_is_fail(err) || sender == NULL) {
        DEBUG_ERR(err, "usr/init/main.c answer init: could not identify client");
        return NULL;
    }
    struct capref return_cap;
    err = slot_alloc(&return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/rpc_server.h answer device cap: could not alloc slot");
    }
    err = frame_forge(return_cap, (lpaddr_t) msg->words[2], (size_t) msg->words[1], my_core_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/rpc_server.h answer device cap: could not forge frame");
    }

    size_t size_of_args = ROUND_UP(sizeof(struct lmp_chan),4) + ROUND_UP(sizeof(errval_t),4) + ROUND_UP(sizeof(struct capref),4);
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

errval_t send_device_cap(void* args) {
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

    errval_t err_send = lmp_chan_send2(lmp, LMP_FLAG_SYNC, *cap, (size_t)(err_is_fail(*err)? 0:1), (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send device cap: could not do lmp chan send2");
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
    msg[0] = AOS_RPC_ID_ACK;

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

errval_t send_nameserver_ep(void* args) {
    struct nameserver_answer_t *ns_args = (struct nameserver_answer_t *) args;
    struct lmp_chan *lmp = &ns_args->sender_lmp;

    errval_t err_send = lmp_chan_send1(lmp, LMP_FLAG_SYNC, ns_endpoint, 1);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t send_char(void* args)
{
    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    char retchar = *(char*)args;

    errval_t err_send = lmp_chan_send2(lmp, LMP_FLAG_SYNC, NULL_CAP, AOS_RPC_ID_ACK, retchar);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/init/main.c send char: could not do lmp chan send2");
        return err_send;
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

    dmb();

    for (int i = 0; i < URPC_PAYLOAD_LEN; i++) {
        tx[i] = message[i];
    }

    dmb();

    tx[LINE_WORDS - 1] = 1;

    current_write_offset++;
    current_write_offset %= N_LINES;
}


void urpc_read_until_ack(uint32_t *ack_response, coreid_t core)
{
    while (true) {
        volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
        while (!*(rx + LINE_WORDS - 1));

        dmb();

        if (rx[0] == 0 && rx[1] == AOS_RPC_ID_ACK) {  // ack, it's a response
            for (int i = 0; i < URPC_PAYLOAD_LEN - 1; i++) {
                ack_response[i] = rx[i+2];
            }

            dmb();

            rx[LINE_WORDS - 1] = 0;

            current_read_offset++;
            current_read_offset %= N_LINES;
            return;
        }

        urpc_read_and_process((uint32_t *) rx, core);
    }
}

void urpc_read_and_process(uint32_t *rx, coreid_t core)
{
    if (rx[1] != AOS_RPC_ID_ACK) {
        if (rx[0] == 0) {
            switch (rx[1]) {
            case AOS_RPC_ID_PROCESS:
                urpc_spawn_handler(core, (uint32_t *) rx + 2);
                break;
            case AOS_RPC_ID_GET_PIDS:
                urpc_get_pids_handler(core, 0);
                break;
            case AOS_RPC_ID_GET_PNAME:
                urpc_get_pname_handler(core, (domainid_t) rx[2]);
                break;
            case AOS_RPC_ID_GET_CHAR:
                urpc_get_char_handler(core);
                break;
            default:
                debug_printf("Message type not supported by init %lu\n", rx[1]);
                return;
            }
        } else {
            debug_printf("Forwarding not implemented!\n");
        }


        rx[LINE_WORDS - 1] = 0;

        current_read_offset++;
        current_read_offset %= N_LINES;
    } else {
        debug_printf("unhandled AOS_RPC_ID_ACK %i!\n", rx[0]);
    }
}

errval_t urpc_read_and_process_non_block(coreid_t core) {
    volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
    if (*(rx + LINE_WORDS - 1) && rx[1] != AOS_RPC_ID_ACK) {
        // there's something new
        dmb();
        urpc_read_and_process((uint32_t *) rx, my_core_id);
        return SYS_ERR_OK;
    }
    return LIB_ERR_NO_EVENT;
}

void urpc_spawn_handler(coreid_t core, void *name) {
    struct spawninfo *si;
    si = malloc(sizeof(struct spawninfo));

    errval_t err = spawn_load_by_name(name, si);
    uint32_t process_ack[URPC_PAYLOAD_LEN];
    process_ack[0] = 0;
    process_ack[1] = AOS_RPC_ID_ACK;
    process_ack[2] = si->domain_id;
    process_ack[3] = err;
    urpc_write(process_ack, 1 - core);
}

void urpc_get_pids_handler(coreid_t core, domainid_t pid) {
    size_t length;
    domainid_t *arr;
    process_manager_get_all_pids(&pm, &arr, &length);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 0;
    pname_ack[1] = AOS_RPC_ID_ACK;
    pname_ack[2] = length;

    // put into msg[3..9], 7 words
    memcpy((void *)&pname_ack[3], (const void *)arr, sizeof(domainid_t) * MIN(7, length));
    urpc_write(pname_ack, 1 - core);

    if (length <= 7)
        return;

    // if there are remaining elements
    length -= 7;
    arr += 7;
    for (size_t i = 0; i < length / 8; ++i) {
        // put into msg[2..9]
        memcpy((void *)&pname_ack[2], (const void *)arr, sizeof(domainid_t) * 8);

        urpc_write(pname_ack, 1 - core);

        arr += 8;
        length -= 8;
    }

    // remaining partial packet
    if (length) {
        memcpy((void *)&pname_ack[2], (const void*)arr, sizeof(domainid_t) * length);

        urpc_write(pname_ack, 1 - core);
    }
}

void urpc_get_pname_handler(coreid_t core, domainid_t pid) {
    const char *name = process_manager_get_name(&pm, pid);
    size_t length = strlen(name);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 0;
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

    // we don't send the '\0'
}

void urpc_get_char_handler(coreid_t core)
{
    uint32_t char_ack[URPC_PAYLOAD_LEN];
    char_ack[0] = INIT_PROCESS_ID;
    char_ack[1] = AOS_RPC_ID_ACK;
    char_ack[2] = get_next_char_from_buffer();

    urpc_write(char_ack, 1-core);
}

#endif /* _INIT_RPC_SERVER_H_ */
