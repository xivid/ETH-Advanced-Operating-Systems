/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/morecore.h>
#include <aos/paging.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/coreboot.h>
#include <multiboot.h>

#include <mm/mm.h>
#include "mem_alloc.h"
#include <spawn/spawn.h>
#include <spawn/multiboot.h>

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

/* rpc declarations */
struct client {
    struct EndPoint end;  // TODO: store the capref directly? (can avoid ugly comparison in whois())
    struct client* prev;
    struct client* next;

    struct lmp_chan lmp;
} *client_list = NULL;

errval_t init_rpc(void);
errval_t recv_handler(void* arg);
errval_t whois(struct capref cap, struct client **he_is);
void* answer_number(struct capref cap, struct lmp_recv_msg *msg);
void* answer_char(struct capref cap, struct lmp_recv_msg* msg);
void* answer_str(struct capref cap, struct lmp_recv_msg* msg);
void* answer_process(struct capref cap, struct lmp_recv_msg* msg);
void* answer_init(struct capref cap);
void* answer_ram(struct capref cap, struct lmp_recv_msg* msg);
errval_t send_received(void* arg);
errval_t send_ram(void* args);
errval_t send_process(void* args);

/* multi-core declarations */
errval_t boot_core(coreid_t core_id);

/* testing declarations */
bool test_alloc_free(int count);
bool test_virtual_memory(int count, int size);
bool test_multi_spawn(int spawns);
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);

/* init declarations */
coreid_t my_core_id;
struct bootinfo *bi;

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
    struct capref cap;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap);
    if (err_is_fail(err)) {
        if (lmp_err_is_transient(err)) {
            err = lmp_chan_recv(lmp, &msg, &cap);
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
    switch (msg.words[0]) {
        case AOS_RPC_ID_NUM:
            answer_args = answer_number(cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_INIT:
            answer_args = answer_init(cap);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_RAM:
            answer_args = answer_ram(cap, &msg);
            answer = (void*) send_ram;
            break;
        case AOS_RPC_ID_CHAR:
            answer_args = answer_char(cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_STR:
            answer_args = answer_str(cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_PROCESS:
            answer_args = answer_process(cap, &msg);
            answer = (void*) send_process;
            break;
        default:
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

// TODO: rewrite this with cap_compare()
errval_t whois(struct capref cap, struct client **he_is) {
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c whois: debug identify cap failed");
        return err;
    }
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
    errval_t err;
    debug_printf("got coreid: %d, process name: %s\n", (int)msg->words[1], (char *)&msg->words[3]);
    coreid_t target_core_id = *(coreid_t *) &msg->words[1];
    if (target_core_id != my_core_id) {
        // aos_rpc_remote_spawn();
        // TODO: implement this
        return NULL;
    }
    struct spawninfo *si = malloc(sizeof(struct spawninfo));
    err = spawn_load_by_name(&msg->words[3], si);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c could not start a process\n");
        return NULL;
    }
    debug_printf("domain id is %d\n", si->domain_id);
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
    *((errval_t*) args) = err;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    *((domainid_t*) args) = si->domain_id;

    return (void*) args_ptr;
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


/* Multi-core implementations */
errval_t boot_core(coreid_t core_id) {

    /* allocate memory and obtain a new KCB */
    errval_t err;
    struct capref kcb_ram;
    err = ram_alloc(&kcb_ram, OBJSIZE_KCB);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not ram alloc kcb_ram");
        return err;
    }

    struct capref kcb;
    err = slot_alloc(&kcb);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not slot alloc kcb_ram");
        return err;
    }
    err = cap_retype(kcb, kcb_ram, 0, ObjType_KernelControlBlock, OBJSIZE_KCB, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not cap retype kcb");
        return err;
    }

    /* initialize core_data by cloning the running kernel */
    struct frame_identity kcb_id;
    err = frame_identify(kcb, &kcb_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not identify kcb frame");
        return err;
    }

    struct capref core_data_f;
    size_t ret;
    err = frame_alloc(&core_data_f, sizeof(struct arm_core_data), &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame alloc core data frame");
        return err;
    }

    struct frame_identity core_data_id;
    err = frame_identify(core_data_f, &core_data_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame identify core data");
        return err;
    }

    err = invoke_kcb_clone(kcb, core_data_f);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not invoke kcb clone");
        return err;
    }

    // map into our address space so that we can access it
    struct arm_core_data* core_data;
    err = paging_map_frame(get_current_paging_state(), (void**) &core_data, ret, core_data_f, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map frame core data to core_data_f");
        return err;
    }

    struct capref init_space;
    err = frame_alloc(&init_space, ARM_CORE_DATA_PAGES*BASE_PAGE_SIZE, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame alloc init_space");
        return err;
    }

    struct frame_identity init_frame_id;
    err = frame_identify(init_space, &init_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not identify init frame");
        return err;
    }

    // allocate memory for the new core
    core_data->memory_bytes = ret;
    core_data->memory_base_start = init_frame_id.base;
    core_data->cmdline = core_data_id.base + (size_t)((lvaddr_t)&(core_data->cmdline_buf) - (lvaddr_t)core_data);
    core_data->kcb = kcb_id.base;

    // fill rest of core_data
    struct mem_region* module = multiboot_find_module(bi, "init");
    if (module == NULL) {
        debug_printf("usr/init/main.c boot_core: init module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct multiboot_modinfo monitor_modinfo = {
        .mod_start = module->mr_base,
        .mod_end = module->mrmod_size + module->mr_base,
        .string = module->mrmod_data,
        .reserved = 0
    };
    core_data->monitor_module = monitor_modinfo;

    /* allocate memory for URPC frame */
    /*struct capref urpc_frame;
    err = frame_alloc(&urpc_frame, sizeof(MON_URPC_SIZE), &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame alloc urpc_frame");
        return err;
    }*/
    struct frame_identity urpc_frame_id;
    err = frame_identify(cap_urpc, &urpc_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify urpc_frame");
        return err;
    }
    core_data->urpc_frame_base = urpc_frame_id.base;
    core_data->urpc_frame_size = urpc_frame_id.bytes;
    // core_data->chan_id = 1; // TODO: what should it be?

    /* load and relocate cpu driver */
    struct mem_region* cpu_module = multiboot_find_module(bi, "cpu_omap44xx");
    if (cpu_module == NULL) {
        debug_printf("usr/init/main.c boot_core: cpu driver module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct capref cpu_frame = {
        .cnode = cnode_module,
        .slot = cpu_module->mrmod_slot
    };

    struct frame_identity cpu_frame_id;
    err = frame_identify(cpu_frame, &cpu_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify");
        return err;
    }

    // memory for relocatable segment
    struct capref relocatable_segment;
    err = frame_alloc(&relocatable_segment, cpu_frame_id.bytes, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame alloc relocatable_segment");
        return err;
    }

    void* segment_addr;
    err = paging_map_frame(get_current_paging_state(), &segment_addr, ret, relocatable_segment, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map relocatable segment memory");
        return err;
    }

    struct frame_identity segment_id;
    err = frame_identify(relocatable_segment, &segment_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify the relocatable_segment");
        return err;
    }

    // elf text segment
    void* elfdata;
    err = paging_map_frame(get_current_paging_state(), &elfdata, cpu_frame_id.bytes, cpu_frame, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map elf frame");
        return err;
    }

    err = load_cpu_relocatable_segment(elfdata, segment_addr, segment_id.base, core_data->kernel_load_base, &core_data->got_base);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not load cpu relocatable segment");
        return err;
    }

    sys_armv7_cache_clean_pou(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_clean_poc(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_invalidate(core_data, core_data + sizeof(core_data));

    // Do we really want to clear the cache for physical addresses?
    sys_armv7_cache_clean_pou((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_clean_poc((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_invalidate((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);

    err = sys_debug_flush_cache();
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not flush cache");
        return err;
    }

    err = invoke_monitor_spawn_core(core_id, CPU_ARM7, core_data_id.base);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not invoke cpu1");
        return err;
    }
    return SYS_ERR_OK;
}

/* tests implementations */
__attribute__((unused))
bool test_alloc_free(int allocations) {
    struct capref capabilities[allocations];
    errval_t err;
    size_t size;

    for (int i = 0; i < allocations; i++) {
        size = BASE_PAGE_SIZE << (i % 9);
        err = ram_alloc_aligned(capabilities + i, size, size);
        if (err_is_fail(err)) {
            debug_printf("Failed allocating capability %i\n", i);
            return false;
        }
    }
    debug_printf("allocations test succeeded\n");
    for (int i = 0; i < allocations; i++) {
        size = BASE_PAGE_SIZE << (i % 9);

        err = aos_ram_free(capabilities[i], size);
        if (err_is_fail(err)) {
            debug_printf("Failed freeing capability %i\n", i);
            return false;
        }
    }
    debug_printf("frees test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_virtual_memory(int count, int size) {
    errval_t err;

    for (int i = 0 ; i < count ; ++i) {
        size_t retsize;
        struct capref frame;
        void *buf;
        frame_alloc(&frame, size, &retsize);
        err = paging_map_frame_readwrite(&buf, size, frame);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Failed mapping %i\n", i);
            return false;
        } else {
            debug_printf("Succeeded mapping %i\n", i);
            debug_printf("-----------------------------\n");
        }
        ((int *)buf)[0] = 42;
    }
    debug_printf("virtual memory test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_multi_spawn(int spawns) {
    errval_t err;

    for (int i = 0; i < spawns; i++) {
        struct spawninfo *si = malloc(sizeof(struct spawninfo));
        err = spawn_load_by_name("/armv7/sbin/hello", si);
        if (err_is_fail(err)) {
            debug_printf("Failed spawning process %i\n", i);
            return false;
        }
    }
    debug_printf("Spawning test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_huge_malloc(void) {
    debug_printf("testing huge malloc\n");
    int *tmp = malloc(100 * 1024 * 1024);
    tmp[10000] = 42;
    tmp[10001] = 43;
    debug_printf("tmp=%p &tmp[10000]=%p\n", tmp, &tmp[10000]);
    debug_printf("testing huge malloc done\n");
    return true;
}

__attribute__((unused))
bool test_dynamic_slots(int count) {
    debug_printf("testing dynamic slots\n");
    for (int i = 0 ; i < count ; ++i) {
        struct capref slot;
        /* errval_t err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL); */
        errval_t err = slot_alloc(&slot);
        if (err_is_fail(err)) {
            debug_printf("err at test: %d. err=%s\n", i, err_getstring(err));
            return false;
        }
    }
    debug_printf("testing dynamic slots done\n");
    return true;
}

/* main */
int main(int argc, char *argv[])
{
    errval_t err;
    client_list = NULL;
    /* Set the core id in the disp_priv struct */
    err = invoke_kernel_get_core_id(cap_kernel, &my_core_id);
    assert(err_is_ok(err));
    disp_set_core_id(my_core_id);

    debug_printf("init: on core %" PRIuCOREID " invoked as:", my_core_id);
    for (int i = 0; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");

    /* First argument contains the bootinfo location, if it's not set */
    bi = (struct bootinfo*)strtol(argv[1], NULL, 10);
    if (!bi) {
        assert(my_core_id > 0);
    }

    // TODO: we initialize ram for first core here
    // and should return to this method what part of memory is assigned to the core (fill mem_left and mem_base)
    struct bootinfo *app_bi = malloc(sizeof(struct bootinfo) + bi->regions_length * sizeof(struct mem_region));
    *app_bi = *bi;
    if (my_core_id == 0) {
        // split every empty memory region into two, and let app core have the second half
        for (int i = 0; i < bi->regions_length; i++) {
            app_bi->regions[i] = bi->regions[i];
            if (bi->regions[i].mr_type == RegionType_Empty) {
                gensize_t halfsize = bi->regions[i].mr_bytes >> 1;
                app_bi->regions[i].mr_base = bi->regions[i].mr_base + halfsize;
                app_bi->regions[i].mr_bytes = bi->regions[i].mr_bytes - halfsize;
                bi->regions[i].mr_bytes = halfsize;
            }
        }
        err = initialize_ram_alloc();
        if(err_is_fail(err)){
            DEBUG_ERR(err, "initialize_ram_alloc");
            return EXIT_FAILURE;
        }
    }

    void* shared_buf;
    if (my_core_id == 0) {
        size_t ret;
        // alloc frame for shared memory between cores
        frame_alloc(&cap_urpc, MON_URPC_SIZE, &ret);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "main.c: cap_urpc frame alloc");
            return EXIT_FAILURE;
        }
        // map the urpc frame for bsp core
        err = paging_map_frame(get_current_paging_state(), &shared_buf, MON_URPC_SIZE, cap_urpc, NULL, NULL);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "main.c: cap_urpc paging map frame");
            return EXIT_FAILURE;
        }
    } else {
        shared_buf = (void *)MON_URPC_VBASE;
    }

    char buf[256];
    debug_print_cap_at_capref(buf, 256, cap_urpc);
    debug_printf("core %d cap_urpc: %s, mapped at %p\n", my_core_id, buf, shared_buf);

    // write information for second core into shared buf
    if (my_core_id == 0) {
        // write bootinfo to shared mem
        *((struct bootinfo*) shared_buf) = *app_bi;
        shared_buf += sizeof(struct bootinfo);
        for (int i = 0; i < app_bi->regions_length; ++i) {
            *((struct mem_region*) shared_buf) = app_bi->regions[i];
            shared_buf += sizeof(struct mem_region);
        }
    } else {
        // read in app core
        bi = (struct bootinfo*) shared_buf;
        // shared_buf += sizeof(struct bootinfo) + sizeof(struct mem_region) * bi->regions_length;

        struct capref mem_cap = {
                .cnode = cnode_super,
                .slot = 0,
        };
        for (int i = 0; i < bi->regions_length; i++) {
            if (bi->regions[i].mr_type == RegionType_Empty) {
                err = ram_forge(mem_cap, bi->regions[i].mr_base, bi->regions[i].mr_bytes, my_core_id);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err, "main.c: ram_forge failed");
                    return EXIT_FAILURE;
                }
                ++mem_cap.slot;
            }
        }
    }

    debug_printf("core %d bootinfo: regions_length %u, mem_spawn_core %u, regions (base:bytes:type/modname):\n", my_core_id, bi->regions_length, bi->mem_spawn_core);
    for (int i = 0; i < bi->regions_length; ++i) {
        if (bi->regions[i].mr_type == RegionType_Module)
            debug_printf("\t(0x%llx:0x%lx:[%s])\n", bi->regions[i].mr_base, bi->regions[i].mrmod_size, my_core_id ? "" : multiboot_module_name(&bi->regions[i]));
        else
            debug_printf("\t(0x%llx:0x%llx:%d)\n", bi->regions[i].mr_base, bi->regions[i].mr_bytes, bi->regions[i].mr_type);
    }
    printf("\n");

    if (my_core_id == 1) {
        err = initialize_ram_alloc(); // use mem_base and mem_left
        if(err_is_fail(err)){
            DEBUG_ERR(err, "initialize_ram_alloc of core 1");
            return EXIT_FAILURE;
        }
    }

    err = init_rpc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "init_rpc");
        return EXIT_FAILURE;
    }

    //test_multi_spawn(1);

    /* struct spawninfo *si = malloc(sizeof(struct spawninfo)); */
    /* err = spawn_load_by_name("/armv7/sbin/hello", si); */
    /* if (err_is_fail(err)) { */
    /*     debug_printf("Failed spawning process memeater\n"); */
    /*     return false; */
    /* } */

    test_virtual_memory(10, BASE_PAGE_SIZE);

    if (my_core_id == 0) {
        debug_printf("booting core 1\n");
        err = boot_core(1);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/init/main.c: boot_core(1) failed");
            return EXIT_FAILURE;
        }
    }

    debug_printf("Message handler loop\n");
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}
